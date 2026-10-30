// Transmission for the PS5: platform functions found by NID, calls with a time limit, the rest mode call.
//
// License: GPL-3.0-or-later (like Transmission).

#include "ui_internal.h"

namespace ps5tm_ui
{

// ---------------------------------------------------------------- platform functions: lookup and isolated call

// NID = hash of the function name (salted SHA-1, first 8 bytes in reverse order, base64). The algorithm
// was verified on a known example from Payload Manager (sceAppInstUtilAppInstallTitleDir = Wudg3Xe3heE).

// Module lists: constant arrays (no constructors at ELF load)

// Function by NID in the first module found from the list. nullptr + reason if the module or the function is missing.
static void* find_function(ModuleList const& modules, char const* nid, std::string& why)
{
    for (std::size_t i = 0; i < modules.count; ++i)
    {
        char const* const module = modules.names[i];
        std::uint32_t handle = 0;
        if (kernel_dynlib_handle(-1, module, &handle) == 0)
        {
            void* const fn = reinterpret_cast<void*>(kernel_dynlib_resolve(-1, handle, nid));
            if (fn != nullptr)
            {
                return fn;
            }
            why = std::string{ "function (NID " } + nid + ") not found in " + module;
            return nullptr;
        }
    }
    why = std::string{ "module not loaded in the process: " } + modules.names[0];
    return nullptr;
}

// Runs a task in a separate thread and waits no longer than timeout seconds. The task must access
// only global data and data from shared_ptr (the thread may outlive the caller).
std::string run_isolated(std::function<std::string()> job, int timeout_seconds, char const* what)
{
    struct Shared
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        std::string result;
    };
    auto const shared = std::make_shared<Shared>();
    std::thread(
        [shared, job]
        {
            std::string r = job();
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->result = std::move(r);
            shared->done = true;
            shared->cv.notify_all();
        })
        .detach();

    std::unique_lock<std::mutex> lock(shared->mutex);
    if (!shared->cv.wait_for(lock, std::chrono::seconds(timeout_seconds), [&] { return shared->done; }))
    {
        return std::string{ what } + ": call did not return within " + std::to_string(timeout_seconds) + " s";
    }
    return shared->result;
}
// ---------------------------------------------------------------- sleep

// Same as ps5-linux-loader (enter_rest_mode). Returns "" on success or the error text.
std::string enter_rest_mode_impl()
{
    std::string why;
    auto const open_flag = reinterpret_cast<int (*)(void*, char const*)>(find_function(KernelModules, NidOpenEventFlag, why));
    auto const notify = reinterpret_cast<int (*)()>(find_function(KernelModules, NidNotifySuspendStart, why));
    auto const set_flag = reinterpret_cast<int (*)(void*, int)>(find_function(KernelModules, NidSetEventFlag, why));
    auto const close_flag = reinterpret_cast<int (*)(void*)>(find_function(KernelModules, NidCloseEventFlag, why));
    if (open_flag == nullptr || notify == nullptr || set_flag == nullptr || close_flag == nullptr)
    {
        return "sleep is unavailable on this firmware: " + why;
    }

    void* event = nullptr;
    int const err = open_flag(&event, "SceSystemStateMgrStatus");
    if (err != 0 || event == nullptr)
    {
        return "could not open the system state flag: " + hex8(err);
    }
    notify();
    set_flag(event, 0x400);
    close_flag(&event);
    return "";
}

} // namespace ps5tm_ui
