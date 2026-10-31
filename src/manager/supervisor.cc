// Transmission for the PS5: the daemon supervisor: launch, stop, restart, network changes.
//
// License: GPL-3.0-or-later (like Transmission).

#include "core_internal.h"

#include <chrono>

namespace ps5tm_core
{


// ---------------------------------------------------------------- Transmission daemon: start, stop, supervision


// Atomics only: initialized by a constant, there is no constructor when the ELF loads
DaemonShared g_d;



// Atomics and a statically initialized mutex only: there are no constructors when the ELF loads
SleepShared g_sl;
char g_sleep_by[64] = "";
pthread_mutex_t g_sleep_by_mutex = PTHREAD_MUTEX_INITIALIZER;

static char g_last_error[240] = ""; // why the last daemon start failed (for the settings page)
static pthread_mutex_t g_last_error_mutex = PTHREAD_MUTEX_INITIALIZER;

static void set_last_error(std::string const& text)
{
    pthread_mutex_lock(&g_last_error_mutex);
    std::snprintf(g_last_error, sizeof(g_last_error), "%s", text.c_str());
    pthread_mutex_unlock(&g_last_error_mutex);
}

constexpr int MaxAutoLaunchFailures = 3;
constexpr char const* DaemonPortFile = "/data/Transmission/transmission/daemon.port"; // port for the daemon (it reads this at startup)
constexpr char const* DaemonFile = "/data/Transmission/transmission/transmission_d.elf";
constexpr char const* DesiredFile = "/data/Transmission/daemon.desired";
constexpr char const* PausedFile = "/data/Transmission/paused_by_stop.txt";

static void set_state(int state)
{
    if (g_d.state.exchange(state) != state)
    {
        g_d.since = static_cast<long>(std::time(nullptr));
    }
}

static char const* state_name(int state)
{
    switch (state)
    {
    case DaemonStarting:
        return "starting";
    case DaemonRunning:
        return "running";
    case DaemonStopping:
        return "stopping";
    case DaemonNotResponding:
        return "not_responding";
    case DaemonRestarting:
        return "restarting";
    default:
        return "stopped";
    }
}

// The desired state survives a manager and console restart: after a manual stop the daemon does not start by itself.
static bool desired_load()
{
    FILE* f = std::fopen(DesiredFile, "r");
    if (f == nullptr)
    {
        return true; // first start: Transmission should be running
    }
    char buf[32] = {};
    bool const has = std::fgets(buf, sizeof(buf), f) != nullptr;
    std::fclose(f);
    return !(has && std::strncmp(buf, "stopped", 7) == 0);
}

static void desired_save(bool running)
{
    g_d.desired_running = running;
    if (!write_text_file(DesiredFile, running ? "running\n" : "stopped\n"))
    {
        shim_log("daemon: cannot save the desired state (errno %d)", errno);
    }
}

// Daemon pid. If the process list itself could not be read (this happens after wake-up), it does not mean the daemon
// died: return the previous value and draw no conclusions.
int daemon_pid()
{
    int const pid = ps5tm_find_pid(DaemonName);
    if (pid < 0)
    {
        static long last_log = 0;
        long const now = static_cast<long>(std::time(nullptr));
        if (now - last_log >= 60)
        {
            last_log = now;
            shim_log("daemon: the process list could not be read (errno %d): keeping the previous state", errno);
        }
        return g_d.pid.load();
    }
    g_d.pid = pid;
    return pid;
}

// Whether a process with this name and pid still exists. If the list fails, assume it exists (safer than deciding it is gone).
static bool process_is(char const* name, int pid)
{
    int const found = ps5tm_find_pid(name);
    return found < 0 || found == pid;
}

void notify_startup_text(std::string const& text)
{
    announce(g_cfg.notify_startup, text);
}

// Whether there is an RPC answer from the daemon over loopback (a real request, not just a connection)
static bool rpc_alive(std::string& session_id)
{
    Json root;
    return rpc_call(R"({"method":"session-get","arguments":{"fields":["version"]}})", root, session_id);
}


bool fetch_lite(std::vector<TorrentLite>& out, std::string& session_id)
{
    Json root;
    if (!rpc_call(R"({"method":"torrent-get","arguments":{"fields":["id","hashString","status"]}})", root, session_id))
    {
        return false;
    }
    auto const* args = root.get("arguments");
    auto const* list = args != nullptr ? args->get("torrents") : nullptr;
    if (list == nullptr || list->type != Json::Type::Array)
    {
        return false;
    }
    out.clear();
    for (auto const& item : list->items)
    {
        auto const* id = item.get("id");
        auto const* hash = item.get("hashString");
        auto const* status = item.get("status");
        if (id == nullptr || hash == nullptr || status == nullptr)
        {
            continue;
        }
        TorrentLite t;
        t.id = static_cast<int>(id->number);
        t.hash = hash->str;
        t.status = static_cast<int>(status->number);
        out.push_back(std::move(t));
    }
    return true;
}

// Whether the path lies inside the root (the root itself or a subfolder)
static bool path_is_under(std::string const& p, std::string const& root)
{
    return p == root || (p.size() > root.size() && p.compare(0, root.size(), root) == 0 && p[root.size()] == '/');
}

std::string ids_array(std::vector<int> const& ids)
{
    std::string s = "[";
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        s += (i ? "," : "") + std::to_string(ids[i]);
    }
    return s + "]";
}

// Pauses the active torrents and remembers which ones, so that after the next start only they are resumed:
// torrents the user paused themselves stay paused. Two groups go into the file:
// lines "R<hash>": were active, paused by the stop, resumed after the start;
// lines "P<hash>": were already paused, must stay paused after the start (Transmission starts by itself a
// torrent added while paused and never started: it has no state file).
// Returns the number paused; -1 = the list could not be obtained, -2 = the list could not be written (then
// nothing is paused: torrents we could not remember must not stay paused forever).
static int pause_active_and_remember(std::string& session_id)
{
    std::vector<TorrentLite> all;
    if (!fetch_lite(all, session_id))
    {
        return -1;
    }
    std::vector<int> ids;
    std::string list;
    for (auto const& t : all)
    {
        if (t.status != 0)
        {
            ids.push_back(t.id);
            list += "R" + t.hash + "\n";
        }
        else
        {
            list += "P" + t.hash + "\n";
        }
    }
    if (list.empty())
    {
        std::remove(PausedFile);
        return 0;
    }
    if (!write_text_file(PausedFile, list))
    {
        shim_log("daemon: cannot save the list of torrents to resume (errno %d): leaving them running", errno);
        return -2;
    }

    if (ids.empty())
    {
        return 0; // nothing active: the "P" list is written, there is nothing to pause
    }
    Json reply;
    rpc_call(std::string{ R"({"method":"torrent-stop","arguments":{"ids":)" } + ids_array(ids) + "}}", reply, session_id);
    for (int i = 0; i < 15; ++i) // wait until all stop (pausing saves the state)
    {
        sleep(1);
        std::vector<TorrentLite> now_state;
        if (!fetch_lite(now_state, session_id))
        {
            break;
        }
        bool all_stopped = true;
        for (auto const& t : now_state)
        {
            for (int const id : ids)
            {
                if (t.id == id && t.status != 0)
                {
                    all_stopped = false;
                }
            }
        }
        if (all_stopped)
        {
            break;
        }
    }
    return static_cast<int>(ids.size());
}

// Resumes the torrents paused during the stop (group R in PausedFile), returns to paused those that
// were paused (group P) but were started by Transmission itself, and removes the list. Returns the number resumed.
static int resume_remembered(std::string& session_id)
{
    FILE* f = std::fopen(PausedFile, "r");
    if (f == nullptr)
    {
        return 0;
    }
    std::vector<std::string> hashes; // R: resume
    std::vector<std::string> keep_paused; // P: must stay paused
    char line[128];
    while (std::fgets(line, sizeof(line), f) != nullptr)
    {
        std::string h = trim(line);
        if (h.size() > 1 && h[0] == 'R')
        {
            hashes.push_back(h.substr(1));
        }
        else if (h.size() > 1 && h[0] == 'P')
        {
            keep_paused.push_back(h.substr(1));
        }
    }
    std::fclose(f);

    std::vector<TorrentLite> all;
    if (!fetch_lite(all, session_id))
    {
        return 0; // the list remains: try again on the next start
    }
    std::vector<int> ids;
    for (auto const& t : all)
    {
        for (auto const& h : hashes)
        {
            if (t.hash == h && t.status == 0)
            {
                ids.push_back(t.id);
            }
        }
    }
    if (!ids.empty())
    {
        Json reply;
        rpc_call(std::string{ R"({"method":"torrent-start","arguments":{"ids":)" } + ids_array(ids) + "}}", reply, session_id);
    }
    std::vector<int> repause;
    for (auto const& t : all)
    {
        for (auto const& h : keep_paused)
        {
            if (t.hash == h && t.status != 0)
            {
                repause.push_back(t.id);
            }
        }
    }
    if (!repause.empty())
    {
        shim_log("daemon: returning %d torrent(s) to the paused state (Transmission started them by itself)", static_cast<int>(repause.size()));
        Json reply;
        rpc_call(std::string{ R"({"method":"torrent-stop","arguments":{"ids":)" } + ids_array(repause) + "}}", reply, session_id);
    }
    std::remove(PausedFile);
    return static_cast<int>(ids.size());
}

long file_size_of(char const* path)
{
    struct stat st;
    return stat(path, &st) == 0 ? static_cast<long>(st.st_size) : -1;
}

static std::string read_from(char const* path, long from, std::size_t max_bytes)
{
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr)
    {
        return "";
    }
    std::fseek(f, from < 0 ? 0 : from, SEEK_SET);
    std::string data(max_bytes, '\0');
    data.resize(std::fread(&data[0], 1, max_bytes, f));
    std::fclose(f);
    return data;
}

// The daemon did not appear after the ELF was sent to the loader: distinguish "the loader did not run the ELF" from "the process started and died
// immediately" (a start line appeared in daemon.log), and also log the ports and the process list.
static void diag_launch_failure(long daemon_log_before)
{
    long const after = file_size_of(DaemonLog);
    std::string const fresh = after > daemon_log_before ? read_from(DaemonLog, daemon_log_before, 1500) : std::string{};
    if (fresh.find("==== Transmission daemon") != std::string::npos)
    {
        shim_log("diag: the loader STARTED the daemon, but it died. daemon.log after the launch:\n%s", fresh.c_str());
    }
    else
    {
        shim_log("diag: the loader did NOT run the ELF: no new start in daemon.log (%ld -> %ld bytes)", daemon_log_before, after);
    }
    shim_log("diag: %s", ps5tm_process_list_text().c_str());
}

// Starts the daemon (its ELF is sent to the loader) and waits until it answers over RPC. "" = ready, otherwise the reason.
static std::string daemon_launch_and_wait()
{
    g_d.last_launch_attempt = static_cast<long>(std::time(nullptr));
    if (daemon_pid() == 0 && ps5tm_port_in_use(rpc_port()))
    {
        return ps5tm_tr("mr_port_busy", { std::to_string(rpc_port()) });
    }
    // The daemon gets the port through a file (the loader does not pass arguments)
    g_d.launched_port = rpc_port();
    if (!write_text_file(DaemonPortFile, std::to_string(rpc_port()) + "\n"))
    {
        shim_log("daemon: cannot write %s (errno %d): the daemon will use the default port", DaemonPortFile, errno);
    }
    set_state(DaemonStarting);
    char err[220] = {};
    long const log_before = file_size_of(DaemonLog);
    auto const launch_t0 = std::chrono::steady_clock::now();
    auto const since_launch = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - launch_t0).count(); };
    shim_log("daemon: launching %s", DaemonFile);
    if (ps5tm_launch_elf(DaemonFile, ps5tm_daemon_size(), err, sizeof(err)) != 0)
    {
        set_state(DaemonStopped);
        std::string text{ err };
        if (text.find("cannot open") != std::string::npos || text.find("is corrupt") != std::string::npos)
        {
            text += ps5tm_tr("mr_d_unpack_again");
        }
        return text;
    }
    g_d.launched_at = static_cast<long>(std::time(nullptr));
    shim_log("daemon: the ELF was handed to the loader in %.1f s", since_launch()); // where the seconds of a slow start go: the loader, the process, or the daemon itself

    int pid = 0;
    for (int i = 0; i < 30 && pid == 0; ++i)
    {
        sleep(1);
        pid = daemon_pid();
    }
    shim_log("daemon: the process %s %.1f s after the launch", pid != 0 ? "appeared" : "did not appear", since_launch());
    if (pid == 0)
    {
        set_state(DaemonStopped);
        diag_launch_failure(log_before);
        return ps5tm_tr("mr_d_noproc");
    }

    std::string session_id;
    for (int i = 0; i < 90; ++i)
    {
        if (rpc_alive(session_id))
        {
            g_d.unhealthy_since = 0;
            g_d.epoch++;
            set_state(DaemonRunning);
            shim_log("daemon: running (pid %d); RPC answered %.1f s after the launch", pid, since_launch());
            return "";
        }
        sleep(1);
        if (daemon_pid() == 0)
        {
            set_state(DaemonStopped);
            diag_launch_failure(log_before);
            return ps5tm_tr("mr_d_died");
        }
    }
    set_state(DaemonNotResponding);
    return ps5tm_tr("mr_d_norpc");
}

// Waits until the process with this name disappears (no longer than seconds seconds). true = it is gone.
static bool wait_process_gone(char const* name, int pid, int seconds)
{
    for (int i = 0; i < seconds && process_is(name, pid); ++i)
    {
        sleep(1);
    }
    return !process_is(name, pid);
}

// Termination signal: SIGTERM first (Transmission closes the session), SIGKILL after grace seconds.
static bool terminate_process(char const* name, int grace_seconds)
{
    int const pid = ps5tm_find_pid(name);
    if (pid <= 0)
    {
        return true;
    }
    shim_log("daemon: SIGTERM to %s pid %d (waiting up to %d s)", name, pid, grace_seconds);
    auto const term_t0 = std::chrono::steady_clock::now();
    kill(pid, SIGTERM);
    if (!wait_process_gone(name, pid, grace_seconds))
    {
        shim_log("daemon: pid %d did not exit within %d s: SIGKILL", pid, grace_seconds);
        kill(pid, SIGKILL);
        if (!wait_process_gone(name, pid, 10))
        {
            shim_log("daemon: pid %d is still there after SIGKILL", pid);
            return false;
        }
    }
    shim_log("daemon: pid %d is gone %.1f s after SIGTERM", pid, std::chrono::duration<double>(std::chrono::steady_clock::now() - term_t0).count());
    if (std::strcmp(name, DaemonName) == 0)
    {
        g_d.pid = 0;
    }
    return true;
}

static bool terminate_daemon(int grace_seconds)
{
    return terminate_process(DaemonName, grace_seconds);
}

// Graceful stop: active torrents are paused (and remembered), then the termination signal.
std::string daemon_stop_graceful(int& paused)
{
    set_state(DaemonStopping);
    paused = 0;
    std::string session_id;
    if (daemon_pid() > 0 && rpc_alive(session_id))
    {
        int const n = pause_active_and_remember(session_id);
        paused = n > 0 ? n : 0;
        shim_log("daemon: paused %d active torrent(s) before stopping (result %d)", paused, n);
    }
    if (!terminate_daemon(g_cfg.daemon_stop_grace_seconds))
    {
        set_state(DaemonNotResponding);
        return ps5tm_tr("mr_d_stopfail");
    }
    set_state(DaemonStopped);
    return "";
}

// Start + resume of what the stop paused + notification
static void launch_common(char const* why)
{
    shim_log("daemon: start (%s)", why);
    std::string const error = daemon_launch_and_wait();
    set_last_error(error);
    if (!error.empty())
    {
        g_d.last_launch_attempt = static_cast<long>(std::time(nullptr)); // the pause before the next attempt counts from its end
        int const failures = ++g_d.launch_failures;
        shim_log("daemon: start failed: %s (failure #%d)", error.c_str(), failures);
        if (failures == MaxAutoLaunchFailures)
        {
            notify_startup_text(ps5tm_tr("me_start_failed_repeated", { std::to_string(failures), error }));
        }
        else if (failures < MaxAutoLaunchFailures)
        {
            notify_startup_text(ps5tm_tr("me_start_failed", { error }));
        }
        return;
    }
    g_d.launch_failures = 0;
    std::string session_id;
    int const resumed = rpc_alive(session_id) ? resume_remembered(session_id) : 0;
    std::string text = ps5tm_tr("me_started", { std::string{ ps5tm_ip() } + ":" + std::to_string(rpc_port()) + "/" });
    if (g_cfg.settings_server)
    {
        int port = g_settings_port_actual.load();
        if (port <= 0)
        {
            port = g_cfg.settings_port.load();
        }
        text += ps5tm_tr("me_started_settings", { std::string{ ps5tm_ip() } + ":" + std::to_string(port) + "/" });
    }
    if (resumed > 0)
    {
        text += ps5tm_tr("me_started_resumed", { std::to_string(resumed) });
    }
    notify_startup_text(text);
}

static void worker_stop_by_user()
{
    int paused = 0;
    std::string const error = daemon_stop_graceful(paused);
    if (error.empty())
    {
        desired_save(false); // after a manual stop the daemon does not start by itself on the next manager start
        notify_startup_text(ps5tm_tr("me_stopped", { std::to_string(paused) }));
    }
    else
    {
        notify_startup_text(ps5tm_tr("me_stop_failed", { error }));
    }
    g_d.busy = false;
}

static void worker_start_by_user()
{
    g_d.launch_failures = 0; // a manual start allows automatic attempts again
    desired_save(true);
    launch_common("requested from the settings page");
    g_d.busy = false;
}

static void worker_launch_by_supervisor()
{
    launch_common("the daemon is not running");
    g_d.busy = false;
}

// Hung daemon: termination signal, forced if needed, and a new start.
static void worker_restart_hung()
{
    long const hang_for = g_d.unhealthy_since.load() != 0 ? static_cast<long>(std::time(nullptr)) - g_d.unhealthy_since.load() : 0;
    set_state(DaemonRestarting);
    shim_log("daemon: restarting a daemon that does not respond");
    diag_dump("restarting the daemon");
    // First ask it to terminate gracefully and wait longer (daemon_stop_grace_seconds): on a clean exit Transmission writes the torrent
    // state files (resume). A forced termination at that moment corrupts them: the torrent then loads without state, paused and
    // in the default folder.
    bool const gone = terminate_daemon(g_cfg.daemon_stop_grace_seconds.load());
    g_d.last_restart = static_cast<long>(std::time(nullptr));
    if (!gone)
    {
        // The process does not die even after SIGKILL: a thread is stuck in a system call (most often disk I/O). A second instance
        // would refuse to start anyway ("another daemon is already running"), so do not start it and retry in 60 s.
        g_d.kill_failed = true;
        if (!g_d.stuck_reported.exchange(true))
        {
            journal_event(ps5tm_tr("mr_hang_still", { duration_text(hang_for) }));
        }
        shim_log("daemon: the process cannot be terminated (usually a thread is stuck in the I/O of a disk): not starting a second copy; trying again in 60 s");
        set_state(DaemonNotResponding);
        g_d.busy = false;
        return;
    }
    g_d.last_restart = static_cast<long>(std::time(nullptr));
    std::string const error = daemon_launch_and_wait();
    set_last_error(error);
    if (error.empty())
    {
        g_d.launch_failures = 0;
        journal_event(ps5tm_tr("mr_hang_restarted", { duration_text(hang_for) }));
        g_d.hang_journaled = true;
        notify_startup_text(ps5tm_tr("me_restarted", { std::string{ ps5tm_ip() } + ":" + std::to_string(rpc_port()) + "/" }));
    }
    else
    {
        shim_log("daemon: restart failed: %s", error.c_str());
        journal_event(ps5tm_tr("mr_hang_restart_failed", { duration_text(hang_for), error }));
        notify_startup_text(ps5tm_tr("me_restart_failed", { error }));
    }
    g_d.busy = false;
}

// The Transmission port was changed in the settings: restart the daemon (gracefully, resuming the torrents).
static void worker_port_change()
{
    shim_log("daemon: the port was changed to %d: restarting the daemon", rpc_port());
    if (daemon_pid() > 0)
    {
        int paused = 0;
        daemon_stop_graceful(paused);
    }
    launch_common("the daemon port was changed");
    g_d.busy = false;
}

// The manager was updated, and with it a new daemon version: restart the daemon (with pause and resume).
static void worker_update_daemon()
{
    shim_log("daemon: the manager brought a new daemon version: restarting the daemon");
    int paused = 0;
    daemon_stop_graceful(paused);
    launch_common("new daemon version");
    g_d.busy = false;
}

template <typename F>
static bool start_worker(F fn)
{
    if (g_d.busy.exchange(true))
    {
        return false;
    }
    std::thread(fn).detach();
    return true;
}

// On manager start: adopt an already running daemon or (if needed) start a new one
void daemon_manager_start()
{
    g_d.since = static_cast<long>(std::time(nullptr));
    g_d.desired_running = desired_load();
    int const pid = daemon_pid();
    shim_log("daemon: manager started; desired=%s, daemon pid=%d", g_d.desired_running ? "running" : "stopped", pid);
    if (pid > 0)
    {
        g_d.launched_at = static_cast<long>(std::time(nullptr)) - 600; // do not know when it was started: assume long ago
        g_d.launched_port = rpc_port();
        set_state(DaemonRunning);
        g_d.epoch++;
        if (ps5tm_daemon_blob_changed())
        {
            start_worker(worker_update_daemon);
        }
    }
    else
    {
        set_state(DaemonStopped);
    }
}


// ---------------------------------------------------------------- network change: quick restart of a daemon that does not answer on the new address

static std::string net_reason_msg(int reason)
{
    return ps5tm_tr(reason == 1 ? "mr_why_net_back" : reason == 2 ? "mr_why_ip" : "mr_why_wake");
}

static char const* net_reason_text(int reason)
{
    return reason == 1 ? "network dropped and came back" : reason == 2 ? "IP address changed" : "console woke from rest mode";
}

// Whether Transmission accepts connections on the address ip (web interface port) and answers over HTTP. Connection with a wait of up to 1.5 s.
static bool daemon_answers_on(char const* ip)
{
    int const fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(rpc_port());
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1)
    {
        close(fd);
        return false;
    }
    int const flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    bool connected = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    if (!connected && errno == EINPROGRESS)
    {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(fd, &w);
        timeval tv{};
        tv.tv_sec = 1;
        tv.tv_usec = 500000;
        if (select(fd + 1, nullptr, &w, nullptr, &tv) > 0)
        {
            int err = 1;
            socklen_t len = sizeof(err);
            connected = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
        }
    }
    if (!connected)
    {
        close(fd);
        return false;
    }
    fcntl(fd, F_SETFL, flags);
    timeval tv{};
    tv.tv_sec = 2;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    std::string const request = std::string{ "GET /transmission/rpc HTTP/1.0\r\nHost: " } + ip + "\r\nConnection: close\r\n\r\n";
    char buf[16] = { 0 };
    bool const answered = send_all(fd, request) && recv(fd, buf, 12, 0) >= 5 && std::strncmp(buf, "HTTP/", 5) == 0;
    close(fd);
    return answered;
}

// Network event: starts the check (if this feature is not disabled by the setting).
void net_event(int reason)
{
    if (g_cfg.daemon_network_restart_seconds.load() <= 0)
    {
        return;
    }
    g_d.net_event_at = static_cast<long>(std::time(nullptr));
    g_d.net_reason = reason;
    g_d.net_attempts = 0;
    g_d.net_probe_at = 0;
    g_d.net_pending = true;
    shim_log("network: %s: checking that Transmission answers on the current address", net_reason_text(reason));
}

static void worker_restart_net()
{
    long const now = static_cast<long>(std::time(nullptr));
    long const waited = now - g_d.net_event_at.load();
    std::string const ip = ps5tm_ip();
    char const* const reason = net_reason_text(g_d.net_reason.load());
    set_state(DaemonRestarting);
    shim_log("daemon: the network changed (%s) and Transmission does not answer on %s for %ld s: restarting it", reason, ip.c_str(), waited);
    diag_dump("restarting the daemon (network change)");
    bool const gone = terminate_daemon(g_cfg.daemon_stop_grace_seconds.load());
    g_d.last_restart = static_cast<long>(std::time(nullptr));
    if (!gone)
    {
        g_d.kill_failed = true;
        if (!g_d.stuck_reported.exchange(true))
        {
            journal_event(ps5tm_tr("mr_net_still", { net_reason_msg(g_d.net_reason.load()) }));
        }
        shim_log("daemon: the process cannot be terminated: not starting a second copy; trying again later");
        set_state(DaemonNotResponding);
        g_d.busy = false;
        return;
    }
    std::string const error = daemon_launch_and_wait();
    set_last_error(error);
    if (error.empty())
    {
        g_d.launch_failures = 0;
        journal_event(ps5tm_tr("mr_net_restarted", { net_reason_msg(g_d.net_reason.load()), ip, duration_text(waited) }));
        g_d.hang_journaled = true; // so the same case does not get into the bell again as "idle"
        notify_startup_text(ps5tm_tr("me_restarted_net", { std::string{ ps5tm_ip() } + ":" + std::to_string(rpc_port()) + "/" }));
    }
    else
    {
        shim_log("daemon: restart after the network change failed: %s", error.c_str());
        journal_event(ps5tm_tr("mr_net_restart_failed", { error }));
        notify_startup_text(ps5tm_tr("me_restart_failed", { error }));
    }
    g_d.busy = false;
}

// Once a second while the check after a network change is running.
static void net_check(long now, bool have_ip)
{
    int const secs = g_cfg.daemon_network_restart_seconds.load();
    long const since = g_d.net_event_at.load();
    if (secs <= 0 || !have_ip || daemon_pid() == 0 || now - since > secs + 180)
    {
        g_d.net_pending = false; // no daemon, no network, or the check dragged on: it is no longer needed
        return;
    }
    if (now - g_d.net_probe_at.load() < 2)
    {
        return;
    }
    g_d.net_probe_at = now;
    std::string const ip = ps5tm_ip();
    if (daemon_answers_on(ip.c_str()))
    {
        shim_log("network: Transmission answers on %s (%ld s after the change): nothing to restart", ip.c_str(), now - since);
        g_d.net_pending = false;
        return;
    }
    if (now - since < secs || g_d.net_attempts.load() > 0 || now - g_d.last_restart.load() < 30)
    {
        return;
    }
    long const blocked = g_d.io_blocked_at.load();
    if (blocked != 0 && blocked >= g_d.io_done_at.load() && now - blocked < 120)
    {
        return; // the daemon is waiting for the disk right now: a restart will not help (the general hang rule applies separately)
    }
    g_d.net_attempts = 1;
    start_worker(worker_restart_net);
}

// Once a second: brings the actual daemon state to the desired one and restarts a hung daemon.
void supervise(bool have_ip)
{
    if (g_d.busy.load() || g_sl.hold.load()) // an operation is in progress or we are waiting for wake-up after sleep
    {
        return;
    }
    long const now = static_cast<long>(std::time(nullptr));
    if (now < g_d.launch_not_before.load())
    {
        return; // just woke up: let the file system and network recover
    }
    int const pid = daemon_pid();

    if (pid == 0)
    {
        if (g_d.state.load() != DaemonStopped)
        {
            shim_log("supervisor: the daemon process is gone");
            set_state(DaemonStopped);
        }
        // After three failures in a row the automatic attempts stop: each one sends 10 MB to the loader, and retries
        // change nothing. After that only the "Start" button, a console wake-up or a manager restart.
        if (g_d.desired_running && g_d.launch_failures.load() < MaxAutoLaunchFailures && now - g_d.last_launch_attempt.load() >= 15)
        {
            start_worker(worker_launch_by_supervisor);
        }
        return;
    }

    if (g_d.state.load() == DaemonStopped)
    {
        set_state(DaemonRunning); // the process appeared by itself (for example, started manually): accept it
    }

    int const restart_after = g_cfg.daemon_restart_after_seconds;
    long const unhealthy = g_d.unhealthy_since.load();
    // "Running" is reached only after the first successful daemon answer, so no separate grace period after
    // the start is needed; at least a minute between restarts.
    if (restart_after > 0 && have_ip && unhealthy != 0 && now - unhealthy >= restart_after && now - g_d.last_restart.load() >= 60)
    {
        start_worker(worker_restart_hung);
        return;
    }
    if (g_d.net_pending.load())
    {
        net_check(now, have_ip);
    }
}

// Result of a daemon poll: count how long it has not been responding (only while the network works)
void daemon_note_rpc(bool ok, bool have_ip)
{
    long const now = static_cast<long>(std::time(nullptr));
    if (ok)
    {
        long const was = g_d.unhealthy_since.load();
        bool const already = g_d.hang_journaled.exchange(false);
        bool const relaunched = was != 0 && g_d.launched_at.load() >= was;
        g_d.unhealthy_since = 0;
        g_d.kill_failed = false;
        g_d.stuck_reported = false;
        if (g_d.state.load() == DaemonNotResponding)
        {
            set_state(DaemonRunning);
        }
        if (was != 0 && now - was >= 10 && !already)
        {
            journal_event(relaunched ? ps5tm_tr("mr_idle_relaunched", { duration_text(now - was) }) : ps5tm_tr("mr_idle_answers", { duration_text(now - was) }));
        }
        return;
    }
    if (!have_ip)
    {
        g_d.unhealthy_since = 0; // without a network even loopback may not work: do not count
        return;
    }
    if (g_d.unhealthy_since.load() == 0)
    {
        g_d.unhealthy_since = now;
    }
    if (now - g_d.unhealthy_since.load() >= 10 && g_d.state.load() == DaemonRunning)
    {
        set_state(DaemonNotResponding);
        shim_log("daemon: not responding (network is up)");
        diag_dump("daemon not responding");
    }
}

} // namespace ps5tm_core

using namespace ps5tm_core;


void ps5tm_daemon_port_changed()
{
    if (!g_d.desired_running.load() || g_d.state.load() == DaemonStopped)
    {
        return; // the daemon is not running: the new port will be used on the next start
    }
    std::thread([] {
        for (int i = 0; i < 90; ++i) // another daemon operation may still be in progress
        {
            if (!g_d.busy.exchange(true))
            {
                worker_port_change();
                return;
            }
            sleep(1);
        }
        shim_log("daemon: the port change could not be applied (the daemon stayed busy)");
    }).detach();
}

// ---------------------------------------------------------------- daemon: state and actions for the settings page

Ps5tmDaemonInfo ps5tm_daemon_info()
{
    Ps5tmDaemonInfo info;
    info.state = state_name(g_d.state.load());
    info.pid = g_d.pid.load();
    info.since_age = static_cast<long>(std::time(nullptr)) - g_d.since.load();
    info.desired_running = g_d.desired_running.load();
    info.busy = g_d.busy.load();
    {
        long const unhealthy = g_d.unhealthy_since.load();
        if (g_d.state.load() == DaemonNotResponding && unhealthy != 0)
        {
            long const waited = static_cast<long>(std::time(nullptr)) - unhealthy;
            int const limit = g_cfg.daemon_restart_after_seconds.load();
            info.unresponsive_for = waited;
            info.restart_in = limit > 0 ? std::max<long>(0, limit - waited) : -1;
        }
        info.stuck = g_d.kill_failed.load();
    }
    if (info.pid > 0 && g_d.state.load() != DaemonStopped)
    {
        info.mem_kib = ps5tm_process_rss_kib(info.pid);
    }
    pthread_mutex_lock(&g_last_error_mutex);
    info.last_error = g_last_error;
    pthread_mutex_unlock(&g_last_error_mutex);
    return info;
}

std::string ps5tm_daemon_request_stop()
{
    if (g_d.busy.load())
    {
        return ps5tm_tr("mr_op_running");
    }
    if (g_d.state.load() == DaemonStopped && daemon_pid() == 0)
    {
        return ps5tm_tr("mr_already_stopped");
    }
    return start_worker(worker_stop_by_user) ? std::string{} : std::string{ ps5tm_tr("mr_op_running") };
}

std::string ps5tm_daemon_request_start()
{
    if (g_d.busy.load())
    {
        return ps5tm_tr("mr_op_running");
    }
    if (daemon_pid() > 0 && g_d.state.load() == DaemonRunning)
    {
        return ps5tm_tr("mr_already_running");
    }
    return start_worker(worker_start_by_user) ? std::string{} : std::string{ ps5tm_tr("mr_op_running") };
}

// Unmounting a USB disk: the torrents located on its mount points are stopped (the daemon closes their files), and we wait up to 15 seconds for the stop plus 2 seconds
// for the files to close. The torrent path is first resolved to the real one (/data/usb0 is /mnt/usb0). Returns the number stopped, -1 if the daemon does not answer
// (then nobody holds the files); detail is an explanation for the log.
int ps5tm_stop_torrents_under(std::vector<std::string> const& roots, std::string& detail)
{
    std::string session_id;
    Json root;
    if (!rpc_call(R"({"method":"torrent-get","arguments":{"fields":["id","status","downloadDir"]}})", root, session_id))
    {
        detail = "the daemon does not answer";
        return -1;
    }
    auto const pick = [&](Json const& reply, std::vector<int>& ids, std::vector<int>& running)
    {
        ids.clear();
        running.clear();
        auto const* args = reply.get("arguments");
        auto const* list = args != nullptr ? args->get("torrents") : nullptr;
        if (list == nullptr || list->type != Json::Type::Array)
        {
            return;
        }
        for (auto const& item : list->items)
        {
            auto const* id = item.get("id");
            auto const* status = item.get("status");
            auto const* dir = item.get("downloadDir");
            if (id == nullptr || status == nullptr || dir == nullptr)
            {
                continue;
            }
            char real[600];
            ps5tm_resolve_alias(dir->str.c_str(), real, sizeof(real));
            bool on_disk = false;
            for (auto const& r : roots)
            {
                on_disk = on_disk || path_is_under(real, r) || path_is_under(dir->str, r);
            }
            if (on_disk)
            {
                ids.push_back(static_cast<int>(id->number));
                if (static_cast<int>(status->number) != 0)
                {
                    running.push_back(static_cast<int>(id->number));
                }
            }
        }
    };
    std::vector<int> ids;
    std::vector<int> running;
    pick(root, ids, running);
    if (running.empty())
    {
        detail = std::to_string(ids.size()) + " torrents on the disk, none running";
        return 0;
    }
    Json reply;
    rpc_call(std::string{ R"({"method":"torrent-stop","arguments":{"ids":)" } + ids_array(running) + "}}", reply, session_id);
    int const total = static_cast<int>(running.size());
    for (int waited = 0; waited < 15; ++waited)
    {
        sleep(1);
        Json again;
        if (!rpc_call(R"({"method":"torrent-get","arguments":{"fields":["id","status","downloadDir"]}})", again, session_id))
        {
            break;
        }
        pick(again, ids, running);
        if (running.empty())
        {
            break;
        }
    }
    sleep(2); // the daemon closes the files of the stopped torrents
    detail = std::to_string(total) + " torrents stopped" + (running.empty() ? "" : ", " + std::to_string(running.size()) + " still running after 15 s");
    return total - static_cast<int>(running.size());
}
