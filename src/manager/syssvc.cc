// Transmission for the PS5: the console system service inside the manager.
//
// License: GPL-3.0-or-later (like Transmission).

#include "core_internal.h"

namespace ps5tm_core
{



// ---------------------------------------------------------------- console system service inside the manager
// This used to be done by a separate helper process transmission_s.elf (port 1234): that is how library-load hangs were dealt with.
// Now the calls come from a worker thread of the manager. The libraries are loaded via dlopen in the order Ipmi, UserService, SystemService (like Payload Manager)
// in that thread, so a possible hang affects only it. A call that does not return within SysHangSeconds disables the service until restart (a bell entry).
// Crash protection: before loading and the first call of each function its name is written to syssvc_probe.txt; if a previous run crashed or hung inside
// a function, the name remains in syssvc_skip.txt and the service does not start (delete the file to retry).
// What the service does: whether a game is on screen or minimised (the sceSystemServiceGetUIStatus value: 0x20010 is the home screen, any other value with a game running means "on screen"),
// the console language code (Russian 8), "do not fall asleep" (PowerTick and reset of the auto power off timer every 10 minutes), the system sleep request (RequestPowerOff).

constexpr int SysKeepAwakePeriodSeconds = 600;
constexpr unsigned SysUiHome = 0x20010;

#ifdef __linux__
// Host (checks): the stubs read the same control files as the helper used to.
static bool ss_mode_has(char const* word)
{
    char buf[64] = {};
    FILE* f = std::fopen("/tmp/ps5tm_sys_mode", "r");
    if (f == nullptr)
    {
        return false;
    }
    std::size_t const n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[n] = 0;
    return std::strstr(buf, word) != nullptr;
}

static void ss_stub_log(char const* text)
{
    if (FILE* f = std::fopen("/tmp/ps5tm_sys_calls.log", "a"))
    {
        std::fprintf(f, "%s\n", text);
        std::fclose(f);
    }
}

static int ss_stub_get_app()
{
    return access("/tmp/ps5tm_sys_app", F_OK) == 0 ? 0x1234 : -1;
}

static int ss_stub_get_title(std::uint32_t, char* title)
{
    char buf[16] = {};
    FILE* f = std::fopen("/tmp/ps5tm_sys_app", "r");
    if (f == nullptr)
    {
        return -1;
    }
    std::size_t const n = std::fread(buf, 1, 9, f);
    std::fclose(f);
    buf[n] = 0;
    buf[std::strcspn(buf, "\r\n")] = 0;
    std::snprintf(title, 10, "%s", buf);
    return 0;
}

static int ss_stub_get_ui(void* out)
{
    if (ss_mode_has("hang"))
    {
        for (;;)
        {
            sleep(1000);
        }
    }
    std::memset(out, 0, 12);
    unsigned ui = (access("/tmp/ps5tm_sys_app", F_OK) == 0 && access("/tmp/ps5tm_sys_suspended", F_OK) != 0) ? 0x20036 : 0x20010;
    if (FILE* vf = std::fopen("/tmp/ps5tm_sys_ui_value", "r"))
    {
        unsigned v = 0;
        if (std::fscanf(vf, "%u", &v) == 1)
        {
            ui = v;
        }
        std::fclose(vf);
    }
    std::memcpy(out, &ui, 4);
    return 0;
}

static int ss_stub_param_int(int, int* value)
{
    int v = 1;
    if (FILE* f = std::fopen("/tmp/ps5tm_sys_lang", "r"))
    {
        if (std::fscanf(f, "%d", &v) != 1)
        {
            v = 1;
        }
        std::fclose(f);
    }
    *value = v;
    return 0;
}

static int ss_stub_tick()
{
    ss_stub_log("PowerTick");
    return 0;
}

static int ss_stub_reset_timer()
{
    ss_stub_log("reset");
    return 0;
}

static int ss_stub_power_off()
{
    ss_stub_log("RequestPowerOff");
    return 0;
}
#endif

struct SysApi
{
    int (*get_app)() = nullptr;
    int (*get_title)(std::uint32_t, char*) = nullptr;
    int (*get_ui)(void*) = nullptr;
    int (*param_int)(int, int*) = nullptr;
    int (*tick)() = nullptr;
    int (*reset_timer)() = nullptr;
    int (*power_off)() = nullptr;
};

struct SysState
{
    std::mutex m;
    bool ready = false; // libraries are loaded, the needed functions are found
    bool hung = false; // a call did not return within SysHangSeconds: the service is no longer used
    bool disabled = false; // a previous start crashed or hung inside a function
    std::string error;
    int app_id = -1;
    std::string title;
    bool ui_valid = false;
    unsigned ui_status = 0;
    int system_lang = -1;
    bool keepawake = false;
    long ka_calls = 0;
    std::time_t last_poll = 0;
    int poweroff_rc = 0;
    bool poweroff_called = false;
};

static SysState g_ss;
static std::atomic<std::time_t> g_ss_busy_since{ 0 };
static std::atomic<char const*> g_ss_busy_name{ nullptr };
static std::atomic<bool> g_ss_want_poweroff{ false };
std::atomic<bool> g_ss_want_ka{ false };

static void ss_mark(char const* name)
{
    if (FILE* f = std::fopen(SysProbeFile, "w"))
    {
        std::fputs(name, f);
        std::fclose(f);
    }
}

static void ss_unmark()
{
    std::remove(SysProbeFile);
}

static bool ss_load(SysApi& api, std::string& err)
{
#ifdef __linux__
    err.clear();
    api.get_app = ss_stub_get_app;
    api.get_title = ss_stub_get_title;
    api.get_ui = ss_stub_get_ui;
    api.param_int = ss_stub_param_int;
    api.tick = ss_stub_tick;
    api.reset_timer = ss_stub_reset_timer;
    api.power_off = ss_stub_power_off;
    return true;
#else
    char const* const libs[3] = { "libSceIpmi.sprx", "libSceUserService.sprx", "libSceSystemService.sprx" };
    void* h[3] = { nullptr, nullptr, nullptr };
    for (int i = 0; i < 3; ++i)
    {
        ss_mark(libs[i]);
        g_ss_busy_name = libs[i];
        g_ss_busy_since = std::time(nullptr);
        h[i] = dlopen(libs[i], RTLD_LAZY);
        g_ss_busy_since = 0;
        g_ss_busy_name = nullptr;
        ss_unmark();
        if (h[i] == nullptr)
        {
            char const* const e = dlerror();
            shim_log("syssvc: dlopen(%s) failed: %s", libs[i], e != nullptr ? e : "?");
        }
    }
    if (h[2] == nullptr)
    {
        err = "dlopen(libSceSystemService.sprx) failed";
        return false;
    }
    auto const sym = [&](char const* name) -> void*
    {
        for (int i = 2; i >= 0; --i)
        {
            if (h[i] != nullptr)
            {
                if (void* const p = dlsym(h[i], name))
                {
                    return p;
                }
            }
        }
        return nullptr;
    };
    api.get_app = reinterpret_cast<int (*)()>(sym("sceLncUtilGetAppIdOfRunningBigApp"));
    api.get_title = reinterpret_cast<int (*)(std::uint32_t, char*)>(sym("sceLncUtilGetAppTitleId"));
    api.get_ui = reinterpret_cast<int (*)(void*)>(sym("sceSystemServiceGetUIStatus"));
    api.param_int = reinterpret_cast<int (*)(int, int*)>(sym("sceSystemServiceParamGetInt"));
    api.tick = reinterpret_cast<int (*)()>(sym("sceSystemServicePowerTick"));
    api.reset_timer = reinterpret_cast<int (*)()>(sym("sceShellCoreUtilResetAutoPowerDownTimer"));
    api.power_off = reinterpret_cast<int (*)()>(sym("sceSystemServiceRequestPowerOff"));
    shim_log("syssvc: functions: app=%d title=%d ui=%d lang=%d tick=%d reset=%d poweroff=%d", api.get_app != nullptr, api.get_title != nullptr, api.get_ui != nullptr,
             api.param_int != nullptr, api.tick != nullptr, api.reset_timer != nullptr, api.power_off != nullptr);
    if (api.get_app == nullptr || api.get_title == nullptr || api.get_ui == nullptr)
    {
        err = "the functions for the running game were not found";
        return false;
    }
    err.clear();
    return true;
#endif
}

// Service thread: loading, then polling every ~1.5 s and carrying out requests (keep awake, sleep). It exits only with the process.
void syssvc_main()
{
    {
        FILE* f = std::fopen(SysProbeFile, "r");
        char name[96] = {};
        if (f != nullptr)
        {
            if (std::fgets(name, sizeof(name), f) == nullptr)
            {
                name[0] = 0;
            }
            std::fclose(f);
            std::remove(SysProbeFile);
            if (FILE* sf = std::fopen(SysSkipFile, "a"))
            {
                std::fprintf(sf, "%s\n", name[0] != 0 ? name : "?");
                std::fclose(sf);
            }
        }
        char skipped[96] = {};
        if (FILE* sf = std::fopen(SysSkipFile, "r"))
        {
            if (std::fgets(skipped, sizeof(skipped), sf) == nullptr)
            {
                skipped[0] = 0;
            }
            std::fclose(sf);
        }
        if (skipped[0] != 0)
        {
            skipped[std::strcspn(skipped, "\r\n")] = 0;
            std::lock_guard<std::mutex> lock(g_ss.m);
            g_ss.disabled = true;
            g_ss.error = std::string{ "a previous start did not return from " } + skipped;
            shim_log("syssvc: disabled: %s (delete %s to try again)", g_ss.error.c_str(), SysSkipFile);
            journal_event(ps5tm_tr("mr_ss_skipped", { std::string{ skipped } }));
            return;
        }
    }
    sleep(3); // let the manager come up
    SysApi api;
    std::string err;
    if (!ss_load(api, err))
    {
        std::lock_guard<std::mutex> lock(g_ss.m);
        g_ss.error = err;
        shim_log("syssvc: not available: %s", err.c_str());
        journal_event(ps5tm_tr("mr_ss_load_failed", { err }));
        return;
    }
    std::set<std::string> seen;
    auto const call = [&](char const* name, auto fn)
    {
        bool const first = seen.insert(name).second;
        if (first)
        {
            ss_mark(name);
        }
        g_ss_busy_name = name;
        g_ss_busy_since = std::time(nullptr);
        auto const r = fn();
        g_ss_busy_since = 0;
        g_ss_busy_name = nullptr;
        if (first)
        {
            ss_unmark();
        }
        return r;
    };
    {
        std::lock_guard<std::mutex> lock(g_ss.m);
        g_ss.ready = true;
    }
    shim_log("syssvc: ready");
    if (api.param_int != nullptr)
    {
        int v = -1;
        int const rc = call("sceSystemServiceParamGetInt", [&] { return api.param_int(1, &v); });
        std::lock_guard<std::mutex> lock(g_ss.m);
        g_ss.system_lang = rc == 0 ? v : -1;
        shim_log("syssvc: console language code: rc=0x%08x value=%d", static_cast<unsigned>(rc), v);
    }
    ps5tm_web_theme_apply(); // the console language is known now: the web interface dictionary and the failure translations for the default language
    std::time_t last_ka = 0;
    for (;;)
    {
        for (int i = 0; i < 15; ++i)
        {
            if (g_ss_want_poweroff.exchange(false) && api.power_off != nullptr)
            {
                sleep(1);
                int const rc = call("sceSystemServiceRequestPowerOff", [&] { return api.power_off(); });
                shim_log("syssvc: RequestPowerOff returned 0x%08x", static_cast<unsigned>(rc));
                std::lock_guard<std::mutex> lock(g_ss.m);
                g_ss.poweroff_called = true;
                g_ss.poweroff_rc = rc;
            }
            if (g_ss_want_ka.load() && api.tick != nullptr)
            {
                std::time_t const now = std::time(nullptr);
                if (last_ka == 0 || now - last_ka >= SysKeepAwakePeriodSeconds)
                {
                    last_ka = now;
                    int const rt = call("sceSystemServicePowerTick", [&] { return api.tick(); });
                    int const rr = api.reset_timer != nullptr ? call("sceShellCoreUtilResetAutoPowerDownTimer", [&] { return api.reset_timer(); }) : 0;
                    std::lock_guard<std::mutex> lock(g_ss.m);
                    g_ss.keepawake = true;
                    ++g_ss.ka_calls;
                    if (g_ss.ka_calls <= 2)
                    {
                        shim_log("syssvc: keep awake: PowerTick rc=0x%08x, ResetAutoPowerDownTimer rc=0x%08x", static_cast<unsigned>(rt), static_cast<unsigned>(rr));
                    }
                }
            }
            else
            {
                last_ka = 0;
                std::lock_guard<std::mutex> lock(g_ss.m);
                g_ss.keepawake = false;
            }
            usleep(100000);
        }
        int const app = call("sceLncUtilGetAppIdOfRunningBigApp", [&] { return api.get_app(); });
        char title[16] = {};
        if (app != -1)
        {
            char buf[16] = {};
            int const rc = call("sceLncUtilGetAppTitleId", [&] { return api.get_title(static_cast<std::uint32_t>(app), buf); });
            if (rc == 0)
            {
                buf[9] = 0;
                std::snprintf(title, sizeof(title), "%s", buf);
            }
        }
        unsigned char ui[256];
        std::memset(ui, 0xCC, sizeof(ui));
        int const rc = call("sceSystemServiceGetUIStatus", [&] { return api.get_ui(ui); });
        int changed = 0;
        for (int k = 0; k < 64; ++k)
        {
            changed += ui[k] != 0xCC ? 1 : 0;
        }
        std::lock_guard<std::mutex> lock(g_ss.m);
        if (g_ss.app_id != app)
        {
            shim_log("syssvc: app: id=0x%x title=%s", static_cast<unsigned>(app), title[0] != 0 ? title : "-");
        }
        g_ss.app_id = app;
        g_ss.title = title;
        if (rc == 0 && changed > 0)
        {
            std::memcpy(&g_ss.ui_status, ui, 4);
            g_ss.ui_valid = true;
        }
        g_ss.last_poll = std::time(nullptr);
    }
}

// Once a second from the monitor: a call that does not return within SysHangSeconds disables the service (the thread stays hung until the console restarts).
void syssvc_watchdog(std::time_t now)
{
    std::time_t const since = g_ss_busy_since.load();
    char const* const name = g_ss_busy_name.load();
    if (since == 0 || name == nullptr || now - since < SysHangSeconds)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(g_ss.m);
    if (g_ss.hung)
    {
        return;
    }
    g_ss.hung = true;
    g_ss.error = std::string{ "the call " } + name + " did not return";
    shim_log("syssvc: HUNG in %s for %d s: the system service features are off until the manager is restarted", name, static_cast<int>(now - since));
    journal_event(ps5tm_tr("mr_ss_hung", { std::string{ name } }));
}

GameState syssvc_game_state()
{
    SysSnapshot const s = ps5tm_syssvc_snapshot();
    if (!s.ready || s.hung || s.disabled || s.age < 0 || s.age > 10)
    {
        return GameState::Unavailable;
    }
    return s.game_state == 0 ? GameState::None : s.game_state == 1 ? GameState::OnScreen : s.game_state == 2 ? GameState::Minimised : GameState::Unknown;
}

} // namespace ps5tm_core

using namespace ps5tm_core;


SysSnapshot ps5tm_syssvc_snapshot()
{
    SysSnapshot r;
    std::time_t const now = std::time(nullptr);
    {
        std::lock_guard<std::mutex> lock(g_ss.m);
        r.ready = g_ss.ready;
        r.hung = g_ss.hung;
        r.disabled = g_ss.disabled;
        r.error = g_ss.error;
        r.app_id = g_ss.app_id;
        r.title = g_ss.title;
        r.ui_status = g_ss.ui_valid ? static_cast<int>(g_ss.ui_status) : -1;
        r.system_lang = g_ss.system_lang;
        r.keepawake = g_ss.keepawake;
        r.ka_calls = g_ss.ka_calls;
        r.age = g_ss.last_poll == 0 ? -1 : static_cast<long>(now - g_ss.last_poll);
        r.game_state = g_ss.app_id == -1 ? 0 : !g_ss.ui_valid ? 3 : (g_ss.ui_status != SysUiHome ? 1 : 2);
    }
    std::time_t const since = g_ss_busy_since.load();
    r.busy_for = since == 0 ? 0 : static_cast<long>(now - since);
    return r;
}

std::string ps5tm_syssvc_json()
{
    SysSnapshot const s = ps5tm_syssvc_snapshot();
    std::string err;
    for (char const c : s.error)
    {
        err += (c == '"' || c == '\\') ? '?' : c;
    }
    std::string title;
    for (char const c : s.title)
    {
        title += ps5tm_alnum(c) ? c : '?';
    }
    char const* const gs = s.game_state == 0 ? "none" : s.game_state == 1 ? "on_screen" : s.game_state == 2 ? "minimised" : "unknown";
    return std::string{ "{\"ready\":" } + (s.ready ? "true" : "false") + ",\"hung\":" + (s.hung ? "true" : "false") + ",\"disabled\":" + (s.disabled ? "true" : "false") +
           ",\"error\":\"" + err + "\",\"game_state\":\"" + gs + "\",\"app_id\":" + std::to_string(s.app_id) + ",\"title\":\"" + title + "\",\"ui_status\":" +
           std::to_string(s.ui_status) + ",\"system_lang\":" + std::to_string(s.system_lang) + ",\"keepawake\":" + (s.keepawake ? "1" : "0") + ",\"ka_calls\":" +
           std::to_string(s.ka_calls) + ",\"busy_for\":" + std::to_string(s.busy_for) + ",\"age\":" + std::to_string(s.age) + "}";
}

int ps5tm_system_language()
{
    std::lock_guard<std::mutex> lock(g_ss.m);
    return g_ss.system_lang;
}

// System sleep request (sceSystemServiceRequestPowerOff: the console enters sleep as from the menu). "" = the request was accepted.
std::string ps5tm_sleep_via_system()
{
    SysSnapshot const s = ps5tm_syssvc_snapshot();
    if (!s.ready || s.hung || s.disabled)
    {
        return ps5tm_tr("mr_ss_unavailable");
    }
    g_ss_want_poweroff = true;
    return "";
}
