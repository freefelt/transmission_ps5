// Transmission for the PS5: the handlers of the JSON API (state, settings, sleep, daemon, folders, folder browser).
//
// License: GPL-3.0-or-later (like Transmission).

#include "ui_internal.h"

namespace ps5tm_ui
{

std::string state_json()
{
    long const now = static_cast<long>(std::time(nullptr));
    long const last_poll = g_stats.last_poll.load();
    std::string j = "{";
    j += "\"version\":" + jstr(ps5tm_version());
    j += ",\"tr_version\":" + jstr(PS5TM_TR_VERSION);
    j += ",\"uptime\":" + std::to_string(ps5tm_uptime_seconds());
    j += ",\"ip\":" + jstr(ps5tm_ip());
    j += ",\"rpc_port\":" + std::to_string(g_cfg.daemon_port.load()) + ",\"settings_port_actual\":" + std::to_string(g_settings_port_actual.load()) + ",\"notif_latest\":" + std::to_string(ps5tm_notifications_latest());
    j += ",\"config\":{";
    bool first = true;
    for (auto const& k : Keys)
    {
        j += std::string{ first ? "" : "," } + jstr(k.name) + ":" + current_value(k.name);
        first = false;
    }
    j += "}";
    j += ",\"stats\":{\"total\":" + std::to_string(g_stats.total.load()) + ",\"downloading\":" +
        std::to_string(g_stats.downloading.load()) + ",\"seeding\":" + std::to_string(g_stats.seeding.load()) +
        ",\"paused\":" + std::to_string(g_stats.paused.load()) + ",\"game_pause_active\":" + jbool(ps5tm_game_pause_active()) + ",\"syssvc\":" + ps5tm_syssvc_json() + ",\"last_poll_age\":" +
        (last_poll == 0 ? std::string{ "-1" } : std::to_string(now - last_poll)) + ",\"rpc_ok\":" + jbool(g_stats.rpc_ok) +
        ",\"monitor_active\":" + jbool(g_stats.monitor_active) + ",\"down_bps\":" + std::to_string(g_stats.down_bps.load()) + ",\"up_bps\":" + std::to_string(g_stats.up_bps.load()) + "}";
    {
        pthread_mutex_lock(&g_icon_mutex);
        std::string const icon_error = g_icon_error;
        pthread_mutex_unlock(&g_icon_mutex);
        j += ",\"icon\":{\"installed\":" + jbool(icon_installed()) + ",\"busy\":" + jbool(g_icon_busy.load()) + ",\"error\":" + jstr(icon_error) + "}";
    }
    {
        auto const info = ps5tm_daemon_info();
        j += ",\"daemon\":{\"state\":" + jstr(info.state) + ",\"pid\":" + std::to_string(info.pid) + ",\"since_age\":" +
            std::to_string(info.since_age) + ",\"desired_running\":" + jbool(info.desired_running) + ",\"busy\":" + jbool(info.busy) + ",\"mem_kib\":" + std::to_string(info.mem_kib) + ",\"last_error\":" + jstr(info.last_error) + ",\"unresponsive_for\":" + std::to_string(info.unresponsive_for) + ",\"restart_in\":" + std::to_string(info.restart_in) + ",\"stuck\":" + jbool(info.stuck) + "}";
    }
    {
        auto const sl = ps5tm_sleep_info();
        j += ",\"sleep\":{\"phase\":" + jstr(sl.phase) + ",\"remaining\":" + std::to_string(sl.remaining) + ",\"holding\":" + jbool(sl.holding) + ",\"manual\":" + jbool(sl.manual) + "}";
        j += ",\"autosleep\":{\"armed\":" + jbool(sl.armed) + ",\"can_arm\":" + jbool(sl.can_arm) + ",\"downloading\":" +
            std::to_string(sl.downloading) + ",\"timeout_minutes\":" + std::to_string(sl.timeout_minutes) + ",\"timeout_remaining\":" +
            std::to_string(sl.timeout_remaining) + "}";
    }
    j += ",\"kstuff\":" + jbool(ps5tm_process_running("kstuff.elf") != 0);
    j += "}";
    return j;
}

bool is_json(Request const& req)
{
    return req.content_type.compare(0, 16, "application/json") == 0;
}

void handle_config(int fd, Request const& req)
{
    std::map<std::string, std::string> values;
    if (!parse_flat_json(req.body, values))
    {
        send_json(fd, 400, "Bad Request", std::string{ "{\"ok\":false,\"errors\":[" } + jstr(ps5tm_tr("mr_bad_json")) + "]}");
        return;
    }

    // check: known keys allowed for the page
    std::vector<std::string> errors;
    for (auto const& entry : values)
    {
        if (entry.first == "language")
        {
            std::string const lang_error = lang_validate_choice(entry.second);
            if (!lang_error.empty())
            {
                errors.push_back("LANGERR:" + lang_error);
            }
        }
        bool known = false;
        for (auto const& k : Keys)
        {
            if (entry.first == k.name)
            {
                known = true;
                if (!k.editable_in_ui)
                {
                    errors.push_back(ps5tm_tr("mr_cfg_file_only", { entry.first }));
                }
            }
        }
        if (!known)
        {
            errors.push_back(ps5tm_tr("mr_cfg_unknown", { entry.first }));
        }
    }

    // apply all or none: remember the old values and roll back on the first error
    std::map<std::string, std::string> old;
    if (errors.empty())
    {
        for (auto const& entry : values)
        {
            old[entry.first] = raw_value(entry.first);
        }
        for (auto const& entry : values)
        {
            auto const error = ps5tm_config_set(entry.first, entry.second);
            if (!error.empty())
            {
                errors.push_back(error);
            }
        }
        if (errors.empty() && g_cfg.low_disk_critical_gb.load() >= g_cfg.low_disk_warn_gb.load() && g_cfg.low_disk_critical_gb.load() != 0)
        {
            errors.push_back(ps5tm_tr("mr_cfg_disk_order"));
        }
        if (!errors.empty())
        {
            for (auto const& entry : old)
            {
                ps5tm_config_set(entry.first, entry.second);
            }
        }
    }

    if (!errors.empty())
    {
        std::string j = "{\"ok\":false,\"errors\":[";
        for (std::size_t i = 0; i < errors.size(); ++i)
        {
            j += std::string{ i ? "," : "" } + jstr(errors[i]);
        }
        send_json(fd, 400, "Bad Request", j + "]}");
        return;
    }

    bool const saved = ps5tm_config_save();
    shim_log("settings page: %zu setting(s) changed%s", values.size(), saved ? "" : " (config.cfg NOT saved)");
    if ((values.count("notifications_keep_days") != 0 && old["notifications_keep_days"] != std::to_string(g_cfg.notifications_keep_days.load())) ||
        (values.count("logs_keep_days") != 0 && old["logs_keep_days"] != std::to_string(g_cfg.logs_keep_days.load())))
    {
        ps5tm_maintenance_run(); // the retention period was reduced: the excess is deleted right away
    }
    if (values.count("autosleep_timeout_minutes") != 0 && old["autosleep_timeout_minutes"] != std::to_string(g_cfg.autosleep_timeout_minutes.load()))
    {
        ps5tm_autosleep_set_timeout(g_cfg.autosleep_timeout_minutes.load()); // if auto-sleep is activated, the countdown starts from this moment
    }
    if ((values.count("web_icons") != 0 && old["web_icons"] != ps5tm_web_icons_name(g_cfg.web_icons.load())) ||
        (values.count("web_theme") != 0 && old["web_theme"] != ps5tm_web_theme_name(g_cfg.web_theme.load())) ||
        (values.count("web_diskbar") != 0 && old["web_diskbar"] != ps5tm_web_diskbar_name(g_cfg.web_diskbar.load())) ||
        (values.count("web_openfolder") != 0 && old["web_openfolder"] != ps5tm_web_openfolder_name(g_cfg.web_openfolder.load())) || values.count("language") != 0)
    {
        ps5tm_web_theme_apply(); // the theme and disk panel on the Transmission page change right away, after this page is refreshed
    }
    if (values.count("daemon_port") != 0 && old["daemon_port"] != std::to_string(g_cfg.daemon_port.load()))
    {
        ps5tm_daemon_port_changed(); // Transmission is restarted with the new port
    }
    send_json(
        fd,
        200,
        "OK",
        std::string{ "{\"ok\":true,\"saved\":" } + jbool(saved) + ",\"settings_port\":" + std::to_string(g_cfg.settings_port.load()) + "}");
}

void handle_icon(int fd, Request const& req)
{
    std::map<std::string, std::string> values;
    bool const parsed = parse_flat_json(req.body, values);
    std::string const action = parsed ? values["action"] : std::string{};
    if (!parsed || (action != "install" && action != "remove"))
    {
        send_json(fd, 400, "Bad Request", "{\"ok\":false,\"message\":\"action install or remove is needed\"}");
        return;
    }
    bool const started = icon_start(action, "settings page", true);
    shim_log("settings page: icon %s requested: %s", action.c_str(), started ? "started" : "busy");
    send_json(
        fd,
        started ? 200 : 409,
        started ? "OK" : "Conflict",
        std::string{ "{\"ok\":" } + jbool(started) + ",\"message\":" + jstr(started ? "" : ps5tm_tr("mr_icon_running")) + "}");
}


void handle_sleep(int fd, Request const& req, std::string const& client)
{
    std::map<std::string, std::string> values;
    if (!parse_flat_json(req.body, values) || (values["action"] != "start" && values["action"] != "cancel"))
    {
        send_json(fd, 400, "Bad Request", "{\"ok\":false,\"message\":\"action start or cancel is needed\"}");
        return;
    }
    if (values["action"] == "cancel")
    {
        bool const was = ps5tm_sleep_cancel();
        shim_log("sleep: cancel requested from %s: %s", client.c_str(), was ? "countdown cancelled" : "no countdown");
        send_json(fd, 200, "OK", std::string{ "{\"ok\":true,\"was_pending\":" } + jbool(was) + "}");
        return;
    }
    std::string const error = ps5tm_sleep_request(client.c_str());
    shim_log("sleep: requested from %s: %s", client.c_str(), error.empty() ? "accepted" : error.c_str());
    send_json(fd, error.empty() ? 200 : 409, error.empty() ? "OK" : "Conflict", std::string{ "{\"ok\":" } + jbool(error.empty()) + ",\"message\":" + jstr(error) + "}");
}

// "Go to rest mode when all downloads finish": can be enabled only with active downloads and is not saved.
void handle_autosleep(int fd, Request const& req)
{
    std::map<std::string, std::string> values;
    bool const parsed = parse_flat_json(req.body, values);
    std::string const action = parsed ? values["action"] : std::string{};
    if (!parsed || (action != "arm" && action != "disarm" && action != "timeout"))
    {
        send_json(fd, 400, "Bad Request", "{\"ok\":false,\"message\":\"action arm, disarm or timeout is needed\"}");
        return;
    }
    std::string error;
    if (action == "disarm")
    {
        ps5tm_autosleep_disarm();
    }
    else
    {
        // arm: the timeout is optional (taken from the settings); timeout: the new value in minutes
        std::string const key = action == "arm" ? "timeout_minutes" : "minutes";
        long minutes = 0;
        bool valid = true;
        if (values.count(key) != 0)
        {
            char* end = nullptr;
            minutes = std::strtol(values[key].c_str(), &end, 10);
            valid = end != values[key].c_str() && *end == '\0';
        }
        error = !valid ? std::string{ "timeout: a number of minutes is needed" }
                       : (action == "arm" ? ps5tm_autosleep_arm(static_cast<int>(minutes)) : ps5tm_autosleep_set_timeout(static_cast<int>(minutes)));
    }
    send_json(fd, error.empty() ? 200 : 409, error.empty() ? "OK" : "Conflict", std::string{ "{\"ok\":" } + jbool(error.empty()) + ",\"message\":" + jstr(error) + "}");
}


// ---------------------------------------------------------------- diagnostics

// The last lines of the wrapper log so that they can be read and copied right from the page
std::string read_file_tail(char const* path, std::size_t max_bytes)
{
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr)
    {
        return std::string{ "file unavailable: " } + path + "\n";
    }
    std::fseek(f, 0, SEEK_END);
    long const size = std::ftell(f);
    long const start = size > static_cast<long>(max_bytes) ? size - static_cast<long>(max_bytes) : 0;
    std::fseek(f, start, SEEK_SET);
    std::string data(static_cast<std::size_t>(size - start), '\0');
    std::size_t const n = std::fread(&data[0], 1, data.size(), f);
    data.resize(n);
    std::fclose(f);
    if (start > 0)
    {
        auto const newline = data.find('\n');
        if (newline != std::string::npos)
        {
            data.erase(0, newline + 1); // the first line could have been cut
        }
    }
    return data;
}

void handle_daemon(int fd, Request const& req)
{
    std::map<std::string, std::string> values;
    if (!parse_flat_json(req.body, values) || (values["action"] != "stop" && values["action"] != "start"))
    {
        send_json(fd, 400, "Bad Request", "{\"ok\":false,\"message\":\"action stop or start is needed\"}");
        return;
    }
    std::string const& action = values["action"];
    std::string const error = action == "stop" ? ps5tm_daemon_request_stop() : ps5tm_daemon_request_start();
    shim_log("settings page: daemon %s requested: %s", action.c_str(), error.empty() ? "accepted" : error.c_str());
    send_json(fd, error.empty() ? 200 : 409, error.empty() ? "OK" : "Conflict", std::string{ "{\"ok\":" } + jbool(error.empty()) + ",\"message\":" + jstr(error) + "}");
}
void handle_fs_post(int fd, Request const& req)
{
    std::map<std::string, std::string> values;
    if (!parse_flat_json(req.body, values))
    {
        send_json(fd, 400, "Bad Request", std::string{ "{\"error\":" } + jstr(ps5tm_tr("mr_bad_request")) + "}");
        return;
    }
    std::string const action = values["action"];
    std::string const path = values["path"];
    std::string const name = values["name"];
    std::string reply;
    if (action == "list")
    {
        reply = run_isolated([path] { return fs_list_json(path); }, 8, "folders");
    }
    else if (action == "mkdir")
    {
        reply = run_isolated([path, name] { return fs_mkdir_json(path, name); }, 8, "folders");
    }
    else
    {
        reply = fs_error(ps5tm_tr("mr_unknown_action"));
    }
    bool const failed = reply.compare(0, 9, "{\"error\":") == 0 || reply.find("call did not return") != std::string::npos;
    send_json(fd, failed ? 400 : 200, failed ? "Bad Request" : "OK", failed && reply.compare(0, 9, "{\"error\":") != 0 ? fs_error(reply) : reply);
}

void handle_folders_post(int fd, Request const& req)
{
    std::map<std::string, std::string> values;
    if (!parse_flat_json(req.body, values))
    {
        send_json(fd, 400, "Bad Request", std::string{ "{\"error\":" } + jstr(ps5tm_tr("mr_bad_request")) + "}");
        return;
    }
    std::string const& action = values["action"];
    if (action == "add")
    {
        bool need_confirm = false;
        std::string normalized;
        std::string reason;
        std::string const error = folder_add(values["path"], values["confirm"] == "true", need_confirm, normalized, reason);
        if (need_confirm)
        {
            std::string fs_name;
            {
                std::vector<std::string> c;
                std::string root;
                if (reason == "fs" && normalize_folder(normalized, c).empty() && folder_kind(c, root) != 0)
                {
                    fs_name = volume_fs(root).name;
                }
            }
            send_json(fd, 409, "Conflict", "{\"need_confirm\":true,\"reason\":" + jstr(reason) + ",\"fs\":" + jstr(fs_name) + ",\"error\":" + jstr(error) + "}");
        }
        else if (!error.empty())
        {
            send_json(fd, 400, "Bad Request", "{\"error\":" + jstr(error) + "}");
        }
        else
        {
            send_json(fd, 200, "OK", "{\"ok\":true,\"path\":" + jstr(normalized) + "}");
        }
    }
    else if (action == "unmount")
    {
        std::string message;
        std::string holders = "[]";
        int const status = folder_unmount(values["path"], values["force"] == "true", message, holders);
        if (status == 0)
        {
            send_json(fd, 200, "OK", "{\"ok\":true,\"message\":" + jstr(message) + "}");
        }
        else if (status == 1)
        {
            send_json(fd, 409, "Conflict", "{\"busy\":true,\"error\":" + jstr(message) + ",\"holders\":" + holders + "}");
        }
        else
        {
            send_json(fd, 409, "Conflict", "{\"error\":" + jstr(message) + "}");
        }
    }
    else if (action == "remove")
    {
        std::string const error = folder_remove(values["path"]);
        if (error.empty())
        {
            send_json(fd, 200, "OK", "{\"ok\":true}");
        }
        else
        {
            send_json(fd, 404, "Not Found", "{\"error\":" + jstr(error) + "}");
        }
    }
    else
    {
        send_json(fd, 400, "Bad Request", std::string{ "{\"error\":" } + jstr(ps5tm_tr("mr_unknown_action")) + "}");
    }
}

} // namespace ps5tm_ui
