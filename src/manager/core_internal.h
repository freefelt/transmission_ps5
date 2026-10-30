// Transmission for the PS5 (manager): the internal interface of the manager core modules (src/manager/*.cc).
// Everything here is shared between the modules; each module keeps its private helpers static.
//
// License: GPL-3.0-or-later (like Transmission).
#pragma once

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "ps5tm_cfg.h"

#include <pthread.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <cstring>
#include <ctime>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern "C" void shim_log(char const* fmt, ...);
extern "C" void ps5tm_notify_text(char const* text);
extern "C" volatile int ps5tm_daemon_finished;
extern "C" char const* ps5tm_ip(void);
extern "C" int ps5tm_current_ip(char* out, std::size_t size);
extern "C" void ps5tm_set_ip(char const* ip);
extern "C" unsigned ps5tm_resume_generation(void);

extern "C" int ps5tm_thread_snapshot(int pid, char* out, std::size_t size);


// Public functions defined by the modules (most are also declared in ps5tm_cfg.h; repeating a declaration is harmless)
Ps5tmDaemonInfo ps5tm_daemon_info();
Ps5tmSleepInfo ps5tm_sleep_info();
SysSnapshot ps5tm_syssvc_snapshot();
bool ps5tm_config_save();
bool ps5tm_game_pause_active();
bool ps5tm_quit_start();
bool ps5tm_sleep_cancel();
char const* ps5tm_autosleep_action_name(int index);
char const* ps5tm_game_pause_name(int index);
char const* ps5tm_sleep_method_name(int index);
char const* ps5tm_web_diskbar_name(int index);
char const* ps5tm_web_icons_name(int index);
char const* ps5tm_web_openfolder_name(int index);
char const* ps5tm_web_theme_name(int index);
extern "C" int ps5tm_cfg_notify_startup(void);
extern "C" int ps5tm_config_init(char const* path);
extern "C" void ps5tm_journal_add(char const* text, int shown);
extern "C" void* ps5tm_monitor_thread(void*);
int ps5tm_stop_torrents_under(std::vector<std::string> const& roots, std::string& detail);
int ps5tm_system_language();
long ps5tm_notifications_latest();
std::string ps5tm_autosleep_arm(int timeout_minutes);
std::string ps5tm_autosleep_set_timeout(int minutes);
std::string ps5tm_config_set(std::string const& key, std::string const& value);
std::string ps5tm_config_text();
std::string ps5tm_daemon_request_start();
std::string ps5tm_daemon_request_stop();
std::string ps5tm_language();
std::string ps5tm_process_list_text();
std::string ps5tm_rpc_probe_text();
std::string ps5tm_sleep_request(char const* requested_by);
std::string ps5tm_sleep_via_system();
std::string ps5tm_sleep_warnings_hidden();
std::string ps5tm_syssvc_json();
std::string ps5tm_thread_snapshot_text();
std::vector<Ps5tmNotification> ps5tm_notifications_snapshot();
std::vector<std::string> ps5tm_folders_get();
void ps5tm_announce(bool enabled, char const* text);
void ps5tm_autosleep_disarm();
void ps5tm_daemon_port_changed();
void ps5tm_folders_set(std::vector<std::string> const& folders);
void ps5tm_maintenance_run();
void ps5tm_notifications_clear();
void ps5tm_web_theme_apply();

namespace ps5tm_core
{



#ifndef PS5TM_POLL_SECONDS
#define PS5TM_POLL_SECONDS 5
#endif
#ifndef PS5TM_SPACE_CHECK_SECONDS
#define PS5TM_SPACE_CHECK_SECONDS 60
#endif

inline int rpc_port()
{
    return g_cfg.daemon_port.load(); // Transmission web interface port (the daemon_port setting)
}

constexpr int SpaceCheckSeconds = PS5TM_SPACE_CHECK_SECONDS;

constexpr int QuietSeconds = 10;

constexpr int ResumeGapSeconds = 8; // a sleep of this many seconds or more: the process was frozen

// The "low space" thresholds come from the settings (1 GB = 10^9 bytes). The critical threshold cannot be higher than the warning one.
inline double warn_bytes()
{
    return static_cast<double>(g_cfg.low_disk_warn_gb.load()) * 1e9;
}

inline double critical_bytes()
{
    int const critical = g_cfg.low_disk_critical_gb.load();
    int const warn = g_cfg.low_disk_warn_gb.load();
    return static_cast<double>(critical < warn ? critical : warn) * 1e9;
}

constexpr std::time_t TrackerErrorCooldown = 30 * 60;

constexpr std::size_t MaxResponse = 4U * 1024U * 1024U;

std::string trim(std::string s);
bool write_text_file(char const* path, std::string const& text);
struct Json
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string str;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    Json const* get(char const* key) const
    {
        for (auto const& m : members)
        {
            if (m.first == key)
            {
                return &m.second;
            }
        }
        return nullptr;
    }
};

class Parser
{
public:
    Parser(char const* begin, char const* end)
        : p_{ begin }
        , end_{ end }
    {
    }

    bool parse(Json& out)
    {
        skip();
        return value(out, 0);
    }

private:
    char const* p_;
    char const* end_;

    void skip()
    {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\n' || *p_ == '\r' || *p_ == '\t'))
        {
            ++p_;
        }
    }

    bool literal(char const* text)
    {
        auto const len = std::strlen(text);
        if (static_cast<std::size_t>(end_ - p_) < len || std::strncmp(p_, text, len) != 0)
        {
            return false;
        }
        p_ += len;
        return true;
    }

    static void append_utf8(std::string& out, std::uint32_t cp)
    {
        if (cp < 0x80)
        {
            out += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(std::uint32_t& out)
    {
        if (end_ - p_ < 4)
        {
            return false;
        }
        out = 0;
        for (int i = 0; i < 4; ++i)
        {
            char const c = *p_++;
            out <<= 4;
            if (c >= '0' && c <= '9')
            {
                out |= static_cast<std::uint32_t>(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                out |= static_cast<std::uint32_t>(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                out |= static_cast<std::uint32_t>(c - 'A' + 10);
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    bool string(std::string& out)
    {
        if (p_ >= end_ || *p_ != '"')
        {
            return false;
        }
        ++p_;
        out.clear();
        while (p_ < end_)
        {
            char const c = *p_++;
            if (c == '"')
            {
                return true;
            }
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (p_ >= end_)
            {
                return false;
            }
            char const e = *p_++;
            switch (e)
            {
            case '"':
            case '\\':
            case '/':
                out += e;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
            {
                std::uint32_t cp = 0;
                if (!hex4(cp))
                {
                    return false;
                }
                if (cp >= 0xD800 && cp <= 0xDBFF && end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u')
                {
                    p_ += 2;
                    std::uint32_t low = 0;
                    if (!hex4(low) || low < 0xDC00 || low > 0xDFFF)
                    {
                        return false;
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                }
                append_utf8(out, cp);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool number(Json& out)
    {
        char const* start = p_;
        while (p_ < end_ && (std::strchr("+-.eE", *p_) != nullptr || (*p_ >= '0' && *p_ <= '9')))
        {
            ++p_;
        }
        if (p_ == start)
        {
            return false;
        }
        out.type = Json::Type::Number;
        out.number = std::strtod(std::string{ start, p_ }.c_str(), nullptr);
        return true;
    }

    bool array(Json& out, int depth)
    {
        ++p_; // '['
        out.type = Json::Type::Array;
        skip();
        if (p_ < end_ && *p_ == ']')
        {
            ++p_;
            return true;
        }
        for (;;)
        {
            Json item;
            skip();
            if (!value(item, depth + 1))
            {
                return false;
            }
            out.items.push_back(std::move(item));
            skip();
            if (p_ >= end_)
            {
                return false;
            }
            if (*p_ == ',')
            {
                ++p_;
                continue;
            }
            if (*p_ == ']')
            {
                ++p_;
                return true;
            }
            return false;
        }
    }

    bool object(Json& out, int depth)
    {
        ++p_; // '{'
        out.type = Json::Type::Object;
        skip();
        if (p_ < end_ && *p_ == '}')
        {
            ++p_;
            return true;
        }
        for (;;)
        {
            std::string key;
            skip();
            if (!string(key))
            {
                return false;
            }
            skip();
            if (p_ >= end_ || *p_ != ':')
            {
                return false;
            }
            ++p_;
            skip();
            Json item;
            if (!value(item, depth + 1))
            {
                return false;
            }
            out.members.emplace_back(std::move(key), std::move(item));
            skip();
            if (p_ >= end_)
            {
                return false;
            }
            if (*p_ == ',')
            {
                ++p_;
                continue;
            }
            if (*p_ == '}')
            {
                ++p_;
                return true;
            }
            return false;
        }
    }

    bool value(Json& out, int depth)
    {
        if (depth > 32 || p_ >= end_)
        {
            return false;
        }
        switch (*p_)
        {
        case '{':
            return object(out, depth);
        case '[':
            return array(out, depth);
        case '"':
            out.type = Json::Type::String;
            return string(out.str);
        case 't':
            out.type = Json::Type::Bool;
            out.boolean = true;
            return literal("true");
        case 'f':
            out.type = Json::Type::Bool;
            out.boolean = false;
            return literal("false");
        case 'n':
            out.type = Json::Type::Null;
            return literal("null");
        default:
            return number(out);
        }
    }
};

struct HttpResult
{
    int status = 0;
    std::string session_id;
    std::string body;
};

bool send_all(int fd, std::string const& data);
struct Torrent
{
    int status = 0; // 0 stopped, 1-2 checking, 3 queued, 4 downloading, 5 seeding queued, 6 seeding
    double left = 0; // needed bytes left to download
    int error = 0; // 0 none, 1 tracker warning, 2 tracker error, 3 local error
    std::string name;
    std::string error_string;
    std::string download_dir;
    double rate_down = 0; // bytes/s
    double rate_up = 0;
};

bool rpc_call(std::string const& payload, Json& root, std::string& session_id);
bool fetch(std::map<int, Torrent>& out, std::string& session_id);
double free_bytes(std::string const& dir, std::string& session_id);
std::string format_size(double bytes);
struct Known
{
    Torrent t;
    std::string notified_error; // "code:text" of the last error already reported
    std::time_t last_tracker_notice = 0;
    bool downloading = false; // the torrent was really downloading (it was in downloading status with data left)
};

std::string clip(std::string s, std::size_t max_bytes);
struct JournalEntry
{
    long id;
    long time;
    int shown;
    char text[340];
};

inline constexpr int JournalSize = 500;

inline constexpr char const* JournalFile = "/data/Transmission/logs/notifications.log";

void journal_add(char const* text, int shown);
void journal_load();
void announce(bool enabled, std::string const& text);
std::string duration_text(long s);
void journal_event(std::string const& text);
void journal_prune(std::time_t now);
inline constexpr char const* LogsDir = "/data/Transmission/logs";

inline constexpr char const* LogsStampFile = "/data/Transmission/logs/.rotated"; // date (UTC) of the last rotation

inline constexpr char const* DebugLogBases[] = { "shim", "daemon", "console", "transmission", "icon" };

void logs_rotate_if_needed(std::time_t now);
void logs_prune(std::time_t now);
inline constexpr char const* WebDir = "/data/Transmission/transmission/web";

inline constexpr char const* WebDiskbarFile = "/data/Transmission/transmission/web_diskbar";

inline constexpr char const* WebOpenFolderFile = "/data/Transmission/transmission/web_openfolder";

void web_theme_apply();
enum DaemonState
{
    DaemonStopped = 0,
    DaemonStarting,
    DaemonRunning,
    DaemonStopping,
    DaemonNotResponding,
    DaemonRestarting
};

struct DaemonShared
{
    std::atomic<int> state{ DaemonStopped };
    std::atomic<int> pid{ 0 };
    std::atomic<long> since{ 0 };
    std::atomic<bool> busy{ false }; // a start, stop or restart operation is in progress
    std::atomic<bool> desired_running{ true };
    std::atomic<unsigned> epoch{ 0 }; // grows on every daemon start: resets the monitor state
    std::atomic<long> launched_at{ 0 };
    std::atomic<long> unhealthy_since{ 0 }; // since when the daemon has not been responding (while the network works)
    std::atomic<long> last_restart{ 0 };
    std::atomic<long> last_launch_attempt{ 0 };
    std::atomic<int> launched_port{ 0 }; // port the daemon was started with (or adopted on)
    std::atomic<long> launch_not_before{ 0 }; // after wake-up: the file system and network are recovering
    std::atomic<int> launch_failures{ 0 };
    std::atomic<bool> hang_journaled{ false }; // the notification about the current hang is already in the bell (on restart)
    std::atomic<bool> stuck_reported{ false }; // the failure to terminate the process is already recorded
    std::atomic<bool> kill_failed{ false }; // a forced restart could not terminate the old process (it is stuck in the system)
    std::atomic<long> io_blocked_at{ 0 }; // when the daemon last reported "io-watch: blocked" (waiting for the disk)
    std::atomic<long> io_done_at{ 0 }; // when it last reported "io-watch: done"
    std::atomic<bool> net_pending{ false }; // after a network change, a check is running whether the daemon answers on the new address
    std::atomic<long> net_event_at{ 0 };
    std::atomic<long> net_probe_at{ 0 };
    std::atomic<int> net_reason{ 0 }; // 1 network is back, 2 IP changed, 3 the console left rest mode
    std::atomic<int> net_attempts{ 0 }; // restarts for this event (no more than one)
};

extern DaemonShared g_d;
enum SleepPhase
{
    SleepIdle = 0,
    SleepCountdown,
    SleepStopping,
    SleepEntering
};

enum SleepReason
{
    ReasonManual = 0,
    ReasonAllDone,
    ReasonTimeout
};

struct SleepShared
{
    std::atomic<int> phase{ SleepIdle };
    std::atomic<int> reason{ ReasonManual };
    std::atomic<long> deadline{ 0 }; // end of the countdown
    std::atomic<bool> cancel{ false };
    // after the sleep call: do not start the daemon until the console wakes up
    std::atomic<bool> hold{ false };
    std::atomic<long> hold_since{ 0 };
    std::atomic<bool> hold_net_lost{ false };
    // "after all downloads finish": not persisted, lives only while the manager runs
    std::atomic<bool> armed{ false };
    std::atomic<int> timeout_minutes{ 240 };
    std::atomic<long> timeout_at{ 0 };
    std::atomic<long> armed_at{ 0 };
    std::atomic<long> idle_since{ 0 };
    // which downloads are tracked and whether they actually finished (rather than being paused or removed)
    std::atomic<bool> track_reset{ false };
    std::atomic<int> tracked{ 0 };
    std::atomic<bool> all_completed{ false };
};

extern SleepShared g_sl;
extern char g_sleep_by[64];
extern pthread_mutex_t g_sleep_by_mutex;
inline constexpr char const* DaemonName = "transmission_d.elf";

inline constexpr char const* DaemonLog = "/data/Transmission/logs/daemon.log";

int daemon_pid();
void notify_startup_text(std::string const& text);
struct TorrentLite
{
    int id = 0;
    std::string hash;
    int status = 0;
};

bool fetch_lite(std::vector<TorrentLite>& out, std::string& session_id);
std::string ids_array(std::vector<int> const& ids);
long file_size_of(char const* path);
std::string daemon_stop_graceful(int& paused);
void daemon_manager_start();
void net_event(int reason);
void supervise(bool have_ip);
void daemon_note_rpc(bool ok, bool have_ip);
void sleep_tick(long now, bool have_ip, bool resumed);
void diag_dump(char const* why);
void check_guard_events();
inline constexpr char const* GamePausedFile = "/data/Transmission/game_paused.txt";

enum class GameState
{
    Unavailable,
    None,
    OnScreen,
    Minimised,
    Unknown
};

inline constexpr char const* SysProbeFile = "/data/Transmission/syssvc_probe.txt";

inline constexpr char const* SysSkipFile = "/data/Transmission/syssvc_skip.txt";

inline constexpr int SysHangSeconds = 30;

extern std::atomic<bool> g_ss_want_ka;
void syssvc_main();
void syssvc_watchdog(std::time_t now);
GameState syssvc_game_state();
void game_pause_tick(std::time_t now);
void keep_awake_tick(std::time_t);

} // namespace ps5tm_core
