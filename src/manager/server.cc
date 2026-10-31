// Transmission for the PS5: the settings server thread: connection routing and the main loop.
//
// License: GPL-3.0-or-later (like Transmission).

#include "ui_internal.h"
#include "ps5tm_page.h" // kPageHtml: the settings page assembled from src/manager/page/ by scripts/build-elf.sh
#include "icon0.h"
#include "icon128.h" // the original 128x128 Transmission icon: the page header
#include "iconfav.h" // favicon of the settings server: the same icon with a blue lever, so the tab is not confused with the Transmission page

namespace ps5tm_ui
{

static void handle_connection(int fd, std::string const& client)
{
    Request req;
    if (!read_request(fd, req))
    {
        return;
    }

    if (req.method == "GET" && (req.path == "/" || req.path == "/index.html"))
    {
        send_response(fd, 200, "OK", "text/html; charset=utf-8", build_settings_page(kPageHtml));
    }
    else if (req.method == "GET" && req.path == "/icon.png")
    {
        send_response(fd, 200, "OK", "image/png", std::string{ reinterpret_cast<char const*>(icon0_png), icon0_png_len });
    }
    else if (req.method == "GET" && req.path == "/icon-128.png")
    {
        send_response(fd, 200, "OK", "image/png", std::string{ reinterpret_cast<char const*>(icon128_png), icon128_png_len });
    }
    else if (req.method == "GET" && (req.path == "/favicon.png" || req.path == "/favicon.ico"))
    {
        send_response(fd, 200, "OK", "image/png", std::string{ reinterpret_cast<char const*>(iconfav_png), iconfav_png_len });
    }
    else if (req.method == "GET" && req.path == "/api/state")
    {
        send_json(fd, 200, "OK", state_json());
    }
    else if (req.method == "GET" && req.path == "/api/diag/threads")
    {
        send_json(fd, 200, "OK", "{\"text\":" + jstr(ps5tm_thread_snapshot_text()) + "}");
    }
    else if (req.method == "GET" && req.path == "/api/diag/probe")
    {
        send_json(fd, 200, "OK", "{\"text\":" + jstr(run_isolated(ps5tm_rpc_probe_text, 8, "check")) + "}");
    }
    else if (req.method == "GET" && req.path == "/api/diag/mounts")
    {
        send_json(fd, 200, "OK", "{\"text\":" + jstr(run_isolated(mounts_report_text, 8, "mounts")) + "}");
    }
    else if (req.method == "GET" && req.path == "/api/diag/disks")
    {
        send_json(fd, 200, "OK", "{\"text\":" + jstr(run_isolated(disks_report_text, 8, "disks")) + "}");
    }
    else if (req.method == "GET" && req.path == "/api/folders")
    {
        send_json(fd, 200, "OK", run_isolated(folders_json, 8, "folders"));
    }
    else if (req.method == "GET" && req.path == "/api/diag/log")
    {
        // from memory: includes lines that could not be written to disk (after wake-up the file system is unavailable)
        std::vector<char> buffer(40000);
        std::size_t const n = ps5tm_log_ring_copy(buffer.data(), buffer.size());
        send_response(fd, 200, "OK", "text/plain; charset=utf-8", std::string{ "# the wrapper log from memory (the last lines)\n" } + std::string{ buffer.data(), n });
    }
    else if (req.method == "GET" && req.path == "/api/diag/logfile")
    {
        // from the disk: also has the lines of other instances of the manager (for example a new one that could not replace the old one and exited)
        send_response(fd, 200, "OK", "text/plain; charset=utf-8", std::string{ "# the manager log file (logs/shim.log, the last lines; it also holds the starts of other instances)\n" } + read_file_tail("/data/Transmission/logs/shim.log", 30000));
    }
    else if (req.method == "GET" && req.path == "/api/diag/all")
    {
        std::vector<char> ring(7000);
        std::size_t const ring_len = ps5tm_log_ring_copy(ring.data(), ring.size());
        std::string text = std::string{ "=== version " } + ps5tm_version() + ", SDK " + ps5tm_sdk_version() + ", built " + ps5tm_build_time() + ", running " + std::to_string(ps5tm_uptime_seconds()) + " s\n";
        text += "\n=== Transmission check\n" + run_isolated(ps5tm_rpc_probe_text, 8, "check") + "\n";
        text += "\n=== disks\n" + run_isolated(disks_report_text, 8, "disks") + "\n";
        text += "\n=== threads\n" + ps5tm_thread_snapshot_text();
        text += "\n=== console processes and ports\n" + ps5tm_process_list_text();
        text += "\n=== wrapper log (from memory)\n" + std::string{ ring.data(), ring_len };
        text += "\n=== manager log file (shim.log, latest)\n" + read_file_tail("/data/Transmission/logs/shim.log", 8000);
        text += "\n=== Transmission log (latest)\n" + read_file_tail("/data/Transmission/logs/transmission.log", 3500);
        text += "\n=== daemon output (console.log, latest)\n" + read_file_tail("/data/Transmission/logs/console.log", 3500);
        text += "\n=== daemon wrapper log (daemon.log, latest)\n" + read_file_tail("/data/Transmission/logs/daemon.log", 3500);
        send_response(fd, 200, "OK", "text/plain; charset=utf-8", text);
    }
    else if (req.method == "GET" && req.path == "/api/notifications")
    {
        std::string j = "{\"latest\":" + std::to_string(ps5tm_notifications_latest()) + ",\"items\":[";
        bool first = true;
        for (auto const& n : ps5tm_notifications_snapshot())
        {
            j += std::string{ first ? "" : "," } + "{\"id\":" + std::to_string(n.id) + ",\"time\":" + std::to_string(n.time) + ",\"shown\":" + jbool(n.shown) + ",\"kind\":" + std::to_string(n.kind) + ",\"text\":" + jstr(n.text) + "}";
            first = false;
        }
        send_json(fd, 200, "OK", j + "]}");
    }
    else if (req.method == "GET" && req.path == "/api/diag/sleeplog")
    {
        // Sleep and daemon lifecycle events from the manager log file (it survives a manager restart)
        std::string const all = read_file_tail("/data/Transmission/logs/shim.log", 1500000);
        std::vector<std::string> keep;
        std::size_t pos = 0;
        while (pos < all.size())
        {
            std::size_t end = all.find('\n', pos);
            if (end == std::string::npos)
            {
                end = all.size();
            }
            std::string const line = all.substr(pos, end - pos);
            pos = end + 1;
            for (char const* key : { "sleep:", "frozen for", "resume detected", "daemon: start", "daemon: running", "daemon: paused", "daemon: SIGTERM", "daemon: restarting", "NOTIFY: Transmission stopped", "NOTIFY: Transmission started", "console will go to sleep", "====" })
            {
                if (line.find(key) != std::string::npos)
                {
                    keep.push_back(line);
                    break;
                }
            }
        }
        std::string text = "# sleep and daemon start/stop events from logs/shim.log (the last 200)\n";
        for (std::size_t i = keep.size() > 200 ? keep.size() - 200 : 0; i < keep.size(); ++i)
        {
            text += keep[i] + "\n";
        }
        send_response(fd, 200, "OK", "text/plain; charset=utf-8", text);
    }
    else if (req.method == "GET" && req.path == "/api/diag/dlog")
    {
        send_response(fd, 200, "OK", "text/plain; charset=utf-8", std::string{ "# daemon wrapper log (daemon.log, latest)\n" } + read_file_tail("/data/Transmission/logs/daemon.log", 8000));
    }
    else if (req.method == "GET" && req.path == "/api/diag/tlog")
    {
        send_response(
            fd,
            200,
            "OK",
            "text/plain; charset=utf-8",
            std::string{ "# the Transmission log itself (transmission.log, the last lines)\n" } +
                read_file_tail("/data/Transmission/logs/transmission.log", 12000));
    }
    else if (req.method == "POST" && (req.path == "/api/config" || req.path == "/api/quit" || req.path == "/api/sleep" || req.path == "/api/autosleep" || req.path == "/api/notifications" || req.path == "/api/folders" || req.path == "/api/fs" || req.path == "/api/daemon" || req.path == "/api/icon"))
    {
        if (!is_json(req))
        {
            send_json(fd, 415, "Unsupported Media Type", std::string{ "{\"ok\":false,\"message\":" } + jstr(ps5tm_tr("mr_need_json")) + "}");
        }
        else if (req.path == "/api/config")
        {
            handle_config(fd, req);
        }
        else if (req.path == "/api/daemon")
        {
            handle_daemon(fd, req);
        }
        else if (req.path == "/api/icon")
        {
            handle_icon(fd, req);
        }
        else if (req.path == "/api/quit")
        {
            bool const started = ps5tm_quit_start();
            send_json(fd, started ? 200 : 409, started ? "OK" : "Conflict", started ? std::string{ "{\"ok\":true}" } : std::string{ std::string{ "{\"ok\":false,\"message\":" } + jstr(ps5tm_tr("mr_quit_running")) + "}" });
        }
        else if (req.path == "/api/folders")
        {
            handle_folders_post(fd, req);
        }
        else if (req.path == "/api/fs")
        {
            handle_fs_post(fd, req);
        }
        else if (req.path == "/api/autosleep")
        {
            handle_autosleep(fd, req);
        }
        else if (req.path == "/api/notifications")
        {
            std::map<std::string, std::string> values;
            if (parse_flat_json(req.body, values) && values["action"] == "clear")
            {
                ps5tm_notifications_clear();
                send_json(fd, 200, "OK", "{\"ok\":true}");
            }
            else
            {
                send_json(fd, 400, "Bad Request", "{\"ok\":false,\"message\":\"action clear is needed\"}");
            }
        }
        else
        {
            handle_sleep(fd, req, client);
        }
    }
    else
    {
        send_response(fd, 404, "Not Found", "text/plain; charset=utf-8", "Not found\n");
    }
}

} // namespace ps5tm_ui

// The thread of the settings server and the public functions (declared in ps5tm_cfg.h)
using namespace ps5tm_ui;

extern "C" void* ps5tm_ui_thread(void*)
{
    if (!g_cfg.settings_server)
    {
        shim_log("settings server is disabled in config.cfg (settings_server=no)");
        return nullptr;
    }


    int listener = -1;
    int bound_port = 0;
    int bound_wanted = 0; // the settings_port value the socket was bound with
    int announced_port = 0;
    bool icon_startup_done = false;
    std::time_t next_try = 0;
    std::time_t rebind_at = 0;
    bool bind_failure_logged = false;
    unsigned resume_seen = ps5tm_resume_generation();
    std::string last_ip = ps5tm_ip();
    bool had_ip = true;
    std::time_t last_probe = std::time(nullptr);
    std::time_t last_selftest = 0;
    int probe_failures = 0;

    while (ps5tm_daemon_finished == 0)
    {
        try
        {
            auto const now = std::time(nullptr);

            // --- when the socket needs to be recreated: wake-up, network, port change
            unsigned const generation = ps5tm_resume_generation();
            if (generation != resume_seen)
            {
                resume_seen = generation;
                int const delay = g_cfg.wake_delay_seconds;
                rebind_at = now + (delay > 1 ? delay : 1);
                shim_log("settings server: wake-up detected: recreating the socket in %d s", delay > 1 ? delay : 1);
            }
            char ip[64];
            bool const have_ip = ps5tm_current_ip(ip, sizeof(ip)) == 0;
            if (had_ip && !have_ip)
            {
                had_ip = false;
                shim_log("settings server: network lost");
            }
            else if ((!had_ip && have_ip) || (have_ip && last_ip != ip))
            {
                had_ip = true;
                last_ip = ip;
                rebind_at = now + 1;
                shim_log("settings server: network is back or IP changed (%s): recreating the socket", ip);
            }
            int const wanted_port = g_cfg.settings_port;
            if (listener >= 0 && bound_wanted != wanted_port)
            {
                rebind_at = now;
                shim_log("settings server: the configured port %d -> %d", bound_wanted, wanted_port);
            }
            if (rebind_at != 0 && now >= rebind_at)
            {
                rebind_at = 0;
                if (listener >= 0)
                {
                    close(listener);
                    listener = -1;
                }
                next_try = 0;
            }

            // --- socket self-check every 15 seconds: recreate a dead socket
            if (listener >= 0 && now - last_probe >= 15)
            {
                last_probe = now;
                if (probe_listener(bound_port))
                {
                    probe_failures = 0;
                }
                else if (++probe_failures >= 2)
                {
                    shim_log("settings server: the socket does not accept connections: recreating it");
                    probe_failures = 0;
                    close(listener);
                    listener = -1;
                    next_try = 0;
                }
            }

            // --- check with a real HTTP request: 2 seconds after the socket starts and once a minute
            if (listener >= 0 && last_selftest != 0 && now - last_selftest >= 60)
            {
                last_selftest = now;
                http_selftest_in_background(bound_port);
            }

            // --- listening socket
            if (listener < 0 && now >= next_try)
            {
                int error = 0;
                int actual = 0;
                listener = make_listener_scan(wanted_port, wanted_port == bound_wanted ? bound_port : 0, actual, error);
                if (listener >= 0)
                {
                    bool const port_changed = bound_port != 0 && bound_port != actual;
                    bound_port = actual;
                    bound_wanted = wanted_port;
                    g_settings_port_actual = actual;
                    bind_failure_logged = false;
                    last_selftest = now - 58; // the first check in ~2 seconds
                    shim_log("settings server: listening on port %d%s", bound_port, actual != wanted_port ? " (the configured port is busy: the first free one is used)" : "");
                    if (actual != wanted_port && actual != announced_port)
                    {
                        announced_port = actual;
                        ps5tm_announce(g_cfg.notify_startup, ps5tm_tr("me_page_started", { std::string{ ps5tm_ip() } + ":" + std::to_string(actual) + "/", std::to_string(wanted_port) }).c_str());
                    }
                    // Icon: on first start (if the setting is on) and on a port change (the icon link contains the port)
                    if ((g_cfg.install_icon && !icon_startup_done) || (port_changed && icon_installed()))
                    {
                        icon_startup_done = true;
                        icon_start("install", port_changed ? "port changed" : "startup", false, true);
                    }
                }
                else
                {
                    next_try = now + 5;
                    if (!bind_failure_logged)
                    {
                        bind_failure_logged = true;
                        shim_log("settings server: cannot listen on port %d (errno %d): retrying every 5 s", wanted_port, error);
                        ps5tm_notify_text(ps5tm_tr("me_page_port", { std::to_string(wanted_port), std::to_string(error) }).c_str());
                    }
                }
            }

            // --- requests
            if (listener < 0)
            {
                sleep(1);
                continue;
            }
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(listener, &readable);
            timeval tv{};
            tv.tv_sec = 1;
            int const ready = select(listener + 1, &readable, nullptr, nullptr, &tv);
            if (ready < 0 && errno != EINTR)
            {
                // the socket was closed or broken from outside: recreate it, do not spin idle
                shim_log("settings server: select failed (errno %d): recreating the socket", errno);
                close(listener);
                listener = -1;
                next_try = now + 1;
                continue;
            }
            if (ready > 0 && FD_ISSET(listener, &readable))
            {
                sockaddr_in peer{};
                socklen_t peer_len = sizeof(peer);
                int const client = accept(listener, reinterpret_cast<sockaddr*>(&peer), &peer_len);
                if (client >= 0)
                {
#ifdef PS5TM_TEST_INHERIT_NONBLOCK
                    // Linux tests ONLY: emulate BSD behavior, where an accepted socket inherits O_NONBLOCK
                    fcntl(client, F_SETFL, fcntl(client, F_GETFL, 0) | O_NONBLOCK);
#endif
#ifndef PS5TM_TEST_SKIP_NONBLOCK_FIX
                    // On BSD (and on the PS5) an accepted socket inherits O_NONBLOCK from the listener: make it blocking
                    fcntl(client, F_SETFL, fcntl(client, F_GETFL, 0) & ~O_NONBLOCK);
#endif
                    char peer_ip[64] = "?";
                    inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip));
                    handle_connection(client, peer_ip);
                    close(client);
                }
            }
        }
        catch (std::exception const& e)
        {
            shim_log("settings server: exception: %s", e.what());
            sleep(1);
        }
    }

    if (listener >= 0)
    {
        close(listener);
    }
    return nullptr;
}

// The rest mode system call for the sleep controller in the monitor. A successful call may not return (the console
// enters sleep), so it runs in a separate thread with a time limit.
std::string ps5tm_enter_rest_mode()
{
    return run_isolated(enter_rest_mode_impl, 10, "sleep");
}

std::atomic<int> g_settings_port_actual{ 0 }; // the actual page port (declared in ps5tm_cfg.h)

