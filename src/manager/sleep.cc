// Transmission for the PS5: sleep, auto sleep and power off.
//
// License: GPL-3.0-or-later (like Transmission).

#include "core_internal.h"

namespace ps5tm_core
{


// ---------------------------------------------------------------- sleep: by button and "after all downloads finish"

static std::string sleep_reason_text(int reason)
{
    if (reason == ReasonAllDone)
    {
        return "all downloads finished";
    }
    if (reason == ReasonTimeout)
    {
        return "time limit reached";
    }
    pthread_mutex_lock(&g_sleep_by_mutex);
    std::string by{ g_sleep_by };
    pthread_mutex_unlock(&g_sleep_by_mutex);
    return by.empty() ? std::string{ "requested from the settings page" } : "requested from " + by;
}

// Stopping Transmission before sleep: like the "Stop" button (active torrents are paused and
// remembered, the daemon receives a termination signal), but the desired state does not change: after
// waking up the daemon starts again and resumes the remembered torrents.
static std::string sleep_stop_daemon()
{
    bool acquired = false;
    for (int i = 0; i < 90 && !acquired; ++i) // another daemon operation (start, stop) may still be in progress
    {
        acquired = !g_d.busy.exchange(true);
        if (!acquired)
        {
            sleep(1);
        }
    }
    if (!acquired)
    {
        return ps5tm_tr("mr_d_busy");
    }
    std::string error;
    if (daemon_pid() > 0)
    {
        int paused = 0;
        error = daemon_stop_graceful(paused);
        if (error.empty())
        {
            notify_startup_text(ps5tm_tr("me_stopped_before_sleep", { std::to_string(paused) }));
        }
    }
    g_d.busy = false;
    return error;
}

// The console goes to sleep or shuts down without us (the PS button, the system menu, a timer of the console): the power state of the system says so (powerstate.cc). The
// payload keeps running in rest mode, so the daemon is stopped cleanly now, exactly as before the sleep that we request ourselves, and is not started until the state is
// "working" again. If our own sleep is already in progress it has stopped the daemon.
static std::atomic<bool> g_power_stopping{ false };

void sleep_power_enter(char const* reason)
{
    long const now = static_cast<long>(std::time(nullptr));
    bool const had_hold = g_sl.hold.exchange(true);
    if (!had_hold)
    {
        g_sl.hold_net_lost = false;
        g_sl.hold_since = now;
    }
    g_sl.hold_power = true;
    shim_log("sleep: the console goes to sleep or shuts down (%s)%s", reason, had_hold ? ": the daemon is being stopped by our own sleep" : "");
    if (!had_hold && g_sl.phase.load() == SleepIdle && !g_power_stopping.exchange(true))
    {
        std::thread(
            []
            {
                std::string const error = sleep_stop_daemon();
                if (!error.empty())
                {
                    shim_log("sleep: Transmission was not stopped cleanly: %s", error.c_str());
                }
                g_power_stopping = false;
            })
            .detach();
    }
}

void sleep_power_leave()
{
    long const now = static_cast<long>(std::time(nullptr));
    if (g_sl.hold_power.exchange(false))
    {
        g_sl.hold = false;
        g_d.launch_failures = 0; // a new wake-up: automatic attempts are allowed again
        g_d.launch_not_before = now + g_cfg.wake_delay_seconds.load();
        shim_log("sleep: the console is awake: Transmission will start again in %d s", g_cfg.wake_delay_seconds.load());
        journal_event(ps5tm_tr("me_j_wake_detected", { std::to_string(g_cfg.wake_delay_seconds.load()) }));
    }
    if (g_sl.armed.load() && g_sl.phase.load() == SleepIdle)
    {
        g_sl.armed = false; // the same rule as for any other wake-up: the elapsed time is not the same any more
        shim_log("sleep: the console woke up: auto-sleep switched off");
        journal_event(ps5tm_tr("me_j_woke"));
    }
    syssvc_resumed(now);
}

static void sleep_worker()
{
    int const reason = g_sl.reason.load();
    bool const poweroff = reason != ReasonManual && g_cfg.autosleep_action.load() == 1; // the "sleep now" button in diagnostics always means sleep
    std::string const why = sleep_reason_text(reason);
    int const delay = g_cfg.sleep_countdown_seconds;
    // The delay with a notification applies only to auto-sleep; the button in diagnostics sleeps at once
    if (reason != ReasonManual && g_cfg.sleep_notify_before && delay > 0)
    {
        g_sl.deadline = static_cast<long>(std::time(nullptr)) + delay;
        shim_log("sleep: countdown %d s before %s (%s)", delay, poweroff ? "power off" : "sleep", why.c_str());
        ps5tm_notify_text(ps5tm_tr(poweroff ? "me_will_poweroff" : "me_will_sleep", { std::to_string(delay), why }).c_str());
        while (static_cast<long>(std::time(nullptr)) < g_sl.deadline.load() && !g_sl.cancel.load())
        {
            sleep(1);
        }
        if (g_sl.cancel.exchange(false))
        {
            shim_log("sleep: cancelled");
            ps5tm_notify_text(ps5tm_tr(poweroff ? "me_off_cancelled" : "me_sleep_cancelled").c_str());
            g_sl.phase = SleepIdle;
            return;
        }
    }

    g_sl.phase = SleepStopping;
    g_sl.hold_net_lost = false;
    g_sl.hold_since = static_cast<long>(std::time(nullptr));
    g_sl.hold = true; // do not start the daemon until the console wakes up
    std::string const stop_error = sleep_stop_daemon();
    if (!stop_error.empty())
    {
        shim_log("sleep: Transmission was not stopped cleanly: %s", stop_error.c_str());
    }

    g_sl.phase = SleepEntering;
    g_sl.hold_since = static_cast<long>(std::time(nullptr));
    if (poweroff)
    {
        shim_log("sleep: requesting power off (%s)", why.c_str());
        journal_event(ps5tm_tr("me_j_poweroff", { why }));
        int const rc = ps5tm_power_off_syscall(); // a successful call does not return
        if (rc != 0)
        {
            shim_log("sleep: the power off call failed: %d", rc);
            ps5tm_notify_text(ps5tm_tr("me_poweroff_failed", { std::to_string(rc) }).c_str());
        }
        else
        {
            sleep(10); // the console should have powered off; if we are still alive, the power off did not happen
            shim_log("sleep: the console is still running 10 s after the power off call");
            ps5tm_notify_text(ps5tm_tr("me_not_off").c_str());
        }
        g_sl.hold = false; // the daemon will be started again
        g_sl.phase = SleepIdle;
        return;
    }
    shim_log("sleep: requesting rest mode (%s)", why.c_str());
    journal_event(ps5tm_tr("me_j_sleep", { why }));
    // A successful call may not return: the console enters sleep, which is not an error (run_isolated will then report a timeout).
    std::string error;
    if (g_cfg.sleep_method.load() == 0) // system request through the helper; if the helper does not respond, the direct method
    {
        std::string const via = ps5tm_sleep_via_system();
        if (via.empty())
        {
            shim_log("sleep: the system request was queued");
            journal_event(ps5tm_tr("me_j_sleep_sysreq"));
        }
        else
        {
            shim_log("sleep: the system request is unavailable (%s): using the direct method", via.c_str());
            journal_event(ps5tm_tr("me_j_sleep_sysreq_fail", { via }));
            error = ps5tm_enter_rest_mode();
        }
    }
    else
    {
        error = ps5tm_enter_rest_mode();
    }
    if (!error.empty() && error.find("did not return") == std::string::npos)
    {
        shim_log("sleep: %s", error.c_str());
        ps5tm_notify_text(ps5tm_tr("me_sleep_failed", { error }).c_str());
        g_sl.hold = false; // sleep failed: the daemon will be started again
    }
    g_sl.phase = SleepIdle;
}

static bool begin_sleep(int reason)
{
    int expected = SleepIdle;
    if (!g_sl.phase.compare_exchange_strong(expected, SleepCountdown))
    {
        return false; // already in progress
    }
    g_sl.reason = reason;
    g_sl.cancel = false;
    g_sl.deadline = 0;
    std::thread(sleep_worker).detach();
    return true;
}

// Once a second: auto-sleep ("all downloads finished" or timeout) and waiting for wake-up after the sleep call.
void sleep_tick(long now, bool have_ip, bool resumed)
{
    if (resumed && g_sl.armed.load() && g_sl.phase.load() == SleepIdle)
    {
        // The console woke up (including waking on its own, by its own timer): the elapsed time and "downloads" before sleep are no longer the same, and the accumulated
        // time could fire immediately as a timeout. Auto-sleep is switched off; it can be enabled again.
        g_sl.armed = false;
        shim_log("sleep: the console woke up: auto-sleep switched off");
        journal_event(ps5tm_tr("me_j_woke"));
    }
    if (g_sl.armed.load() && g_sl.phase.load() == SleepIdle)
    {
        int reason = -1;
        if (now >= g_sl.timeout_at.load())
        {
            reason = ReasonTimeout;
        }
        else if (g_stats.rpc_ok.load())
        {
            if (g_stats.downloading.load() > 0)
            {
                g_sl.idle_since = 0;
            }
            else if (g_sl.idle_since.load() == 0)
            {
                g_sl.idle_since = now;
            }
            else if (now - g_sl.idle_since.load() >= 10) // no active downloads for 10 seconds already
            {
                if (g_sl.tracked.load() > 0 && g_sl.all_completed.load())
                {
                    reason = ReasonAllDone; // all tracked downloads have really finished
                }
                else
                {
                    // paused or removed: leave the console alone, just clear the checkbox
                    g_sl.armed = false;
                    shim_log("sleep: auto-sleep switched off: the downloads were paused or removed, not finished");
                    journal_event(ps5tm_tr("me_j_deact_self"));
                }
            }
        }
        if (reason >= 0)
        {
            g_sl.armed = false; // the task is done; it is not persisted
            char body[240];
            std::snprintf(
                body,
                sizeof(body),
                "auto-sleep triggered (%s): armed %ld min ago, now %d downloading / %d seeding, Transmission RPC %s",
                reason == ReasonTimeout ? "timeout" : "all downloads finished",
                (now - g_sl.armed_at.load()) / 60,
                g_stats.downloading.load(),
                g_stats.seeding.load(),
                g_stats.rpc_ok.load() ? "answers" : "does NOT answer");
            shim_log("sleep: %s", body);
            journal_event(ps5tm_tr("me_j_sleep_body", { body }));
            begin_sleep(reason);
        }
    }

    // After the sleep call do not start the daemon until the console wakes up: wake-up is visible by SIGCONT/a time jump
    // or by the network coming back (in rest mode the network disappears). If the network never went away, sleep did not happen.
    // (a hold that comes from the power state ends when the state is "working" again; only if the monitor of the power state is silent for 2 minutes the network decides)
    int const phase = g_sl.phase.load();
    bool const power_decides = g_sl.hold_power.load() && power_poll_age() >= 0 && power_poll_age() < 120;
    if (g_sl.hold.load() && !power_decides && (phase == SleepEntering || phase == SleepIdle))
    {
        if (!have_ip)
        {
            g_sl.hold_net_lost = true;
        }
        if (resumed || (have_ip && g_sl.hold_net_lost.load()))
        {
            g_sl.hold = false;
            g_d.launch_failures = 0; // a new wake-up: automatic attempts are allowed again
            g_d.launch_not_before = now + g_cfg.wake_delay_seconds.load();
            shim_log("sleep: wake-up detected: Transmission will start again in %d s", g_cfg.wake_delay_seconds.load());
            journal_event(ps5tm_tr("me_j_wake_detected", { std::to_string(g_cfg.wake_delay_seconds.load()) }));
        }
        else if (!g_sl.hold_net_lost.load() && now - g_sl.hold_since.load() >= 180)
        {
            g_sl.hold = false;
            shim_log("sleep: the console is still awake 3 minutes after the sleep call: Transmission will start again");
            ps5tm_notify_text(ps5tm_tr("me_not_slept").c_str());
        }
    }
}

} // namespace ps5tm_core

using namespace ps5tm_core;


// ---------------------------------------------------------------- sleep: state and actions for the settings page

Ps5tmSleepInfo ps5tm_sleep_info()
{
    long const now = static_cast<long>(std::time(nullptr));
    Ps5tmSleepInfo info;
    int const phase = g_sl.phase.load();
    info.phase = phase == SleepCountdown ? "countdown" : phase == SleepStopping ? "stopping" : phase == SleepEntering ? "entering" : "idle";
    info.remaining = phase == SleepCountdown ? std::max(0L, g_sl.deadline.load() - now) : 0;
    info.manual = phase != SleepIdle && g_sl.reason.load() == ReasonManual;
    info.armed = g_sl.armed.load();
    info.downloading = g_stats.downloading.load();
    info.can_arm = g_stats.rpc_ok.load() && info.downloading > 0;
    info.timeout_minutes = g_sl.timeout_minutes.load();
    info.timeout_remaining = info.armed ? std::max(0L, g_sl.timeout_at.load() - now) : 0;
    info.holding = g_sl.hold.load();
    return info;
}

std::string ps5tm_sleep_request(char const* requested_by)
{
    pthread_mutex_lock(&g_sleep_by_mutex);
    std::snprintf(g_sleep_by, sizeof(g_sleep_by), "%s", requested_by);
    pthread_mutex_unlock(&g_sleep_by_mutex);
    return begin_sleep(ReasonManual) ? std::string{} : std::string{ ps5tm_tr("mr_sleep_planned") };
}

bool ps5tm_sleep_cancel()
{
    if (g_sl.phase.load() == SleepCountdown)
    {
        g_sl.cancel = true;
        return true;
    }
    return false;
}

std::string ps5tm_autosleep_arm(int timeout_minutes)
{
    if (timeout_minutes <= 0)
    {
        timeout_minutes = g_cfg.autosleep_timeout_minutes.load(); // by default the timeout from the settings
    }
    if (timeout_minutes < 1 || timeout_minutes > 1440)
    {
        return ps5tm_tr("mr_timeout_range");
    }
    if (g_sl.phase.load() != SleepIdle)
    {
        return ps5tm_tr("mr_sleep_planned");
    }
    if (!(g_stats.rpc_ok.load() && g_stats.downloading.load() > 0))
    {
        return ps5tm_tr("mr_no_active");
    }
    g_sl.timeout_minutes = timeout_minutes;
    g_sl.timeout_at = static_cast<long>(std::time(nullptr)) + static_cast<long>(timeout_minutes) * 60;
    g_sl.idle_since = 0;
    g_sl.tracked = 0;
    g_sl.all_completed = false;
    g_sl.track_reset = true; // the monitor will remember which downloads are running now
    g_sl.armed_at = static_cast<long>(std::time(nullptr));
    g_sl.armed = true;
    shim_log("sleep: auto-sleep armed: %s when all downloads finish, or after %d min at the latest (%d downloading now)", g_cfg.autosleep_action.load() == 1 ? "power off" : "sleep", timeout_minutes, g_stats.downloading.load());
    journal_event(ps5tm_tr("me_j_armed", { ps5tm_tr(g_cfg.autosleep_action.load() == 1 ? "me_w_poweroff" : "me_w_sleep"), std::to_string(timeout_minutes), std::to_string(g_stats.downloading.load()) }));
    return "";
}

void ps5tm_autosleep_disarm()
{
    if (g_sl.armed.exchange(false))
    {
        shim_log("sleep: auto-sleep disarmed");
        journal_event(ps5tm_tr("me_j_disarmed"));
    }
}

std::string ps5tm_autosleep_set_timeout(int minutes)
{
    if (minutes < 1 || minutes > 1440)
    {
        return ps5tm_tr("mr_timeout_range");
    }
    if (!g_sl.armed.load())
    {
        return ps5tm_tr("mr_not_enabled");
    }
    g_sl.timeout_minutes = minutes;
    g_sl.timeout_at = static_cast<long>(std::time(nullptr)) + static_cast<long>(minutes) * 60; // count from the current moment
    shim_log("sleep: auto-sleep timeout set to %d min", minutes);
    journal_event(ps5tm_tr("me_j_limit", { std::to_string(minutes) }));
    return "";
}
