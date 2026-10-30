// Transmission for the PS5: the settings keys the page can read and write (table and current values).
//
// License: GPL-3.0-or-later (like Transmission).

#include "ui_internal.h"

namespace ps5tm_ui
{

// ---------------------------------------------------------------- setting values




std::string current_value(std::string const& key)
{
    if (key == "wake_delay_seconds") return std::to_string(g_cfg.wake_delay_seconds.load());
    if (key == "poll_interval_seconds") return std::to_string(g_cfg.poll_interval_seconds.load());
    if (key == "notify_startup") return jbool(g_cfg.notify_startup);
    if (key == "notify_torrent_started") return jbool(g_cfg.notify_torrent_started);
    if (key == "notify_torrent_finished") return jbool(g_cfg.notify_torrent_finished);
    if (key == "notify_torrent_error") return jbool(g_cfg.notify_torrent_error);
    if (key == "notify_tracker_error") return jbool(g_cfg.notify_tracker_error);
    if (key == "notify_low_disk_space") return jbool(g_cfg.notify_low_disk_space);
    if (key == "notify_game_pause") return jbool(g_cfg.notify_game_pause);
    if (key == "settings_server") return jbool(g_cfg.settings_server);
    if (key == "settings_port") return std::to_string(g_cfg.settings_port.load());
    if (key == "install_icon") return jbool(g_cfg.install_icon);
    if (key == "sleep_notify_before") return jbool(g_cfg.sleep_notify_before);
    if (key == "sleep_countdown_seconds") return std::to_string(g_cfg.sleep_countdown_seconds.load());
    if (key == "daemon_restart_after_seconds") return std::to_string(g_cfg.daemon_restart_after_seconds.load());
    if (key == "daemon_network_restart_seconds") return std::to_string(g_cfg.daemon_network_restart_seconds.load());
    if (key == "daemon_stop_grace_seconds") return std::to_string(g_cfg.daemon_stop_grace_seconds.load());
    if (key == "daemon_port") return std::to_string(g_cfg.daemon_port.load());
    if (key == "low_disk_warn_gb") return std::to_string(g_cfg.low_disk_warn_gb.load());
    if (key == "low_disk_critical_gb") return std::to_string(g_cfg.low_disk_critical_gb.load());
    if (key == "autosleep_timeout_minutes") return std::to_string(g_cfg.autosleep_timeout_minutes.load());
    if (key == "notifications_keep_days") return std::to_string(g_cfg.notifications_keep_days.load());
    if (key == "logs_keep_days") return std::to_string(g_cfg.logs_keep_days.load());
    if (key == "web_theme") return jstr(ps5tm_web_theme_name(g_cfg.web_theme.load()));
    if (key == "web_diskbar") return jstr(ps5tm_web_diskbar_name(g_cfg.web_diskbar.load()));
    if (key == "web_openfolder") return jstr(ps5tm_web_openfolder_name(g_cfg.web_openfolder.load()));
    if (key == "game_pause") return jstr(ps5tm_game_pause_name(g_cfg.game_pause.load()));
    if (key == "autosleep_action") return jstr(ps5tm_autosleep_action_name(g_cfg.autosleep_action.load()));
    if (key == "sleep_method") return jstr(ps5tm_sleep_method_name(g_cfg.sleep_method.load()));
    if (key == "keep_awake") return jbool(g_cfg.keep_awake);
    if (key == "language")
    {
        std::string shown = ps5tm_language().empty() ? lang_default() : ps5tm_language();
        return jstr(shown.empty() ? std::string{ "en" } : shown);
    }
    if (key == "web_icons") return jstr(ps5tm_web_icons_name(g_cfg.web_icons.load()));
    if (key == "sleep_warnings_hidden") return jstr(ps5tm_sleep_warnings_hidden());
    return "";
}

// Value "as in config.cfg": for rollback when saving fails (strings without JSON quotes)
std::string raw_value(std::string const& key)
{
    std::string value = current_value(key);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    {
        value = value.substr(1, value.size() - 2); // our strings are unescaped (theme name, version)
    }
    return value;
}

} // namespace ps5tm_ui
