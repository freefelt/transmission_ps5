// Transmission for PS5 (manager): shared settings and statistics, daemon state.
//
// Settings are stored in /data/Transmission/config.cfg. Fields are atomic because the monitor reads them
// (its own thread) while the settings server changes them (another thread): changes apply on the fly.
//
// License: GPL-3.0-or-later (same as Transmission).
#pragma once

#include <atomic>
#include <initializer_list>
#include <string>
#include <vector>

// Transmission version built into this build (the build script passes it as a define)
#ifndef PS5TM_TR_VERSION
#define PS5TM_TR_VERSION "unknown"
#endif

#ifndef PS5TM_POLL_SECONDS
#define PS5TM_POLL_SECONDS 5
#endif

struct Ps5tmConfig
{
    std::atomic<int> wake_delay_seconds{ 5 }; // pause after the console wakes from sleep (settings page socket)
    std::atomic<int> poll_interval_seconds{ PS5TM_POLL_SECONDS }; // daemon polling interval

    std::atomic<bool> notify_startup{ true }; // Transmission start, stop and restart, stop before sleep
    std::atomic<bool> notify_torrent_started{ true };
    std::atomic<bool> notify_torrent_finished{ true };
    std::atomic<bool> notify_torrent_error{ true }; // local errors: no space, permissions, disk
    std::atomic<bool> notify_tracker_error{ true };
    std::atomic<bool> notify_low_disk_space{ true };
    std::atomic<bool> notify_game_pause{ true }; // "game on screen: downloads paused…" / "downloads resumed…" messages to the console (they always go to the journal)
    std::atomic<int> low_disk_warn_gb{ 5 }; // "low space": less than this many GB free (or less than what is left to download)
    std::atomic<int> low_disk_critical_gb{ 1 }; // "disk almost full": less than this many GB (0 = do not report)

    std::atomic<bool> settings_server{ true }; // settings page in the browser
    std::atomic<int> settings_port{ 9092 };
    std::atomic<bool> install_icon{ true }; // icon on the "Media" tab (currently disabled in the build)
    std::atomic<bool> sleep_notify_before{ true }; // notification and delay before sleep (false = immediately)
    std::atomic<int> sleep_countdown_seconds{ 10 }; // delay before sleep, seconds (can be cancelled)
    std::atomic<int> autosleep_timeout_minutes{ 240 }; // auto-sleep timeout: sleep anyway this many minutes after arming

    std::atomic<int> daemon_restart_after_seconds{ 120 }; // 0 = do not restart a hung daemon
    std::atomic<int> daemon_network_restart_seconds{ 15 }; // 0 = do not restart a daemon that does not answer on the new address after a network change
    std::atomic<int> daemon_stop_grace_seconds{ 20 }; // how long to wait for a graceful daemon shutdown
    std::atomic<int> notifications_keep_days{ 7 }; // how many days to keep notifications (notification bell)
    std::atomic<int> logs_keep_days{ 2 }; // how many days to keep debug logs (logs/)
    std::atomic<bool> restart_default_migrated{ false }; // the old default (15 s) for waiting for the daemon response was replaced by 120 s
    std::atomic<bool> folders_migrated{ false }; // download path from the Transmission settings has already been migrated to the "download folders" list
    std::atomic<bool> keep_awake{ false }; // while downloads are running, keep the console from sleeping on its own timer (via the helper)
    std::atomic<int> sleep_method{ 0 }; // how to enter rest mode: 0 system request via the helper (sceSystemServiceRequestPowerOff), 1 direct (system state flag)
    std::atomic<int> autosleep_action{ 0 }; // action when all downloads finish: 0 enter rest mode, 1 power off the console
    std::atomic<int> game_pause{ 0 }; // pause downloads during a game: 0 never, 1 only when the game is on screen, 2 while a game is running (including minimized)
    std::atomic<int> web_openfolder{ 0 }; // "Open folder" item in the download menu on the Transmission page: 0 do not show, 1 BFpilot, 2 PS5 Web File Manager
    std::atomic<int> web_diskbar{ 1 }; // "disk status" panel on the Transmission page: 0 do not show, 1 disks of all torrents, 2 only disks of running torrents
    std::atomic<int> web_icons{ 0 }; // icon set of the Transmission page: 0 color, 1 monochrome
    std::atomic<int> web_theme{ 0 }; // Transmission web interface theme: 0 dark, 1 light
    std::atomic<int> daemon_port{ 9091 }; // Transmission web interface port (the daemon restarts when it changes)
};

// What the monitor saw at the last poll (for the settings page)
struct Ps5tmStats
{
    std::atomic<int> total{ 0 };
    std::atomic<int> downloading{ 0 };
    std::atomic<int> seeding{ 0 };
    std::atomic<int> paused{ 0 };
    std::atomic<long> last_poll{ 0 }; // time(), 0 = not polled yet
    std::atomic<bool> rpc_ok{ false };
    std::atomic<bool> monitor_active{ false };
    std::atomic<long long> down_bps{ 0 }; // total download rate of all torrents, bytes/s
    std::atomic<long long> up_bps{ 0 };   // total seeding rate, bytes/s
};

extern Ps5tmConfig g_cfg; // defined in ps5tm_notify.cc
extern Ps5tmStats g_stats;

// config.cfg text for the current values (with comments) and writing it to the file.
std::string ps5tm_config_text();
bool ps5tm_config_save();

// Apply one setting by name. "" = success, otherwise the error text (the value is not changed).
std::string ps5tm_config_set(std::string const& key, std::string const& value);

// Diagnostics: what the daemon answers on loopback right now and a thread snapshot of the daemon process.
std::string ps5tm_rpc_probe_text();
std::string ps5tm_thread_snapshot_text();
std::string ps5tm_process_list_text(); // all console processes and port state: for sorting out "the daemon does not start"

// From the wrapper (ps5tm_main_v2.c, manager role)
extern "C" int ps5tm_launch_elf(char const* path, std::size_t expected_size, char* err, std::size_t errsize);
extern "C" std::size_t ps5tm_daemon_size(void);
extern "C" std::size_t ps5tm_icon_helper_size(void);
extern "C" long ps5tm_process_rss_kib(int pid); // process resident memory, KiB (-1 = failed)
extern "C" int ps5tm_process_list(char* out, std::size_t size);
extern "C" int ps5tm_find_pid(char const* name);
extern "C" int ps5tm_disk_holders(char const* const* roots, int nroots, char* out, std::size_t outsize); // who holds the disk: lines "pid TAB name TAB count TAB example path"
extern "C" int ps5tm_resolve_alias(char const* in, char* out, std::size_t size);
extern "C" int ps5tm_volume_info(char const* root, char* fstype, std::size_t fssize, char* device, std::size_t devsize); // file system and device of the volume
extern "C" int ps5tm_disk_kind_of_root(char const* root); // 1 = M.2 SSD (/dev/ssd*), 2 = USB (/dev/es*.crypt, /dev/da*)
extern "C" int ps5tm_fs_needs_fix(char const* fstype); // exFAT and FAT32: no sparse files, the "USB fix" is needed // path through a nullfs alias (/data/ext1) -> real (/mnt/ext1); 1 = the path changed
int ps5tm_stop_torrents_under(std::vector<std::string> const& roots, std::string& detail); // stop torrents located on these mount points and wait; number stopped (-1 = no response from the daemon)
extern "C" int ps5tm_daemon_blob_changed(void);
extern "C" int ps5tm_port_in_use(int port);
extern "C" std::size_t ps5tm_log_ring_copy(char* out, std::size_t size);

// Transmission daemon (transmission_d.elf): state and actions. Start and stop are performed
// in the background, the page polls the state.
struct Ps5tmDaemonInfo
{
    std::string state; // stopped | starting | running | stopping | not_responding | restarting
    int pid = 0;
    long since_age = 0; // how many seconds in this state
    bool desired_running = true; // false after a manual stop: the manager does not start the daemon on its own
    bool busy = false; // a start, stop or restart is in progress
    std::string last_error; // why the last start failed ("" = all good)
    long unresponsive_for = -1; // how many seconds the daemon has not answered (only in the not_responding state), otherwise -1
    long restart_in = -1; // in how many seconds the forced restart will happen (-1 = none)
    bool stuck = false; // the process could not be terminated (most likely waiting on a disk)
    long mem_kib = -1; // resident memory of the daemon process, KiB (-1 = unknown)
};
Ps5tmDaemonInfo ps5tm_daemon_info();
std::string ps5tm_daemon_request_stop(); // "" = accepted, otherwise the reason for refusal
std::string ps5tm_daemon_request_start();

// Sleep (button and "after all downloads finish"). Before sleep Transmission is stopped, as with the button.
// The "after all downloads finish" task lives only while the manager is running and is not saved to the file.
struct Ps5tmSleepInfo
{
    std::string phase; // idle | countdown | stopping | entering
    long remaining = 0; // seconds until sleep (countdown only)
    bool armed = false; // "after all downloads finish" is armed
    bool can_arm = false; // there are active downloads: can be armed
    int downloading = 0;
    int timeout_minutes = 0;
    long timeout_remaining = 0; // seconds until the forced sleep
    bool manual = false; // the "sleep now" button is currently active (not auto-sleep): always a real sleep, even when the action is "power off"
    bool holding = false; // waiting for wake-up, the daemon does not come up
};
Ps5tmSleepInfo ps5tm_sleep_info();
std::string ps5tm_sleep_request(char const* requested_by); // "" = accepted, otherwise the reason for refusal
bool ps5tm_sleep_cancel(); // true = there was a countdown and it was cancelled
std::string ps5tm_autosleep_arm(int timeout_minutes);
void ps5tm_autosleep_disarm();
std::string ps5tm_autosleep_set_timeout(int minutes);

// From ps5tm_ui.cc: rest mode system call (in a separate thread with a time limit). "" = invoked.
std::string ps5tm_enter_rest_mode();

// Actual settings page port: the first free one starting from settings_port (0 = not listening yet)
extern std::atomic<int> g_settings_port_actual;

// Notification journal (notification bell on the page): all manager notifications, including those disabled in the settings
// (shown = false: not sent to the console). Stored in logs/notifications.log and survives a restart.
struct Ps5tmNotification
{
    long id = 0;
    long time = 0; // unix time
    bool shown = true; // true: sent to the console
    std::string text;
    int kind = 1; // 1 sent, 0 disabled in the settings (not sent to the console), 2 event (to the journal, not a notification), 3 system (sent; a message of the payload itself), 4 system, disabled in the settings
};
std::vector<Ps5tmNotification> ps5tm_notifications_snapshot(); // oldest to newest
long ps5tm_notifications_latest(); // id of the latest notification (0 = none)
void ps5tm_notifications_clear();
void ps5tm_announce(bool enabled, char const* text); // enabled: send to the console; it goes to the journal in any case
extern "C" void ps5tm_journal_add(char const* text, int shown); // from the wrapper (notify)

// Transmission port changed: the daemon restarts (gracefully, resuming torrents)
void ps5tm_daemon_port_changed();

// Log retention: deletes notifications older than notifications_keep_days, once a day splits debug logs by date
// and deletes files older than logs_keep_days. Called at startup, every 10 minutes and after a settings change.
void ps5tm_maintenance_run();

// Transmission web interface theme (page on the daemon port): puts ps5-theme.css into the web/ folder. Called at startup and after
// a change of the web_theme setting; the daemon does the same on its own start (reads web_theme from the transmission/ folder).
void ps5tm_web_theme_apply();
char const* ps5tm_web_theme_name(int index);
char const* ps5tm_web_diskbar_name(int index); // off, all, active
char const* ps5tm_web_openfolder_name(int index); // off, bfpilot, webfm
char const* ps5tm_game_pause_name(int index); // off, screen, running
inline bool ps5tm_alnum(char c) // Latin letter or digit (without isalnum from the system library: we do not need a new import)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
std::string ps5tm_tr(char const* key, std::initializer_list<std::string> args = {}); // message in the selected language (key from locales/*.strings; {1}, {2}... are substituted from args)
std::string ps5tm_web_lang_js(); // ps5-lang.js: translation dictionary of the Transmission web interface for the selected language
std::string ps5tm_guard_messages_text(); // guard_messages.txt: translations of daemon refusals (read by the daemon)
std::string ps5tm_language(); // saved interface language: "", "ru", "en" or "file:<name>.strings" (validated when the settings page opens)
char const* ps5tm_autosleep_action_name(int index); // sleep, poweroff
char const* ps5tm_sleep_method_name(int index); // native, direct
bool ps5tm_game_pause_active(); // downloads are currently paused because of a game (for the status line on the page)
struct SysSnapshot
{
    bool ready = false; // system service libraries are loaded
    bool hung = false; // the call did not return within 30 s: the service is off until it returns
    bool disabled = false; // the previous start crashed or hung inside the function
    std::string error;
    int game_state = 0; // 0 no game, 1 on screen, 2 minimized, 3 unknown
    int app_id = -1;
    std::string title;
    int ui_status = -1; // "UI state" number (on 11.20: 0x20010 home screen, 0x20036 game on screen); -1 no data
    int system_lang = -1; // console language code (Russian 8); -1 unknown
    bool keepawake = false;
    long ka_calls = 0;
    long busy_for = 0; // how many seconds the current call has been running (0 none)
    long age = -1; // how many seconds ago the last poll was (-1 none yet)
};
SysSnapshot ps5tm_syssvc_snapshot();
std::string ps5tm_syssvc_json(); // same for /api/state
std::string ps5tm_power_json(); // the power state of the console (rest mode, shutdown) for /api/state
int ps5tm_system_language(); // console language code (Russian 8); -1 while unknown
std::string ps5tm_sleep_via_system(); // system sleep request: "" = request accepted, otherwise the reason
extern "C" int ps5tm_power_off_syscall(void); // power off the console by system call (on the host only a flag in a file)
extern "C" void ps5tm_exit_now(void); // terminate the manager process immediately
bool ps5tm_quit_start(); // full quit: gracefully stop the daemon and the helper and terminate the manager
char const* ps5tm_web_icons_name(int index);

// Version at which the user hid the warnings of the "Rest mode" section ("" = show). After an update the version is different
// and the warnings are shown again.
std::string ps5tm_sleep_warnings_hidden();

// "Download folders": new torrents can be added (and moved) only into them. Stored in config.cfg as download_folder=/path lines.
std::vector<std::string> ps5tm_folders_get();
void ps5tm_folders_set(std::vector<std::string> const& folders);
