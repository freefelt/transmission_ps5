// Transmission for the PS5: disconnecting a USB drive through the system ("Safely remove from PS5").
//
// An ordinary unmount(2) from our process is not enough for the console's extended storage (/mnt/ext0, /dev/es0.crypt): afterwards the
// console shows the drive with a wrong size and complains about an improper removal. So we do what the system menu does: we ask the
// automounter of ShellCore, over IPMI, to eject the device. The protocol (the service SceAutoMounterIpc, the methods "list devices",
// "mount point of a device" and "eject") was found in ShadowMountPlus (https://github.com/drakmor/ShadowMountPlus, src/sm_usb.c,
// GPL-3.0), where it is verified on firmware 9.40; this is our own implementation of the same sequence.
//
// The IPMI client of libSceIpmi is a C++ class: its functions are taken with dlsym by their mangled names (the manager loads
// libSceIpmi anyway), so the binary gets no new imports. The whole sequence runs in a worker thread, because a call into the system can hang:
// the caller waits a limited time, and when the thread does not return, the system eject is switched off until the manager is
// restarted. Before the sequence a marker is written to a file; if the previous run did not remove it (the process crashed or hung inside
// the system), the eject stays switched off (like the system-service thread does; delete the skip file to try again).
//
// License: GPL-3.0-or-later (like Transmission).

#include "ui_internal.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#ifndef __linux__
#include <dlfcn.h>
#endif

namespace ps5tm_ui
{

#ifdef __linux__

// The Linux host (tests): there is no system to ask. The file /tmp/ps5tm_test_eject switches the stub on: "ok" detaches the mount like the system
// would, "busy" and "fail" return errors; without the file the system eject is not available (ENOTSUP) and the callers take the old way.
static constexpr char const* TestEjectFile = "/tmp/ps5tm_test_eject";

bool usb_system_eject_available()
{
    return access(TestEjectFile, F_OK) == 0;
}

int usb_system_eject(std::string const& path, int& native_status)
{
    native_status = 0;
    char mode[32] = {};
    FILE* f = std::fopen(TestEjectFile, "r");
    if (f == nullptr)
    {
        return ENOTSUP;
    }
    if (std::fgets(mode, sizeof(mode), f) == nullptr)
    {
        mode[0] = 0;
    }
    std::fclose(f);
    if (FILE* log = std::fopen("/tmp/ps5tm_test_eject.log", "a"))
    {
        std::fprintf(log, "eject %s\n", path.c_str());
        std::fclose(log);
    }
    if (std::strncmp(mode, "ok", 2) == 0)
    {
        return umount2(path.c_str(), MNT_DETACH) == 0 ? 0 : errno;
    }
    if (std::strncmp(mode, "busy", 4) == 0)
    {
        return EBUSY;
    }
    if (std::strncmp(mode, "gone", 4) == 0)
    {
        return ENOENT;
    }
    native_status = static_cast<int>(0x80020005u);
    return EIO;
}

#else

static constexpr char const* EjectProbeFile = "/data/Transmission/usb_eject_probe.txt";
static constexpr char const* EjectSkipFile = "/data/Transmission/usb_eject_skip.txt";
static constexpr int EjectWaitSeconds = 25;

static std::atomic<bool> g_eject_off{ false };
static std::mutex g_eject_mutex; // one eject at a time

// Whether the previous run left the marker: the process did not come back from the system. Checked once per process.
static void eject_check_previous()
{
    static std::once_flag once;
    std::call_once(once,
                   []
                   {
                       char name[64] = {};
                       FILE* f = std::fopen(EjectProbeFile, "r");
                       if (f == nullptr)
                       {
                           return;
                       }
                       if (std::fgets(name, sizeof(name), f) == nullptr)
                       {
                           name[0] = 0;
                       }
                       std::fclose(f);
                       std::remove(EjectProbeFile);
                       if (FILE* sf = std::fopen(EjectSkipFile, "a"))
                       {
                           std::fprintf(sf, "%s\n", name[0] != 0 ? name : "?");
                           std::fclose(sf);
                       }
                       g_eject_off = true;
                       shim_log("usb eject: the previous request did not return (%s): the system eject is off; delete %s to try again", name, EjectSkipFile);
                   });
    if (!g_eject_off && access(EjectSkipFile, F_OK) == 0)
    {
        g_eject_off = true;
    }
}

namespace
{
struct IpmiInput // the firmware's DataInfo
{
    void const* data;
    std::size_t size;
};
struct IpmiOutput // the firmware's BufferInfo: the third field is written by IPMI
{
    void* data;
    std::size_t capacity;
    std::size_t written;
};
static_assert(sizeof(IpmiInput) == 16, "IPMI DataInfo ABI");
static_assert(sizeof(IpmiOutput) == 24, "IPMI BufferInfo ABI");

struct IpmiApi
{
    void (*config_ctor)(void*) = nullptr;
    std::uint64_t (*storage_size)(void*) = nullptr;
    int (*create)(void**, void const*, void*, void*) = nullptr;
    int (*connect)(void*, void const*, std::size_t, std::int32_t*) = nullptr;
    int (*invoke)(void*, std::uint32_t, IpmiInput const*, std::uint32_t, std::int32_t*, IpmiOutput*, std::uint32_t) = nullptr;
    void (*terminate)(void*) = nullptr;
    void (*destroy)(void*) = nullptr;
};

constexpr std::uint32_t MethodListDevices = 0x30015;
constexpr std::uint32_t MethodMountPoint = 0x30013;
constexpr std::uint32_t MethodEject = 0x30025;
constexpr std::uint32_t DeviceLimit = 8;
constexpr std::size_t MountPointSize = 128;

bool load_api(IpmiApi& api)
{
    void* const h = dlopen("libSceIpmi.sprx", RTLD_LAZY);
    if (h == nullptr)
    {
        return false;
    }
    api.config_ctor = reinterpret_cast<void (*)(void*)>(dlsym(h, "_ZN4IPMI6Client6ConfigC1Ev"));
    api.storage_size = reinterpret_cast<std::uint64_t (*)(void*)>(dlsym(h, "_ZN4IPMI6Client6Config24estimateClientMemorySizeEv"));
    api.create = reinterpret_cast<int (*)(void**, void const*, void*, void*)>(dlsym(h, "_ZN4IPMI6Client6createEPPS0_PKNS0_6ConfigEPvS6_"));
    api.connect = reinterpret_cast<int (*)(void*, void const*, std::size_t, std::int32_t*)>(dlsym(h, "_ZN4IPMI4impl10ClientImpl7connectEPKvmPi"));
    api.invoke = reinterpret_cast<int (*)(void*, std::uint32_t, IpmiInput const*, std::uint32_t, std::int32_t*, IpmiOutput*, std::uint32_t)>(
        dlsym(h, "_ZN4IPMI4impl10ClientImpl16invokeSyncMethodEjPKNS_8DataInfoEjPiPNS_10BufferInfoEj"));
    api.terminate = reinterpret_cast<void (*)(void*)>(dlsym(h, "_ZN4IPMI4impl10ClientImpl19terminateConnectionEv"));
    api.destroy = reinterpret_cast<void (*)(void*)>(dlsym(h, "_ZN4IPMI4impl10ClientImpl7destroyEv"));
    return api.config_ctor != nullptr && api.storage_size != nullptr && api.create != nullptr && api.connect != nullptr && api.invoke != nullptr &&
        api.terminate != nullptr && api.destroy != nullptr;
}

// A system status 0x8002xxxx carries an errno in its low 16 bits; any other failure is EIO.
int native_error(std::int32_t status)
{
    auto const u = static_cast<std::uint32_t>(status);
    if ((u & 0xffff0000u) == 0x80020000u && (u & 0xffffu) != 0)
    {
        return static_cast<int>(u & 0xffffu);
    }
    return status != 0 ? EIO : 0;
}

struct Client
{
    void* storage = nullptr;
    void* client = nullptr;
};

// destroy() is void and can leave a live connection on failure: its kid field (offset 8) is -1 on success (checked in libSceIpmi 9.40). A client that
// could not be destroyed keeps its storage until the process exits, and no further request is made.
void close_client(IpmiApi const& api, Client& c)
{
    if (c.client != nullptr)
    {
        api.terminate(c.client);
        api.destroy(c.client);
        std::uint32_t kid = 0;
        std::memcpy(&kid, static_cast<char const*>(c.client) + 8, sizeof(kid));
        if (kid != UINT32_MAX)
        {
            g_eject_off = true;
            shim_log("usb eject: the IPMI client could not be destroyed (kid 0x%08x): the system eject is off until restart", kid);
            return;
        }
    }
    std::free(c.storage);
}

int open_client(IpmiApi const& api, Client& c, std::int32_t& native)
{
    alignas(16) unsigned char config[0x200] = {};
    api.config_ctor(config);
    std::memcpy(config, "SceAutoMounterIpc", sizeof("SceAutoMounterIpc")); // the name of the service is 17 characters
    std::uint32_t const flag = 1;
    std::uint64_t const request_size = 0x200;
    std::uint64_t const response_size = 0xf800;
    std::memcpy(config + 0x1c, &flag, sizeof(flag));
    std::memcpy(config + 0x28, &request_size, sizeof(request_size));
    std::memcpy(config + 0x30, &response_size, sizeof(response_size));
    std::uint64_t const size = api.storage_size(config);
    if (size < 0x1000 || size > 0x100000)
    {
        return ENOTSUP;
    }
    c.storage = std::calloc(1, static_cast<std::size_t>(size));
    if (c.storage == nullptr)
    {
        return ENOMEM;
    }
    int rc = api.create(&c.client, config, nullptr, c.storage);
    if (rc != 0 || c.client == nullptr)
    {
        native = rc;
        c.client = nullptr;
        return rc != 0 ? native_error(rc) : EIO;
    }
    std::int32_t result = 0;
    rc = api.connect(c.client, nullptr, 0, &result);
    native = rc != 0 ? rc : result;
    return native_error(native);
}

int call(IpmiApi const& api, Client& c, std::uint32_t method, IpmiInput const* in, IpmiOutput* out, std::uint32_t out_count, std::int32_t& native)
{
    std::int32_t result = 0;
    int const rc = api.invoke(c.client, method, in, in != nullptr ? 1 : 0, &result, out, out_count);
    native = rc != 0 ? rc : result;
    return native_error(native);
}

// The sequence itself: list the devices, find the one that has our mount point, eject it once (a failed eject is never repeated).
int eject_sequence(std::string const& path, std::int32_t& native)
{
    IpmiApi api;
    if (!load_api(api))
    {
        return ENOTSUP;
    }
    Client c;
    int status = open_client(api, c, native);
    if (status == 0)
    {
        std::uint32_t ids[DeviceLimit] = {};
        std::int32_t count = 0;
        std::uint64_t capacity = DeviceLimit;
        IpmiInput const input{ &capacity, sizeof(capacity) };
        IpmiOutput output[2] = { { ids, sizeof(ids), 0 }, { &count, sizeof(count), 0 } };
        status = call(api, c, MethodListDevices, &input, output, 2, native);
        if (status == 0 && (count < 0 || count > static_cast<std::int32_t>(DeviceLimit) || output[0].written != sizeof(ids) || output[1].written != sizeof(count)))
        {
            status = EIO;
        }
        if (status == 0)
        {
            status = ENOENT;
            for (std::int32_t i = 0; i < count; ++i)
            {
                char mount_point[MountPointSize] = {};
                std::uint32_t const args[2] = { ids[i], static_cast<std::uint32_t>(sizeof(mount_point)) };
                IpmiInput const point_input{ args, sizeof(args) };
                IpmiOutput point_output{ mount_point, sizeof(mount_point), 0 };
                status = call(api, c, MethodMountPoint, &point_input, &point_output, 1, native);
                if (status != 0)
                {
                    break;
                }
                if (std::memchr(mount_point, '\0', sizeof(mount_point)) == nullptr || point_output.written != sizeof(mount_point))
                {
                    status = EIO;
                    break;
                }
                if (path != mount_point)
                {
                    status = ENOENT;
                    continue;
                }
                IpmiInput const eject_input{ &ids[i], sizeof(ids[i]) };
                status = call(api, c, MethodEject, &eject_input, nullptr, 0, native);
                break;
            }
        }
    }
    close_client(api, c);
    return status;
}
} // namespace

bool usb_system_eject_available()
{
    eject_check_previous();
    return !g_eject_off;
}

struct EjectJob
{
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    int status = EIO;
    std::int32_t native = 0;
};

int usb_system_eject(std::string const& path, int& native_status)
{
    native_status = 0;
    eject_check_previous();
    if (g_eject_off)
    {
        return ENOTSUP;
    }
    std::lock_guard<std::mutex> const one_at_a_time{ g_eject_mutex };
    if (FILE* f = std::fopen(EjectProbeFile, "w"))
    {
        std::fputs("eject", f);
        std::fclose(f);
    }
    auto job = std::make_shared<EjectJob>();
    std::thread(
        [job, path]
        {
            std::int32_t native = 0;
            int const status = eject_sequence(path, native);
            std::lock_guard<std::mutex> const lock{ job->m };
            job->status = status;
            job->native = native;
            job->done = true;
            job->cv.notify_all();
        })
        .detach();
    std::unique_lock<std::mutex> lock{ job->m };
    if (!job->cv.wait_for(lock, std::chrono::seconds(EjectWaitSeconds), [&job] { return job->done; }))
    {
        g_eject_off = true; // the thread stays in the system; the marker file stays too, so that the next start does not try again
        shim_log("usb eject: the system did not answer in %d s: the system eject is off until restart", EjectWaitSeconds);
        return ETIMEDOUT;
    }
    std::remove(EjectProbeFile);
    native_status = job->native;
    shim_log("usb eject: %s: status %d, native 0x%08x", path.c_str(), job->status, static_cast<unsigned>(job->native));
    return job->status;
}

#endif

} // namespace ps5tm_ui
