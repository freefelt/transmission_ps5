// Transmission for the PS5: the monitor thread: torrent events, disk space, quit.
//
// License: GPL-3.0-or-later (like Transmission).

#include "core_internal.h"

namespace ps5tm_core
{


static void maintenance_run()
{
    auto const now = std::time(nullptr);
    journal_prune(now);
    logs_rotate_if_needed(now);
    logs_prune(now);
}

static void emit(char const* label_key, std::vector<std::string> const& items, bool enabled)
{
    if (items.empty())
    {
        return;
    }

    std::string const label = ps5tm_tr(label_key);
    std::string text;
    if (items.size() == 1)
    {
        text = ps5tm_tr("me_ev_one", { label, clip(items.front(), 140) });
    }
    else
    {
        std::string names;
        constexpr std::size_t Shown = 3;
        for (std::size_t i = 0; i < items.size() && i < Shown; ++i)
        {
            names += (i == 0 ? "" : ", ") + clip(items[i], 40);
        }
        if (items.size() > Shown)
        {
            names += ps5tm_tr("me_ev_more", { std::to_string(items.size() - Shown) });
        }
        text = ps5tm_tr("me_ev_many", { label, std::to_string(items.size()), names });
    }
    announce(enabled, text);
}

struct SpaceState
{
    int level = 0; // 0 normal, 1 low space, 2 almost full: which level has already been reported
    int ok_streak = 0; // consecutive "clearly fine" checks (hysteresis to avoid flicker at the threshold)
};


// ---------------------------------------------------------------- full quit (button on the settings page)
// Cleanly stops Transmission (active torrents are paused and remembered: they resume on the next manager start),
// closes the system service helper and terminates the manager itself. If the daemon could not be stopped, the manager keeps running.
static std::atomic<bool> g_quitting{ false };

static void worker_quit()
{
    shim_log("quit: requested from the settings page");
    bool acquired = false;
    for (int i = 0; i < 90 && !acquired; ++i) // another daemon operation (start, stop) may still be in progress
    {
        acquired = !g_d.busy.exchange(true);
        if (!acquired)
        {
            sleep(1);
        }
    }
    int paused = 0;
    std::string error;
    if (!acquired)
    {
        error = ps5tm_tr("mr_d_busy");
    }
    else if (daemon_pid() > 0)
    {
        error = daemon_stop_graceful(paused);
    }
    if (!error.empty())
    {
        shim_log("quit: not exiting: %s", error.c_str());
        notify_startup_text(ps5tm_tr("me_quit_failed", { error }));
        if (acquired)
        {
            g_d.busy = false;
        }
        g_quitting = false;
        return;
    }
    notify_startup_text(ps5tm_tr("me_closed", { std::to_string(paused) }));
    sleep(2); // let the page receive the reply
    ps5tm_exit_now();
}

} // namespace ps5tm_core

using namespace ps5tm_core;


void ps5tm_maintenance_run()
{
    maintenance_run();
}

bool ps5tm_quit_start()
{
    if (g_quitting.exchange(true))
    {
        return false;
    }
    std::thread(worker_quit).detach();
    return true;
}

extern "C" void* ps5tm_monitor_thread(void*)
{
    std::string session_id;
    std::map<int, Known> known;
    std::map<std::string, SpaceState> space;
    std::time_t first_ok = 0;
    std::time_t last_space_check = 0;
    std::string last_ip = ps5tm_ip();
    bool baselined = false;
    bool warned = false;
    unsigned resume_seen = ps5tm_resume_generation();
    unsigned epoch_seen = g_d.epoch.load();
    std::map<int, bool> tracked; // auto-sleep: download (id) -> finished
    std::set<int> prev_downloading;
    bool had_ip = true; // assume there is network at startup
    int tick = 0;

    web_theme_apply(); // Transmission web interface theme (and the theme name for the daemon)
    maintenance_run(); // rotate logs and clean up old ones before starting the daemon
    std::time_t last_maintenance = std::time(nullptr);
    daemon_manager_start(); // pick up a running daemon or start a new one
    if (std::getenv("PS5TM_NO_SYS_HELPER") == nullptr)
    {
        std::thread{ syssvc_main }.detach(); // console system service (game, language, "keep awake", sleep)
    }
 // system service helper (detects the game): once per manager start

    while (ps5tm_daemon_finished == 0)
    {
        auto const before = std::time(nullptr);
        sleep(1);
        unsigned const resume_now = ps5tm_resume_generation();
        bool const resumed = resume_now != resume_seen;
        resume_seen = resume_now;
        auto const slept = std::time(nullptr) - before;
        g_stats.monitor_active = true;
        if (std::time(nullptr) - last_maintenance >= 600)
        {
            last_maintenance = std::time(nullptr);
            maintenance_run();
        }
        check_guard_events();
        if (slept >= ResumeGapSeconds)
        {
            shim_log("monitor: the process was frozen for %ld s (standby?)", static_cast<long>(slept));
        }
        if (resumed || slept >= ResumeGapSeconds)
        {
            shim_log("monitor: resume detected (SIGCONT / time gap)");
            syssvc_resumed(std::time(nullptr));
            net_event(3);
        }

        // Once a second: network state and daemon supervision (start, restart a hung one).
        bool have_ip = false;
        try
        {
            char cur_ip[64];
            have_ip = ps5tm_current_ip(cur_ip, sizeof(cur_ip)) == 0;
            if (had_ip && !have_ip)
            {
                shim_log("monitor: network lost");
                had_ip = false;
            }
            else if (!had_ip && have_ip)
            {
                shim_log("monitor: network is back (%s)", cur_ip);
                had_ip = true;
                last_ip = cur_ip;
                ps5tm_set_ip(cur_ip);
                g_d.unhealthy_since = 0; // restart the "daemon is not responding" count while the network is already working
                net_event(1);
            }
            else if (have_ip && last_ip != cur_ip)
            {
                shim_log("monitor: IP %s -> %s", last_ip.c_str(), cur_ip);
                last_ip = cur_ip;
                ps5tm_set_ip(cur_ip);
                net_event(2);
            }
            sleep_tick(static_cast<long>(std::time(nullptr)), have_ip, resumed || slept >= ResumeGapSeconds);
            supervise(have_ip);
            syssvc_watchdog(std::time(nullptr));
            game_pause_tick(std::time(nullptr));
            keep_awake_tick(std::time(nullptr));
        }
        catch (std::exception const& e)
        {
            shim_log("monitor: exception in the supervisor: %s", e.what());
        }

        if (epoch_seen != g_d.epoch.load()) // the daemon was restarted: the previous poll state is no longer valid
        {
            epoch_seen = g_d.epoch.load();
            known.clear();
            tracked.clear(); // torrent ids are different in the new daemon
            prev_downloading.clear();
            g_sl.track_reset = true;
            baselined = false;
            first_ok = 0;
            session_id.clear();
            tick = 0;
        }

        if (++tick < g_cfg.poll_interval_seconds)
        {
            continue;
        }
        tick = 0;
        std::fflush(nullptr);

        try
        {
            if (g_d.busy.load() || g_d.pid.load() == 0) // a start/stop is in progress or there is no daemon: nothing to poll
            {
                g_stats.rpc_ok = false;
                continue;
            }

            std::map<int, Torrent> current;
            if (!fetch(current, session_id))
            {
                g_stats.rpc_ok = false;
                daemon_note_rpc(false, have_ip);
                continue;
            }
            daemon_note_rpc(true, have_ip);

            auto const now = std::time(nullptr);
            {
                int downloading = 0;
                int seeding = 0;
                int paused = 0;
                for (auto const& entry : current)
                {
                    int const status = entry.second.status;
                    if (status == 3 || status == 4)
                    {
                        ++downloading;
                    }
                    else if (status == 5 || status == 6)
                    {
                        ++seeding;
                    }
                    else if (status == 0)
                    {
                        ++paused;
                    }
                }
                g_stats.total = static_cast<int>(current.size());
                g_stats.downloading = downloading;
                g_stats.seeding = seeding;
                g_stats.paused = paused;
                double down = 0, up = 0;
                for (auto const& entry : current)
                {
                    down += entry.second.rate_down;
                    up += entry.second.rate_up;
                }
                g_stats.down_bps = static_cast<long long>(down);
                g_stats.up_bps = static_cast<long long>(up);
                g_stats.last_poll = static_cast<long>(now);
                g_stats.rpc_ok = true;
            }
            // Auto-sleep: which downloads were running and whether they actually finished
            {
                std::set<int> now_downloading;
                for (auto const& [id, t] : current)
                {
                    if (t.status == 3 || t.status == 4)
                    {
                        now_downloading.insert(id);
                    }
                }
                if (g_sl.armed.load())
                {
                    if (g_sl.track_reset.exchange(false))
                    {
                        tracked.clear();
                        for (int const id : prev_downloading)
                        {
                            tracked.emplace(id, false);
                        }
                    }
                    for (int const id : now_downloading)
                    {
                        tracked.emplace(id, false); // track new downloads too
                    }
                    bool all = !tracked.empty();
                    for (auto& [id, done] : tracked)
                    {
                        auto const it = current.find(id);
                        if (it != current.end() && it->second.left <= 0 && it->second.status != 3 && it->second.status != 4)
                        {
                            done = true; // finished downloading (the mark stays even if the torrent is deleted later)
                        }
                        all = all && done;
                    }
                    g_sl.tracked = static_cast<int>(tracked.size());
                    g_sl.all_completed = all;
                }
                else
                {
                    tracked.clear();
                }
                prev_downloading = std::move(now_downloading);
            }
            if (first_ok == 0)
            {
                first_ok = now;
            }
            if (!baselined && now - first_ok >= QuietSeconds)
            {
                baselined = true;
                // baseline for hang analysis: how many threads a healthy process has
                shim_log("monitor: baseline (healthy):\n%s", ps5tm_thread_snapshot_text().c_str());
            }
            warned = false;

            std::vector<std::string> started, finished, errors, tracker_errors;
            std::map<int, Known> next;

            for (auto const& [id, t] : current)
            {
                auto it = known.find(id);
                bool const is_new = it == known.end();
                Known k = is_new ? Known{} : it->second;

                if (baselined)
                {
                    bool const was_stopped = is_new || it->second.t.status == 0;
                    if (was_stopped && t.status != 0)
                    {
                        started.push_back(t.name);
                    }
                    // On a local error the daemon zeroes "left to download"; this is not a completion.
                    if (k.downloading && it != known.end() && it->second.t.left > 0 && t.left <= 0 && t.error != 3)
                    {
                        finished.push_back(t.name);
                        k.downloading = false;
                    }
                }
                if (t.status == 4 && t.left > 0)
                {
                    k.downloading = true;
                }

                if ((t.error == 2 || t.error == 3) && !t.error_string.empty())
                {
                    auto const key = std::to_string(t.error) + ":" + t.error_string;
                    if (key != k.notified_error)
                    {
                        bool allow = baselined;
                        if (allow && t.error == 2 && now - k.last_tracker_notice < TrackerErrorCooldown)
                        {
                            allow = false;
                        }
                        if (allow)
                        {
                            (t.error == 2 ? tracker_errors : errors).push_back(t.name + ": " + t.error_string);
                            if (t.error == 2)
                            {
                                k.last_tracker_notice = now;
                            }
                        }
                        k.notified_error = key; // do not show the same message again
                    }
                }
                else
                {
                    k.notified_error.clear(); // the error is cleared: report it again if it recurs
                }

                k.t = t;
                next.emplace(id, std::move(k));
            }
            known = std::move(next);

            emit("me_lbl_started", started, g_cfg.notify_torrent_started);
            emit("me_lbl_finished", finished, g_cfg.notify_torrent_finished);
            emit("me_lbl_error", errors, g_cfg.notify_torrent_error);
            emit("me_lbl_tracker", tracker_errors, g_cfg.notify_tracker_error);

            // Free space: once a minute and only while something is downloading
            if (baselined && now - last_space_check >= SpaceCheckSeconds) // check even with the notification disabled: it goes to the journal
            {
                last_space_check = now;

                std::map<std::string, double> need; // folder -> how much still needs to be downloaded
                for (auto const& [id, t] : current)
                {
                    if ((t.status == 3 || t.status == 4) && t.left > 0 && !t.download_dir.empty())
                    {
                        need[t.download_dir] += t.left;
                    }
                }
                for (auto it = space.begin(); it != space.end();)
                {
                    it = need.count(it->first) != 0 ? std::next(it) : space.erase(it);
                }

                for (auto const& [dir, remaining] : need)
                {
                    double const free = free_bytes(dir, session_id);
                    if (free < 0)
                    {
                        continue;
                    }

                    int const level = (critical_bytes() > 0 && free < critical_bytes()) ? 2 : ((free < warn_bytes() || free < remaining) ? 1 : 0);
                    bool const clearly_ok = free >= warn_bytes() * 1.2 && free >= remaining * 1.1;
                    auto& st = space[dir];

                    if (level > st.level)
                    {
                        st.level = level;
                        st.ok_streak = 0;
                        std::string text = ps5tm_tr(level == 2 ? "me_disk_critical" : "me_disk_low", { clip(dir, 40), format_size(free) });
                        if (level == 1 || remaining > free)
                        {
                            text += ps5tm_tr("me_disk_need", { format_size(remaining) });
                        }
                        announce(g_cfg.notify_low_disk_space, text);
                    }
                    else if (level == 0 && clearly_ok)
                    {
                        if (++st.ok_streak >= 2)
                        {
                            st.level = 0;
                        }
                    }
                    else
                    {
                        st.ok_streak = 0;
                    }
                }
            }
        }
        catch (std::exception const& e)
        {
            if (!warned)
            {
                shim_log("monitor: exception: %s", e.what());
                warned = true;
            }
        }
    }

    return nullptr;
}
