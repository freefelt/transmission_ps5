// Transmission for the PS5: settings (config.cfg), the global state and the accessors of the settings.
//
// License: GPL-3.0-or-later (like Transmission).

#include "core_internal.h"

Ps5tmConfig g_cfg;
std::string g_language; // interface language (see ps5tm_language)
pthread_mutex_t g_language_mutex = PTHREAD_MUTEX_INITIALIZER;
Ps5tmStats g_stats;

namespace ps5tm_core
{


// ---------------------------------------------------------------- settings (config.cfg)

static char g_config_path[256] = "";

std::string trim(std::string s)
{
    auto const is_space = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && is_space(static_cast<unsigned char>(s.back())))
    {
        s.pop_back();
    }
    std::size_t i = 0;
    while (i < s.size() && is_space(static_cast<unsigned char>(s[i])))
    {
        ++i;
    }
    return s.substr(i);
}

static std::string lowercase(std::string s)
{
    for (auto& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

static bool parse_bool(std::string const& value, bool& out)
{
    auto const v = lowercase(value);
    if (v == "yes" || v == "true" || v == "on" || v == "1")
    {
        out = true;
        return true;
    }
    if (v == "no" || v == "false" || v == "off" || v == "0")
    {
        out = false;
        return true;
    }
    return false;
}

static bool parse_int(std::string const& value, int lo, int hi, int& out)
{
    if (value.empty() || value.size() > 6)
    {
        return false;
    }
    for (char const c : value)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
    }
    int const n = std::atoi(value.c_str());
    if (n < lo || n > hi)
    {
        return false;
    }
    out = n;
    return true;
}

// config.cfg text (with comments) built from the values in c. Used both for the default file
// and for saving from the settings page.
static std::string config_text(Ps5tmConfig const& c)
{
    auto const yn = [](bool b) { return b ? "yes" : "no"; };
    std::string t;
    t += "# Transmission for PS5: settings.\n";
    t += "# Format: name=value. Lines starting with #, and everything after \" #\" in a line, are comments.\n";
    t += "# The file can be edited by hand or on the settings page (http://CONSOLE_IP:settings_port/).\n";
    t += "# Changes made on the page apply at once; changes made to the file by hand apply after Transmission is restarted.\n";
    t += "# If a setting is missing from the file, its default value is used.\n";
    t += "\n";
    t += "# ---------------- Delays (seconds) ----------------\n";
    t += "\n";
    t += "# Pause after the console wakes up from rest mode before the first attempt to restore the web interface.\n";
    t += "# The network does not come up at once after waking. If the web interface is not restored after sleep,\n";
    t += "# try increasing it. Allowed: 0 to 300.\n";
    t += "wake_delay_seconds=" + std::to_string(c.wake_delay_seconds.load()) + "\n";
    t += "\n";
    t += "# How often the daemon is polled. Torrent notifications arrive with this delay.\n";
    t += "# Allowed: 1 to 600.\n";
    t += "poll_interval_seconds=" + std::to_string(c.poll_interval_seconds.load()) + "\n";
    t += "\n";
    t += "# ---------------- Notifications (yes / no) ----------------\n";
    t += "\n";
    t += "# Start, stop and restart of Transmission, stop before sleep.\n";
    t += std::string{ "notify_startup=" } + yn(c.notify_startup) + "\n";
    t += "\n";
    t += "# Torrent started.\n";
    t += std::string{ "notify_torrent_started=" } + yn(c.notify_torrent_started) + "\n";
    t += "\n";
    t += "# Torrent finished.\n";
    t += std::string{ "notify_torrent_finished=" } + yn(c.notify_torrent_finished) + "\n";
    t += "\n";
    t += "# Torrent error: no space, no access, disk error.\n";
    t += std::string{ "notify_torrent_error=" } + yn(c.notify_torrent_error) + "\n";
    t += "\n";
    t += "# Tracker error (often happens and is temporary).\n";
    t += std::string{ "notify_tracker_error=" } + yn(c.notify_tracker_error) + "\n";
    t += "\n";
    t += "# Low disk space where downloads go: less than low_disk_warn_gb, or less than what is left to download;\n";
    t += "# and \"disk almost full\": less than low_disk_critical_gb.\n";
    t += std::string{ "notify_low_disk_space=" } + yn(c.notify_low_disk_space) + "\n";
    t += std::string{ "notify_game_pause=" } + yn(c.notify_game_pause) + "\n";
    t += "\n";
    t += "# Thresholds for these notifications, GB (1 GB = 10^9 bytes). The first one is 1 to 100000, the second is 0 (do not report) to\n";
    t += "# 100000 and smaller than the first.\n";
    t += "low_disk_warn_gb=" + std::to_string(c.low_disk_warn_gb.load()) + "\n";
    t += "low_disk_critical_gb=" + std::to_string(c.low_disk_critical_gb.load()) + "\n";
    t += "\n";
    t += "# Notifications can be turned off individually. Supervision of Transmission (start, restart on a hang)\n";
    t += "# does not depend on them.\n";
    t += "\n";
    t += "# ---------------- Settings page, icon, sleep ----------------\n";
    t += "\n";
    t += "# The settings page in a browser: http://CONSOLE_IP:port/ (the icon on the Media tab opens the same page).\n";
    t += "# No password, open to the whole local network. This is the only setting that needs a restart.\n";
    t += std::string{ "settings_server=" } + yn(c.settings_server) + "\n";
    t += "\n";
    t += "# Port of the settings page (1024 to 65535). The first free port starting from this one is used; the address comes\n";
    t += "# in a notification. Rarely needs changing: in the advanced settings on the page.\n";
    t += "settings_port=" + std::to_string(c.settings_port.load()) + "\n";
    t += "\n";
    t += "# The \"Transmission\" icon on the Media tab: yes = install at start (if it is missing or the settings page port changed),\n";
    t += "# no = leave alone. Install and remove: buttons on the settings page. The registration in the system is\n";
    t += "# done by a separate short process transmission_i.elf, so that a possible hang does not touch the manager.\n";
    t += std::string{ "install_icon=" } + yn(c.install_icon) + "\n";
    t += "\n";
    t += "# ---------------- Rest mode ----------------\n";
    t += "\n";
    t += "# Auto-sleep (\"Go to rest mode when all downloads finish\" on the settings page) is switched on only\n";
    t += "# on the page and is not saved to the file. Before auto-sleep a notification can be shown and a delay observed: during that time\n";
    t += "# the sleep can be cancelled on the page. no = go to sleep at once.\n";
    t += std::string{ "sleep_notify_before=" } + yn(c.sleep_notify_before) + "\n";
    t += "\n";
    t += "# Do not let the console fall asleep by itself while auto sleep / auto power off is activated (the Activate button; a PowerTick every 10 minutes). Outside the activation the console sleeps as usual. The console settings may have an\n";
    t += "# automatic sleep time that would otherwise fire before everything is downloaded. yes / no.\n";
    t += std::string{ "keep_awake=" } + yn(c.keep_awake) + "\n";
    t += "\n";
    t += "# Delay before auto-sleep, seconds (0 = at once). From 0 to 3600.\n";
    t += "sleep_countdown_seconds=" + std::to_string(c.sleep_countdown_seconds.load()) + "\n";
    t += "\n";
    t += "# Auto-sleep timeout, minutes (1 to 1440): that many minutes after activation the console goes to sleep in any\n";
    t += "# case, even if the downloads have not finished. The activation itself is not saved to the file.\n";
    t += "autosleep_timeout_minutes=" + std::to_string(c.autosleep_timeout_minutes.load()) + "\n";
    t += "\n";
    t += "# The version at which you hid the warnings of the \"Rest mode\" section with the button on the page. After an update the version differs,\n";
    t += "# and the warnings are shown again (they may have new text). Empty = show.\n";
    t += "sleep_warnings_hidden=" + ps5tm_sleep_warnings_hidden() + "\n";
    t += "\n";
    t += "# ---------------- Transmission web interface ----------------\n";
    t += "\n";
    t += "# The \"disk status\" panel above the download list on the Transmission page: off = do not show, all = disks that hold\n";
    t += "# torrents and downloads (default), active = only disks that have running torrents.\n";
    t += "# Applies after the Transmission page is refreshed. The old values yes and no are read as all and off.\n";
    t += std::string{ "web_diskbar=" } + ps5tm_web_diskbar_name(c.web_diskbar.load()) + "\n";
    t += "# What to do when all downloads are finished (auto-sleep): sleep = go to rest mode, poweroff = turn the console off.\n";
    t += std::string{ "autosleep_action=" } + ps5tm_autosleep_action_name(c.autosleep_action.load()) + "\n";
    t += "# Rest mode method: native = system request (the console goes to sleep as from the menu, with an animation), direct = direct (the system state flag).\n";
    t += std::string{ "sleep_method=" } + ps5tm_sleep_method_name(c.sleep_method.load()) + "\n";
    if (!ps5tm_language().empty())
    {
        t += "# Interface language of the settings page: ru, en or file:<name>.strings (a file from the locales folder). If empty, the default language is used.\n";
        t += "language=" + ps5tm_language() + "\n";
    }
    t += "# Pause of downloads during a game: off = do not pause, screen = only when the game is on screen, running = while the game is running (including minimised).\n";
    t += "";
    t += std::string{ "game_pause=" } + ps5tm_game_pause_name(c.game_pause.load()) + "\n";
    t += "# The \"Open folder\" item in the torrent menu on the Transmission page: off = do not show, bfpilot = open in BFpilot (port 5905),\n";
    t += "# webfm = open in PS5 Web File Manager (port 8888). Applies after the Transmission page is refreshed.\n";
    t += std::string{ "web_openfolder=" } + ps5tm_web_openfolder_name(c.web_openfolder.load()) + "\n";
    t += "\n";
    t += "# Icons of the Transmission page (toolbar buttons, menus, torrent icons): color = colored (default),\n";
    t += "# mono = monochrome. Applies after the Transmission page is refreshed.\n";
    t += std::string{ "web_icons=" } + ps5tm_web_icons_name(c.web_icons.load()) + "\n";
    t += "\n";
    t += "# Theme of the Transmission page (port 9091): dark = dark (default), light = light.\n";
    t += "# Applies after the Transmission page is refreshed.\n";
    t += std::string{ "web_theme=" } + ps5tm_web_theme_name(c.web_theme.load()) + "\n";
    t += "\n";
    t += "# ---------------- Log retention ----------------\n";
    t += "\n";
    t += "# Notifications (the bell on the page) are kept this many days, then deleted. 1 to 365.\n";
    t += "notifications_keep_days=" + std::to_string(c.notifications_keep_days.load()) + "\n";
    t += "\n";
    t += "# Debug logs (the logs/ folder: manager, daemon, Transmission) are split by day (UTC) into files like\n";
    t += "# shim.2026-10-13.log, files older than this many days are deleted. 1 to 90.\n";
    t += "logs_keep_days=" + std::to_string(c.logs_keep_days.load()) + "\n";
    t += "\n";
    t += "# ---------------- Transmission (daemon) ----------------\n";
    t += "\n";
    t += "# Port of the Transmission web interface (1024 to 65535). When it is changed on the page Transmission restarts.\n";
    t += "daemon_port=" + std::to_string(c.daemon_port.load()) + "\n";
    t += "\n";
    t += "# If Transmission stops responding while the network is working (this happens after sleep), the manager\n";
    t += "# restarts it after this many seconds and sends a notification. 0 = do not restart. 0 to 3600.\n";
    t += "daemon_restart_after_seconds=" + std::to_string(c.daemon_restart_after_seconds.load()) + "\n";
    t += "\n";
    t += "# After a network change (the network dropped and came back, the IP address changed, the console woke from rest mode) the manager checks whether\n";
    t += "# Transmission answers on the console's current address. If it does not answer after this many seconds it is restarted at once, without waiting for the general\n";
    t += "# time above. If it answers, nothing is done. 0 = do not restart. 0 to 3600.\n";
    t += "daemon_network_restart_seconds=" + std::to_string(c.daemon_network_restart_seconds.load()) + "\n";
    t += "\n";
    t += "# How many seconds Transmission is given to close by itself after the termination signal (the Stop button,\n";
    t += "# a restart) before it is terminated by force. 5 to 120.\n";
    t += "daemon_stop_grace_seconds=" + std::to_string(c.daemon_stop_grace_seconds.load()) + "\n";
    t += "\n";
    t += "# ---------------- Download folders ----------------\n";
    t += "# New torrents can be added (and moved) only to these folders. One per line: download_folder=/path.\n";
    t += "# At most 8 folders can be added on the settings page, any number here; they can be removed both there and here.\n";
    t += "# External drives: /mnt/extN (M.2 SSD), /mnt/usbN (USB drives); internal drive: /data/...\n";
    {
        std::vector<std::string> const folders = ps5tm_folders_get();
        if (folders.empty())
        {
            t += "# download_folder=/mnt/usb0/torrents\n";
        }
        for (auto const& f : folders)
        {
            t += "download_folder=" + f + "\n";
        }
    }
    t += std::string{ "download_folders_migrated=" } + yn(c.folders_migrated) + "\n";
    t += std::string{ "daemon_restart_default_migrated=" } + yn(c.restart_default_migrated) + "\n";
    return t;
}

bool write_text_file(char const* path, std::string const& text)
{
    std::string const tmp = std::string{ path } + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (f == nullptr)
    {
        return false;
    }
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok || std::rename(tmp.c_str(), path) != 0)
    {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

// Apply a single setting by name. "" = success, otherwise the error text.
static std::vector<std::string> g_folders; // "Download folders" (order matters: the first one is the default folder)
static pthread_mutex_t g_folders_mutex = PTHREAD_MUTEX_INITIALIZER;

static char g_sleep_warnings_hidden[40] = ""; // version at which the warnings of the "Rest mode" section were hidden
static pthread_mutex_t g_sleep_warnings_mutex = PTHREAD_MUTEX_INITIALIZER;

static std::string set_by_name(std::string const& key, std::string const& value)
{
    bool b = false;
    int n = 0;

    struct BoolKey
    {
        char const* name;
        std::atomic<bool>* target;
    };
    BoolKey const bools[] = {
        { "notify_startup", &g_cfg.notify_startup },
        { "notify_torrent_started", &g_cfg.notify_torrent_started },
        { "notify_torrent_finished", &g_cfg.notify_torrent_finished },
        { "notify_torrent_error", &g_cfg.notify_torrent_error },
        { "notify_tracker_error", &g_cfg.notify_tracker_error },
        { "notify_low_disk_space", &g_cfg.notify_low_disk_space },
    { "notify_game_pause", &g_cfg.notify_game_pause },
        { "settings_server", &g_cfg.settings_server },
        { "install_icon", &g_cfg.install_icon },
        { "download_folders_migrated", &g_cfg.folders_migrated },
        { "daemon_restart_default_migrated", &g_cfg.restart_default_migrated },
        { "sleep_notify_before", &g_cfg.sleep_notify_before },
        { "keep_awake", &g_cfg.keep_awake },
    };
    for (auto const& entry : bools)
    {
        if (key == entry.name)
        {
            if (!parse_bool(value, b))
            {
                return ps5tm_tr("mr_cfg_need_bool", { key, value });
            }
            *entry.target = b;
            return "";
        }
    }

    if (key == "download_folder")
    {
        if (value.empty() || value[0] != '/')
        {
            return ps5tm_tr("mr_cfg_abs_path", { value });
        }
        pthread_mutex_lock(&g_folders_mutex);
        bool known = false;
        for (auto const& f : g_folders)
        {
            known = known || f == value;
        }
        if (!known)
        {
            g_folders.push_back(value);
        }
        pthread_mutex_unlock(&g_folders_mutex);
        return "";
    }
    if (key == "sleep_warnings_hidden")
    {
        bool valid = value.size() <= 32;
        for (char const c : value)
        {
            valid = valid && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == '_'); // no isalnum: we do not add new imports
        }
        if (!valid)
        {
            return ps5tm_tr("mr_cfg_sleep_warn");
        }
        pthread_mutex_lock(&g_sleep_warnings_mutex);
        std::snprintf(g_sleep_warnings_hidden, sizeof(g_sleep_warnings_hidden), "%s", value.c_str());
        pthread_mutex_unlock(&g_sleep_warnings_mutex);
        return "";
    }
    if (key == "web_icons")
    {
        for (int i = 0; i < 2; ++i)
        {
            if (value == ps5tm_web_icons_name(i))
            {
                g_cfg.web_icons = i;
                return "";
            }
        }
        if (value == "rounded" || value == "lucide" || value == "bold") // old names of the monochrome icon sets: now a single set
        {
            g_cfg.web_icons = 1;
            return "";
        }
        return ps5tm_tr("mr_cfg_need", { "web_icons", "color" + ps5tm_tr("mr_or") + "mono", value });
    }
    if (key == "language")
    {
        bool ok = value.empty() || value == "ru" || value == "en";
        if (!ok && value.compare(0, 5, "file:") == 0 && value.size() > 5 && value.size() <= 69)
        {
            ok = true;
            for (std::size_t i = 5; i < value.size(); ++i)
            {
                char const c = value[i];
                ok = ok && (ps5tm_alnum(c) || c == '.' || c == '_' || c == '-');
            }
        }
        if (!ok)
        {
            return ps5tm_tr("mr_cfg_need_lang", { value });
        }
        pthread_mutex_lock(&g_language_mutex);
        g_language = value;
        pthread_mutex_unlock(&g_language_mutex);
        return "";
    }
    if (key == "sleep_method")
    {
        for (int i = 0; i < 2; ++i)
        {
            if (value == ps5tm_sleep_method_name(i))
            {
                g_cfg.sleep_method = i;
                return "";
            }
        }
        return ps5tm_tr("mr_cfg_need", { "sleep_method", "native" + ps5tm_tr("mr_or") + "direct", value });
    }
    if (key == "autosleep_action")
    {
        for (int i = 0; i < 2; ++i)
        {
            if (value == ps5tm_autosleep_action_name(i))
            {
                g_cfg.autosleep_action = i;
                return "";
            }
        }
        return ps5tm_tr("mr_cfg_need", { "autosleep_action", "sleep" + ps5tm_tr("mr_or") + "poweroff", value });
    }
    if (key == "game_pause")
    {
        for (int i = 0; i < 3; ++i)
        {
            if (value == ps5tm_game_pause_name(i))
            {
                g_cfg.game_pause = i;
                return "";
            }
        }
        return ps5tm_tr("mr_cfg_need", { "game_pause", "off, screen" + ps5tm_tr("mr_or") + "running", value });
    }
    if (key == "web_openfolder")
    {
        for (int i = 0; i < 3; ++i)
        {
            if (value == ps5tm_web_openfolder_name(i))
            {
                g_cfg.web_openfolder = i;
                return "";
            }
        }
        return ps5tm_tr("mr_cfg_need", { "web_openfolder", "off, bfpilot" + ps5tm_tr("mr_or") + "webfm", value });
    }
    if (key == "web_diskbar")
    {
        for (int i = 0; i < 3; ++i)
        {
            if (value == ps5tm_web_diskbar_name(i))
            {
                g_cfg.web_diskbar = i;
                return "";
            }
        }
        if (value == "yes" || value == "true" || value == "1" || value == "on") // old value (checkbox): on
        {
            g_cfg.web_diskbar = 1;
            return "";
        }
        if (value == "no" || value == "false" || value == "0") // old value (checkbox): off
        {
            g_cfg.web_diskbar = 0;
            return "";
        }
        return ps5tm_tr("mr_cfg_need", { "web_diskbar", "off, all" + ps5tm_tr("mr_or") + "active", value });
    }
    if (key == "web_theme")
    {
        for (int i = 0; i < 2; ++i)
        {
            if (value == ps5tm_web_theme_name(i))
            {
                g_cfg.web_theme = i;
                return "";
            }
        }
        if (value == "system") // the old value ("follow the browser") was removed: it is now the dark theme
        {
            g_cfg.web_theme = 0;
            return "";
        }
        return ps5tm_tr("mr_cfg_need", { "web_theme", "dark" + ps5tm_tr("mr_or") + "light", value });
    }

    // old settings (the "all in one" version): silently ignore
    if (key == "restart_elf_path" || key == "auto_restart_after_seconds" || key == "notify_web_ui")
    {
        return "";
    }

    struct IntKey
    {
        char const* name;
        std::atomic<int>* target;
        int lo;
        int hi;
    };
    IntKey const ints[] = {
        { "wake_delay_seconds", &g_cfg.wake_delay_seconds, 0, 300 },
        { "poll_interval_seconds", &g_cfg.poll_interval_seconds, 1, 600 },
        { "settings_port", &g_cfg.settings_port, 1024, 65535 },
        { "sleep_countdown_seconds", &g_cfg.sleep_countdown_seconds, 0, 3600 },
        { "daemon_restart_after_seconds", &g_cfg.daemon_restart_after_seconds, 0, 3600 },
        { "daemon_network_restart_seconds", &g_cfg.daemon_network_restart_seconds, 0, 3600 },
        { "daemon_stop_grace_seconds", &g_cfg.daemon_stop_grace_seconds, 5, 120 },
        { "daemon_port", &g_cfg.daemon_port, 1024, 65535 },
        { "notifications_keep_days", &g_cfg.notifications_keep_days, 1, 365 },
        { "logs_keep_days", &g_cfg.logs_keep_days, 1, 90 },
        { "low_disk_warn_gb", &g_cfg.low_disk_warn_gb, 1, 100000 },
        { "autosleep_timeout_minutes", &g_cfg.autosleep_timeout_minutes, 1, 1440 },
        { "low_disk_critical_gb", &g_cfg.low_disk_critical_gb, 0, 100000 },
    };
    for (auto const& entry : ints)
    {
        if (key == entry.name)
        {
            if (!parse_int(value, entry.lo, entry.hi, n))
            {
                return ps5tm_tr("mr_cfg_need_range", { key, std::to_string(entry.lo), std::to_string(entry.hi), value });
            }
            if (key == "settings_port" && n == rpc_port())
            {
                return ps5tm_tr("mr_cfg_port_web", { std::to_string(n) });
            }
            if (key == "daemon_port" && (n == g_cfg.settings_port.load() || n == g_settings_port_actual.load()))
            {
                return ps5tm_tr("mr_cfg_port_page", { std::to_string(n) });
            }
            *entry.target = n;
            return "";
        }
    }

    return ps5tm_tr("mr_cfg_unknown", { key });
}

// Loading the settings. If the file does not exist, it is created with the default values. Returns 1 if
// at least one notification is enabled for the monitor (otherwise it would idle).
// The download path currently set in the Transmission settings (settings.json, key download-dir); "" if the file or the key is missing.
static std::string read_settings_download_dir()
{
    FILE* f = std::fopen("/data/Transmission/transmission/config/settings.json", "rb");
    if (f == nullptr)
    {
        return "";
    }
    std::string text;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0 && text.size() < 262144)
    {
        text.append(buffer, n);
    }
    std::fclose(f);
    auto const key = text.find("\"download-dir\"");
    if (key == std::string::npos)
    {
        return "";
    }
    auto i = text.find('"', text.find(':', key + 14) == std::string::npos ? text.size() : text.find(':', key + 14));
    if (i == std::string::npos)
    {
        return "";
    }
    std::string out;
    for (++i; i < text.size() && text[i] != '"'; ++i)
    {
        if (text[i] == '\\' && i + 1 < text.size())
        {
            ++i;
        }
        out += text[i];
    }
    return out;
}

// Once, on the first start with this feature: the current Transmission download path becomes the first "download folder" (so nothing
// changes for those who upgrade). A new user has no Transmission settings yet: the list stays empty.
static void migrate_folders_once()
{
    if (g_cfg.folders_migrated.load())
    {
        return;
    }
    std::string const dir = read_settings_download_dir();
    if (!dir.empty() && dir[0] == '/' && ps5tm_folders_get().empty())
    {
        ps5tm_folders_set({ dir });
        journal_add(ps5tm_tr("mr_folder_moved", { dir }).c_str(), 2);
        shim_log("download folders: migrated the current download-dir %s", dir.c_str());
    }
    g_cfg.folders_migrated = true;
    ps5tm_config_save();
}

} // namespace ps5tm_core

using namespace ps5tm_core;


std::vector<std::string> ps5tm_folders_get()
{
    pthread_mutex_lock(&g_folders_mutex);
    std::vector<std::string> const copy = g_folders;
    pthread_mutex_unlock(&g_folders_mutex);
    return copy;
}

void ps5tm_folders_set(std::vector<std::string> const& folders)
{
    pthread_mutex_lock(&g_folders_mutex);
    g_folders = folders;
    pthread_mutex_unlock(&g_folders_mutex);
}

std::string ps5tm_sleep_warnings_hidden()
{
    pthread_mutex_lock(&g_sleep_warnings_mutex);
    std::string const value = g_sleep_warnings_hidden;
    pthread_mutex_unlock(&g_sleep_warnings_mutex);
    return value;
}

char const* ps5tm_web_icons_name(int index)
{
    static char const* const names[] = { "color", "mono" };
    return index >= 0 && index < 2 ? names[index] : names[0];
}

char const* ps5tm_web_theme_name(int index)
{
    static char const* const names[] = { "dark", "light" };
    return index >= 0 && index < 2 ? names[index] : names[0];
}

std::string ps5tm_language()
{
    pthread_mutex_lock(&g_language_mutex);
    std::string const copy = g_language;
    pthread_mutex_unlock(&g_language_mutex);
    return copy;
}

char const* ps5tm_sleep_method_name(int index)
{
    static char const* const names[] = { "native", "direct" };
    return index >= 0 && index < 2 ? names[index] : names[0];
}

char const* ps5tm_autosleep_action_name(int index)
{
    static char const* const names[] = { "sleep", "poweroff" };
    return index >= 0 && index < 2 ? names[index] : names[0];
}

char const* ps5tm_game_pause_name(int index)
{
    static char const* const names[] = { "off", "screen", "running" };
    return index >= 0 && index < 3 ? names[index] : names[0];
}

char const* ps5tm_web_openfolder_name(int index)
{
    static char const* const names[] = { "off", "bfpilot", "webfm" };
    return index >= 0 && index < 3 ? names[index] : names[0];
}

char const* ps5tm_web_diskbar_name(int index)
{
    static char const* const names[] = { "off", "all", "active" };
    return index >= 0 && index < 3 ? names[index] : names[1];
}

extern "C" int ps5tm_config_init(char const* path)
{
    journal_load(); // bell: notifications from before the manager restart
    std::snprintf(g_config_path, sizeof(g_config_path), "%s", path);
    if (access(path, F_OK) != 0)
    {
        if (write_text_file(path, config_text(Ps5tmConfig{})))
        {
            shim_log("created the default settings file %s", path);
        }
        else
        {
            shim_log("warning: cannot create %s (errno %d): defaults are used", path, errno);
        }
    }

    FILE* f = std::fopen(path, "r");
    if (f == nullptr)
    {
        shim_log("warning: cannot read %s (errno %d): defaults are used", path, errno);
    }
    else
    {
        char raw[512];
        int line = 0;
        while (std::fgets(raw, sizeof(raw), f) != nullptr)
        {
            ++line;
            std::string text{ raw };
            if (line == 1 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) // BOM from the editor
            {
                text.erase(0, 3);
            }
            auto const comment = text.find(" #");
            if (comment != std::string::npos)
            {
                text.erase(comment);
            }
            text = trim(text);
            if (text.empty() || text[0] == '#' || text[0] == ';')
            {
                continue;
            }
            auto const eq = text.find('=');
            if (eq == std::string::npos)
            {
                shim_log("config.cfg:%d: no '=' in '%s': line ignored", line, text.c_str());
                continue;
            }
            auto const error = set_by_name(lowercase(trim(text.substr(0, eq))), trim(text.substr(eq + 1)));
            if (!error.empty())
            {
                shim_log("config.cfg:%d: %s (default kept)", line, error.c_str());
            }
        }
        std::fclose(f);
    }

    // Once: the old default (15 s without a response) is too short for a daemon busy with long disk work; replace it with 120 s.
    // A value chosen manually (not 15) is left alone.
    if (!g_cfg.restart_default_migrated.load())
    {
        if (g_cfg.daemon_restart_after_seconds.load() == 15)
        {
            g_cfg.daemon_restart_after_seconds = 120;
            shim_log("config: daemon_restart_after_seconds: the old default 15 s replaced by 120 s");
        }
        g_cfg.restart_default_migrated = true;
        ps5tm_config_save();
    }
    migrate_folders_once();
    return 1;
}

extern "C" int ps5tm_cfg_notify_startup(void)
{
    return g_cfg.notify_startup ? 1 : 0;
}

std::string ps5tm_config_text()
{
    return config_text(g_cfg);
}

bool ps5tm_config_save()
{
    if (g_config_path[0] == 0)
    {
        return false;
    }
    return write_text_file(g_config_path, config_text(g_cfg));
}

std::string ps5tm_config_set(std::string const& key, std::string const& value)
{
    if (key == "daemon_port")
    {
        // The new port must not be taken by another program. The port our daemon is currently running on does not count.
        int const n = std::atoi(value.c_str());
        if (n == g_cfg.settings_port.load() || n == g_settings_port_actual.load())
        {
            return ps5tm_tr("mr_cfg_port_page", { value });
        }
        if (n >= 1024 && n <= 65535 && n != g_d.launched_port.load() && ps5tm_port_in_use(n))
        {
            return ps5tm_tr("mr_cfg_port_other", { value });
        }
    }
    return set_by_name(key, value);
}
