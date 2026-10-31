// Transmission for the PS5: the power state of the console (rest mode, shutdown), read from the system's event flags.
//
// The console does NOT freeze the payload in rest mode (the manager and the daemon go on running, no SIGSTOP, no time gap), so the payload has to be told. The event flag
// SceSystemStateMgrInfo holds the state of the system manager in its low 16 bits (10 starting, 100 shutting down, 200 power saving, 300 suspend on going, 500 main on standby,
// 1000 working); SceSystemStateMgrStatus has among others the bit 0x200000 "the shell UI is shutting down". ShadowMountPlus (drakmor, GPL-3.0) reads the same flags the same
// way: open by name, poll without waiting. Everything here is a read-only poll in a thread of its own.
//
// What is done with it: sleep.cc (sleep_power_enter / sleep_power_leave): the daemon is stopped cleanly before the console sleeps and started again when it is awake.
//
// License: GPL-3.0-or-later (like Transmission).

#include "core_internal.h"

extern "C"
{
int kernel_dynlib_handle(int pid, char const* basename, std::uint32_t* handle);
std::intptr_t kernel_dynlib_resolve(int pid, std::uint32_t handle, char const* nid);
}

namespace ps5tm_core
{

namespace
{

constexpr unsigned StateStarting = 10;
constexpr unsigned StateShutdown = 100;
constexpr unsigned StateSaving = 200;
constexpr unsigned StateSuspend = 300;
constexpr unsigned StateStandby = 500;
constexpr unsigned StateWorking = 1000;
constexpr std::uint64_t StatusShellUiShutdown = 0x200000ULL;
constexpr unsigned PollMilliseconds = 300;
constexpr unsigned WaitModeOr = 2;

constexpr char const* ProbeFile = "/data/Transmission/power_probe.txt";
constexpr char const* SkipFile = "/data/Transmission/power_skip.txt";
constexpr char const* KernelModuleList[] = { "libkernel_web.sprx", "libkernel.sprx", "libkernel_sys.sprx" };
constexpr char const* NidOpenFlag = "1vDaenmJtyA"; // sceKernelOpenEventFlag
constexpr char const* NidPollFlag = "9lvj5DjHZiA"; // sceKernelPollEventFlag
constexpr char const* NidCloseFlag = "s9-RaxukuzQ"; // sceKernelCloseEventFlag

struct PowerShared
{
    std::mutex m;
    bool ready = false;
    bool disabled = false;
    std::string error;
    unsigned state = 0;
    std::uint64_t info = 0;
    std::uint64_t status = 0;
    bool sleeping = false;
    long since = 0; // when the state last changed
};

PowerShared g_pw;
std::atomic<long> g_pw_last_poll{ 0 };

void* find_kernel_function(char const* nid)
{
    for (char const* module : KernelModuleList)
    {
        std::uint32_t handle = 0;
        if (kernel_dynlib_handle(-1, module, &handle) == 0)
        {
            if (void* const fn = reinterpret_cast<void*>(kernel_dynlib_resolve(-1, handle, nid)))
            {
                return fn;
            }
        }
    }
    return nullptr;
}

char const* state_name(unsigned state)
{
    switch (state)
    {
    case StateStarting: return "starting";
    case StateShutdown: return "shutdown";
    case StateSaving: return "saving";
    case StateSuspend: return "suspend";
    case StateStandby: return "standby";
    case StateWorking: return "working";
    default: return "unknown";
    }
}

void set_error(std::string const& text, bool disabled)
{
    std::lock_guard<std::mutex> lock(g_pw.m);
    g_pw.error = text;
    g_pw.disabled = disabled;
}

} // namespace

// The thread: opens the flags once (a call that crashes or hangs leaves a mark: the next start does not repeat it) and polls them.
void power_main()
{
    struct stat st;
    if (stat(SkipFile, &st) == 0)
    {
        set_error(std::string{ "off: a previous start did not return from a system call; delete " } + SkipFile + " to try again", true);
        shim_log("power: off (%s exists)", SkipFile);
        return;
    }
    if (stat(ProbeFile, &st) == 0)
    {
        write_text_file(SkipFile, "the previous start of the power state monitor did not return\n");
        std::remove(ProbeFile);
        set_error(std::string{ "off: the previous start crashed or hung in a system call; delete " } + SkipFile + " to try again", true);
        shim_log("power: the previous start did not return: the monitor is off until %s is deleted", SkipFile);
        return;
    }
    write_text_file(ProbeFile, "open\n");

    using OpenFn = int (*)(void*, char const*);
    using PollFn = int (*)(void*, std::uint64_t, unsigned, std::uint64_t*);
    using CloseFn = int (*)(void*);
    auto const open_flag = reinterpret_cast<OpenFn>(find_kernel_function(NidOpenFlag));
    auto const poll_flag = reinterpret_cast<PollFn>(find_kernel_function(NidPollFlag));
    auto const close_flag = reinterpret_cast<CloseFn>(find_kernel_function(NidCloseFlag));
    if (open_flag == nullptr || poll_flag == nullptr || close_flag == nullptr)
    {
        std::remove(ProbeFile);
        set_error("the system functions for the power state were not found on this firmware", true);
        shim_log("power: the event flag functions were not found");
        return;
    }
    void* info_flag = nullptr;
    void* status_flag = nullptr;
    int const rc_info = open_flag(&info_flag, "SceSystemStateMgrInfo");
    if (rc_info != 0 || info_flag == nullptr)
    {
        std::remove(ProbeFile);
        char text[96];
        std::snprintf(text, sizeof(text), "SceSystemStateMgrInfo did not open: 0x%08x", static_cast<unsigned>(rc_info));
        set_error(text, true);
        shim_log("power: %s", text);
        return;
    }
    if (open_flag(&status_flag, "SceSystemStateMgrStatus") != 0)
    {
        status_flag = nullptr; // only the shell UI shutdown bit is lost
    }

    unsigned previous = 0;
    bool have_previous = false;
    bool previous_shellui = false;
    bool asleep = false;
    std::uint64_t last_status = 0;
    bool have_status = false;
    int status_logs = 0;
    bool announced = false;
    int awake_polls = 0; // consecutive polls that say "awake": the bit of the shell UI and the state change a few milliseconds apart, one poll can see them in between
    std::uint64_t last_info = 0;
    int info_logs = 0;
    for (;;)
    {
        std::uint64_t info = 0;
        if (poll_flag(info_flag, ~0ULL, WaitModeOr, &info) == 0)
        {
            if (!announced)
            {
                announced = true;
                std::remove(ProbeFile); // the first call came back
                shim_log("power: the state of the console is read (SceSystemStateMgrInfo=0x%016llx)", static_cast<unsigned long long>(info));
            }
            unsigned const state = static_cast<unsigned>(info & 0xFFFFu);
            bool shellui = previous_shellui;
            if (status_flag != nullptr)
            {
                std::uint64_t status = 0;
                if (poll_flag(status_flag, ~0ULL, WaitModeOr, &status) == 0)
                {
                    shellui = (status & StatusShellUiShutdown) != 0;
                    if ((!have_status || status != last_status) && status_logs < 60)
                    {
                        ++status_logs;
                        shim_log("power: SceSystemStateMgrStatus=0x%016llx", static_cast<unsigned long long>(status));
                    }
                    last_status = status;
                    have_status = true;
                    std::lock_guard<std::mutex> lock(g_pw.m);
                    g_pw.status = status;
                }
                else
                {
                    shellui = false; // no bit set: the poll reports an error
                }
            }
            g_pw_last_poll = static_cast<long>(std::time(nullptr));
            long const now = static_cast<long>(std::time(nullptr));
            if (!have_previous || state != previous)
            {
                shim_log("power: the state of the console: %s (%u), SceSystemStateMgrInfo=0x%016llx", state_name(state), state, static_cast<unsigned long long>(info));
            }
            else if (info != last_info && info_logs < 60)
            {
                ++info_logs; // the state is the same, the rest of the pattern (the trigger code) changed
                shim_log("power: SceSystemStateMgrInfo=0x%016llx (the state is still %s)", static_cast<unsigned long long>(info), state_name(state));
            }
            last_info = info;
            bool const sleep_state = state == StateShutdown || state == StateSuspend || state == StateStandby;
            bool const want_sleep = sleep_state || shellui;
            {
                std::lock_guard<std::mutex> lock(g_pw.m);
                if (!g_pw.ready || g_pw.state != state)
                {
                    g_pw.since = now;
                }
                g_pw.ready = true;
                g_pw.state = state;
                g_pw.info = info;
                g_pw.sleeping = asleep || want_sleep;
            }
            if (want_sleep && !asleep)
            {
                asleep = true;
                std::string const reason = sleep_state ? std::string{ state_name(state) } : std::string{ "the shell UI is shutting down" };
                sleep_power_enter(reason.c_str());
            }
            else if (!want_sleep && asleep && state == StateWorking && ++awake_polls >= 2)
            {
                asleep = false;
                {
                    std::lock_guard<std::mutex> lock(g_pw.m);
                    g_pw.sleeping = false;
                }
                sleep_power_leave();
            }
            if (want_sleep || state != StateWorking)
            {
                awake_polls = 0;
            }
            previous = state;
            have_previous = true;
            previous_shellui = shellui;
        }
        usleep(PollMilliseconds * 1000);
    }
}

// Seconds since the last good poll of the flag (-1 none yet): sleep.cc falls back to the network when the monitor is silent
long power_poll_age()
{
    long const last = g_pw_last_poll.load();
    return last == 0 ? -1 : static_cast<long>(std::time(nullptr)) - last;
}

} // namespace ps5tm_core

using namespace ps5tm_core;

std::string ps5tm_power_json()
{
    std::lock_guard<std::mutex> lock(g_pw.m);
    std::string err;
    for (char const c : g_pw.error)
    {
        err += (c == '"' || c == '\\') ? '?' : c;
    }
    std::string j = std::string{ "{\"ready\":" } + (g_pw.ready ? "true" : "false") + ",\"disabled\":" + (g_pw.disabled ? "true" : "false") + ",\"error\":\"" + err + "\"" +
        ",\"state\":" + std::to_string(g_pw.state) + ",\"name\":\"" + state_name(g_pw.state) + "\",\"sleeping\":" + (g_pw.sleeping ? "true" : "false") +
        ",\"since\":" + std::to_string(g_pw.since) + "}";
    return j;
}
