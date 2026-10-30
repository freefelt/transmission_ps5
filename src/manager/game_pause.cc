// Transmission for the PS5: pause of the downloads while a game runs, keep awake.
//
// License: GPL-3.0-or-later (like Transmission).

#include "core_internal.h"

namespace ps5tm_core
{


// ---------------------------------------------------------------- pausing downloads during a game and auto-starting the trial system service helper
// The game state is provided by the transmission_s.elf helper (port 1234, status.json): none, on_screen, minimised. The manager starts it on its own
// startup (once per start; a hung helper is not restarted by itself, so processes do not multiply) and pauses active torrents
// according to the game_pause setting. What was paused is written to GamePausedFile (lines "R<hash>"): after the pause is lifted only
// those are resumed; torrents stopped by the user stay stopped.

constexpr int GamePauseDelaySeconds = 5; // the game must stay on screen for this many seconds in a row before downloads are paused (when choosing "Close" in the game menu, "on screen" flashes for a couple of seconds)
constexpr int GameResumeDelaySeconds = 30; // after the game is closed (or minimised), downloads resume only after this many seconds: so they do not jump back and forth when entering the settings or starting another game

struct GamePause
{
    bool initialized = false;
    bool paused = false; // the pause is set and not yet lifted (the list is in GamePausedFile)
    unsigned applied_epoch = 0; // which daemon run the pause was applied to (after a daemon restart the torrents may have started again)
    std::time_t last_poll = 0;
    std::time_t lost_since = 0; // since when the helper has not been responding while the pause is set
    std::time_t pause_since = 0; // since when the game is on screen (or running, depending on the setting): downloads are paused after GamePauseDelaySeconds
    std::time_t resume_since = 0; // since when the pause condition is gone (the game was closed or minimised): downloads resume after GameResumeDelaySeconds
    std::string session_id;
};
static GamePause g_gp;

static std::vector<std::string> read_game_paused()
{
    std::vector<std::string> hashes;
    FILE* f = std::fopen(GamePausedFile, "r");
    if (f == nullptr)
    {
        return hashes;
    }
    char line[128];
    while (std::fgets(line, sizeof(line), f) != nullptr)
    {
        std::string const h = trim(line);
        if (h.size() > 1 && h[0] == 'R')
        {
            hashes.push_back(h.substr(1));
        }
    }
    std::fclose(f);
    return hashes;
}

// Pauses active torrents and appends them to the list. Returns the number paused, -1 = the torrent list was not received,
// -2 = the list could not be written (then nothing is stopped: what was not remembered must not stay paused forever).
static int game_pause_now()
{
    std::vector<TorrentLite> all;
    if (!fetch_lite(all, g_gp.session_id))
    {
        return -1;
    }
    std::set<std::string> remembered;
    for (auto const& h : read_game_paused())
    {
        remembered.insert(h);
    }
    std::vector<int> ids;
    for (auto const& t : all)
    {
        if (t.status != 0)
        {
            ids.push_back(t.id);
            remembered.insert(t.hash);
        }
    }
    std::string list;
    for (auto const& h : remembered)
    {
        list += "R" + h + "\n";
    }
    if (!list.empty() && !write_text_file(GamePausedFile, list))
    {
        shim_log("game pause: cannot save the list of paused torrents (errno %d): leaving them running", errno);
        return -2;
    }
    if (!ids.empty())
    {
        Json reply;
        rpc_call(std::string{ R"({"method":"torrent-stop","arguments":{"ids":)" } + ids_array(ids) + "}}", reply, g_gp.session_id);
    }
    return static_cast<int>(ids.size());
}

// Resumes what we paused (and what is still paused), removes the list. Returns the number resumed, -1 = failed (the list remains).
static int game_resume_now()
{
    auto const hashes = read_game_paused();
    if (hashes.empty())
    {
        std::remove(GamePausedFile);
        return 0;
    }
    std::vector<TorrentLite> all;
    if (!fetch_lite(all, g_gp.session_id))
    {
        return -1;
    }
    std::vector<int> ids;
    for (auto const& t : all)
    {
        if (t.status == 0 && std::find(hashes.begin(), hashes.end(), t.hash) != hashes.end())
        {
            ids.push_back(t.id);
        }
    }
    if (!ids.empty())
    {
        Json reply;
        rpc_call(std::string{ R"({"method":"torrent-start","arguments":{"ids":)" } + ids_array(ids) + "}}", reply, g_gp.session_id);
    }
    std::remove(GamePausedFile);
    return static_cast<int>(ids.size());
}

void game_pause_tick(std::time_t now)
{
    int const mode = g_cfg.game_pause.load();
    if (!g_gp.initialized)
    {
        g_gp.initialized = true;
        g_gp.paused = file_size_of(GamePausedFile) >= 0; // a pause not lifted since last time (manager restart)
    }
    if (mode == 0 && !g_gp.paused)
    {
        return;
    }
    if (now - g_gp.last_poll < 3)
    {
        return;
    }
    g_gp.last_poll = now;
    if (daemon_pid() == 0)
    {
        return; // no daemon: once it starts, the pause will be applied or lifted
    }
    GameState const st = mode == 0 ? GameState::None : syssvc_game_state();
    if (st == GameState::Unavailable)
    {
        if (g_gp.paused)
        {
            if (g_gp.lost_since == 0)
            {
                g_gp.lost_since = now;
            }
            if (now - g_gp.lost_since >= 60) // the helper disappeared while the pause is set: the torrents must not stay paused forever
            {
                int const n = game_resume_now();
                if (n >= 0)
                {
                    g_gp.paused = false;
                    g_gp.lost_since = 0;
                    shim_log("game pause: the game state is unknown: resumed %d torrent(s)", n);
                    if (n > 0)
                    {
                        announce(g_cfg.notify_game_pause, ps5tm_tr("mr_gp_helper_lost", { std::to_string(n) }));
                    }
                }
            }
        }
        return;
    }
    g_gp.lost_since = 0;
    bool const want = mode == 1 ? st == GameState::OnScreen : (st == GameState::OnScreen || st == GameState::Minimised || st == GameState::Unknown);
    unsigned const epoch = g_d.epoch.load();
    if (want)
    {
        g_gp.resume_since = 0; // the game came back on screen during the delay: the resume is cancelled
    }
    else
    {
        g_gp.pause_since = 0; // the game disappeared before the delay before pausing expired
    }
    if (want && !g_gp.paused)
    {
        if (g_gp.pause_since == 0)
        {
            g_gp.pause_since = now;
        }
        if (now - g_gp.pause_since < GamePauseDelaySeconds)
        {
            return;
        }
    }
    if (want && (!g_gp.paused || g_gp.applied_epoch != epoch))
    {
        int const n = game_pause_now();
        if (n >= 0)
        {
            bool const first = !g_gp.paused;
            g_gp.paused = true;
            g_gp.pause_since = 0;
            g_gp.applied_epoch = epoch;
            shim_log("game pause: game %s: paused %d torrent(s)", st == GameState::OnScreen ? "on screen" : st == GameState::Minimised ? "minimised" : "running", n);
            if (n > 0 && first)
            {
                announce(g_cfg.notify_game_pause, ps5tm_tr(st == GameState::OnScreen ? "mr_gp_paused_screen" : st == GameState::Minimised ? "mr_gp_paused_min" : "mr_gp_paused_run", { std::to_string(n) }));
            }
        }
    }
    else if (!want && g_gp.paused)
    {
        if (mode != 0) // turning the setting off lifts the pause at once; otherwise we wait GameResumeDelaySeconds in a row
        {
            if (g_gp.resume_since == 0)
            {
                g_gp.resume_since = now;
            }
            if (now - g_gp.resume_since < GameResumeDelaySeconds)
            {
                return;
            }
        }
        int const n = game_resume_now();
        if (n >= 0)
        {
            g_gp.paused = false;
            g_gp.resume_since = 0;
            shim_log("game pause: resumed %d torrent(s)", n);
            if (n > 0)
            {
                announce(g_cfg.notify_game_pause, ps5tm_tr(mode == 0 ? "mr_gp_resumed_off" : st == GameState::None ? "mr_gp_resumed_closed" : "mr_gp_resumed_min", { std::to_string(n) }));
            }
        }
    }
}

// "Do not let the console fall asleep by itself": only while auto-sleep/auto power off is armed (the "Activate" button), we ask the helper every 10 minutes
// to call PowerTick and reset the auto power off timer (the minimum sleep timer in the console settings is 20 minutes). Outside arming we leave the system alone.
// We check the state every 5 s via the helper's status.json: after it restarts (for example, after sleep) the request repeats by itself.
static bool g_ka_state = false;

void keep_awake_tick(std::time_t)
{
    bool const want = g_cfg.keep_awake.load() && g_sl.armed.load() && g_sl.phase.load() == SleepIdle; // only while auto-sleep or auto power off is armed
    g_ss_want_ka = want;
    if (want != g_ka_state)
    {
        g_ka_state = want;
        shim_log("keep awake: %s", want ? "turned on (auto sleep is active)" : "turned off");
    }
}

} // namespace ps5tm_core

using namespace ps5tm_core;


bool ps5tm_game_pause_active()
{
    return g_gp.paused;
}
