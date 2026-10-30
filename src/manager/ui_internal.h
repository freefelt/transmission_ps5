// Transmission for the PS5 (manager): the internal interface of the settings server modules (src/manager/*.cc).
// Everything here is shared between the modules; each module keeps its private helpers static.
//
// License: GPL-3.0-or-later (like Transmission).
#pragma once

#include "ps5tm_cfg.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#ifndef __linux__
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#else
#include <sys/mount.h>
#endif
#include <sys/time.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <dirent.h>
#include <exception>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C"
{
void shim_log(char const* fmt, ...);
void ps5tm_notify_text(char const* text);
char const* ps5tm_ip(void);
int ps5tm_current_ip(char* out, std::size_t size);
unsigned ps5tm_resume_generation(void);
char const* ps5tm_version(void);
long ps5tm_uptime_seconds(void);
int ps5tm_process_running(char const* name);
extern volatile int ps5tm_daemon_finished;

// Lookup of functions in the loaded modules (from the SDK). The rest of the platform is resolved through them.
int kernel_dynlib_handle(int pid, char const* basename, std::uint32_t* handle);
std::intptr_t kernel_dynlib_resolve(int pid, std::uint32_t handle, char const* nid);
}


namespace ps5tm_ui
{


inline constexpr char const* TitleId = "TRNS00001";
inline constexpr char const* IconDir = "/user/app/TRNS00001";
inline constexpr char const* IconSceSys = "/user/app/TRNS00001/sce_sys";
inline constexpr char const* IconParam = "/user/app/TRNS00001/sce_sys/param.json";
inline constexpr char const* IconFile = "/user/app/TRNS00001/sce_sys/icon0.png";
inline constexpr char const* IconHelperFile = "/data/Transmission/icon/transmission_i.elf"; // helper (built into the manager)
inline constexpr char const* IconHelperName = "transmission_i.elf";
inline constexpr char const* IconRequestFile = "/data/Transmission/icon/request";
inline constexpr char const* IconResultFile = "/data/Transmission/icon/result";
inline constexpr char const* IconRegisteredFile = "/data/Transmission/icon/registered"; // exists only after a successful registration; inside, the param.json it was registered with
inline constexpr char const* IconAttemptFile = "/data/Transmission/icon/attempt"; // exists while the attempt has not finished with an answer from the helper
inline constexpr char const* IconLogFile = "/data/Transmission/logs/icon.log";
inline constexpr std::size_t MaxHeader = 16 * 1024;
inline constexpr std::size_t MaxBody = 32 * 1024;

std::string jstr(std::string const& s);
std::string jbool(bool b);
std::string hex8(int value);
bool parse_flat_json(std::string const& text, std::map<std::string, std::string>& out);
struct KeyInfo
{
    char const* name;
    bool editable_in_ui; // settings_server is changed only in the file (otherwise the page could be disabled from itself)
};

inline constexpr KeyInfo Keys[] = {
    { "wake_delay_seconds", true },
    { "poll_interval_seconds", true },
    { "notify_startup", true },
    { "notify_torrent_started", true },
    { "notify_torrent_finished", true },
    { "notify_torrent_error", true },
    { "notify_tracker_error", true },
    { "notify_low_disk_space", true },
    { "notify_game_pause", true },
    { "settings_server", false },
    { "settings_port", true },
    { "install_icon", true },
    { "sleep_notify_before", true },
    { "sleep_countdown_seconds", true },
    { "daemon_restart_after_seconds", true },
    { "daemon_network_restart_seconds", true },
    { "daemon_stop_grace_seconds", true },
    { "daemon_port", true },
    { "low_disk_warn_gb", true },
    { "low_disk_critical_gb", true },
    { "autosleep_timeout_minutes", true },
    { "notifications_keep_days", true },
    { "logs_keep_days", true },
    { "web_theme", true },
    { "web_diskbar", true },
    { "web_openfolder", true },
    { "game_pause", true },
    { "autosleep_action", true },
    { "sleep_method", true },
    { "keep_awake", true },
    { "language", true },
    { "web_icons", true },
    { "sleep_warnings_hidden", true },
};

std::string current_value(std::string const& key);
std::string raw_value(std::string const& key);
inline constexpr char const* NidOpenEventFlag = "1vDaenmJtyA"; // sceKernelOpenEventFlag

inline constexpr char const* NidNotifySuspendStart = "da6-yq0GbLg"; // sceKernelNotifySystemSuspendStart

inline constexpr char const* NidSetEventFlag = "IOnSvHzqu6A"; // sceKernelSetEventFlag

inline constexpr char const* NidCloseEventFlag = "s9-RaxukuzQ"; // sceKernelCloseEventFlag

struct ModuleList
{
    char const* const* names;
    std::size_t count;
};

inline constexpr char const* KernelModuleNames[] = { "libkernel_web.sprx", "libkernel.sprx", "libkernel_sys.sprx" };

inline constexpr ModuleList KernelModules{ KernelModuleNames, 3 };

std::string run_isolated(std::function<std::string()> job, int timeout_seconds, char const* what);
bool icon_installed();
extern std::atomic<bool> g_icon_busy;
extern char g_icon_error[700];
extern pthread_mutex_t g_icon_mutex;
bool icon_start(std::string const& action, std::string const& why, bool persist, bool automatic = false);
std::string enter_rest_mode_impl();
struct Request
{
    std::string method;
    std::string path;
    std::string content_type;
    std::string body;
};

bool read_request(int fd, Request& req);
void send_response(int fd, int code, char const* status, char const* content_type, std::string const& body);
void send_json(int fd, int code, char const* status, std::string const& json);
std::string state_json();
bool is_json(Request const& req);
void handle_config(int fd, Request const& req);
void handle_icon(int fd, Request const& req);
void handle_sleep(int fd, Request const& req, std::string const& client);
void handle_autosleep(int fd, Request const& req);
std::string read_file_tail(char const* path, std::size_t max_bytes);
void handle_daemon(int fd, Request const& req);
struct LangData
{
    std::string name; // value of the first LanguageName entry
    std::map<std::string, std::string> map; // key -> value
    std::size_t known = 0; // how many keys are also in the English table (for files)
};

std::string lang_default();
std::string lang_validate_choice(std::string const& value);
struct LangResolved
{
    std::string id; // ru, en or file:...
    LangData file; // file data if id = file:...
    std::string fallback; // not empty: the saved language turned out to be unavailable (its value)
    std::string reason; // error code
};

std::string build_settings_page(char const* source);
struct FsInfo
{
    std::string name;
    std::string device;
    bool bad = false;
};

FsInfo volume_fs(std::string const& root);
struct MountInfo
{
    std::string on;
    std::string type;
    std::string from;
};

std::string mounts_report_text();
std::vector<std::string> unmount_plan(std::string const& root);
struct UnmountPlan
{
    std::vector<std::string> aliases; // nullfs links to the disk (BindMount), to be unmounted first
    std::vector<std::string> parts; // the mounts of the disk itself (all its partitions), in mount order
};
UnmountPlan unmount_plan_parts(std::string const& root);
bool usb_system_eject_available(); // usb_eject.cc: whether the system can be asked to disconnect a drive
int usb_system_eject(std::string const& path, int& native_status); // 0 or an errno (ENOTSUP: not available)
int unmount_one(std::string const& path, bool force);
std::string holders_json(std::vector<std::string> const& mounts);
std::string errno_text(int e);
std::string disks_report_text();
inline constexpr std::size_t FolderLimit = 8;

extern pthread_mutex_t g_folders_edit_mutex;
struct FolderLock
{
    FolderLock() { pthread_mutex_lock(&g_folders_edit_mutex); }
    ~FolderLock() { pthread_mutex_unlock(&g_folders_edit_mutex); }
};

std::string normalize_folder(std::string in, std::vector<std::string>& comps);
int folder_kind(std::vector<std::string> const& c0, std::string& root);
std::string folder_add(std::string const& path_in, bool confirm, bool& need_confirm, std::string& normalized, std::string& reason);
std::string folder_remove(std::string const& path_in);
std::string folders_json();
int folder_unmount(std::string const& path_in, bool force, std::string& message, std::string& holders);
std::string fs_error(std::string const& text);
std::string fs_list_json(std::string const in);
std::string fs_mkdir_json(std::string const in, std::string name);
void handle_fs_post(int fd, Request const& req);
void handle_folders_post(int fd, Request const& req);
int make_listener_scan(int wanted, int preferred, int& actual, int& error);
bool probe_listener(int port);
void http_selftest_in_background(int port);

} // namespace ps5tm_ui
