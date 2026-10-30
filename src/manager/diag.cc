// Transmission for the PS5: diagnostics: processes, threads, RPC probe.
//
// License: GPL-3.0-or-later (like Transmission).

#include "core_internal.h"

namespace ps5tm_core
{


// Snapshot for the log: what the daemon answers on loopback and where its threads stand. Not more than once in 8 seconds.
void diag_dump(char const* why)
{
    static std::time_t last = 0;
    auto const now = std::time(nullptr);
    if (now - last < 8)
    {
        return;
    }
    last = now;
    shim_log("diag (%s): RPC probe: %s", why, ps5tm_rpc_probe_text().c_str());
    shim_log("diag (%s): daemon process:\n%s", why, ps5tm_thread_snapshot_text().c_str());
}

} // namespace ps5tm_core

using namespace ps5tm_core;



// ---------------------------------------------------------------- diagnostics (settings page and log)

std::string ps5tm_process_list_text()
{
    char buffer[4096];
    int const count = ps5tm_process_list(buffer, sizeof(buffer));
    std::string text = "port " + std::to_string(rpc_port()) + " (Transmission): ";
    text += ps5tm_port_in_use(rpc_port()) ? "in use" : "free";
    text += "; port 9021 (ELF loader): ";
    text += ps5tm_port_in_use(9021) ? "someone is listening" : "nobody is listening";
    if (count < 0)
    {
        return text + "\nthe process list could not be obtained (errno " + std::to_string(errno) + ")";
    }
    return text + "\nprocesses (" + std::to_string(count) + "): pid state name\n" + buffer;
}

std::string ps5tm_thread_snapshot_text()
{
    int const pid = g_d.pid.load() > 0 ? g_d.pid.load() : ps5tm_find_pid(DaemonName);
    if (pid <= 0)
    {
        return "daemon process not found\n";
    }
    char buffer[8192];
    int const count = ps5tm_thread_snapshot(pid, buffer, sizeof(buffer));
    if (count < 0)
    {
        return std::string{ "thread snapshot unavailable (no method worked):\n" } + buffer;
    }
    return std::string{ buffer } + "(entries: " + std::to_string(count) + ", pid " + std::to_string(pid) + ")\n";
}

// What Transmission answers on loopback right now: the connection and the first line of the HTTP response to a
// /transmission/rpc request (a "409" response means the RPC handler is alive). Distinguishes "socket is dead"
// (connection failed), "connection accepted but no answer" (the Transmission thread does not process requests)
// and "everything answers".
std::string ps5tm_rpc_probe_text()
{
    auto const now_ms = [] {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<long long>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
    };

    long long const started = now_ms();
    int const fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return std::string{ "socket() failed: errno " } + std::to_string(errno);
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(rpc_port()));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int connect_errno = 0;
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        if (errno != EINPROGRESS)
        {
            connect_errno = errno;
        }
        else
        {
            fd_set w;
            FD_ZERO(&w);
            FD_SET(fd, &w);
            timeval tv{};
            tv.tv_sec = 3;
            if (select(fd + 1, nullptr, &w, nullptr, &tv) > 0)
            {
                socklen_t len = sizeof(connect_errno);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &connect_errno, &len);
            }
            else
            {
                connect_errno = ETIMEDOUT;
            }
        }
    }
    long long const connected = now_ms();
    if (connect_errno != 0)
    {
        close(fd);
        return std::string{ "connect FAILED: errno " } + std::to_string(connect_errno) + " after " + std::to_string(connected - started) + " ms";
    }

    std::string text = "connect OK (" + std::to_string(connected - started) + " ms); ";
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
    timeval tv{};
    tv.tv_sec = 3;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    char const request[] = "GET /transmission/rpc HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n";
    if (send(fd, request, sizeof(request) - 1, 0) <= 0)
    {
        close(fd);
        return text + "request not sent: errno " + std::to_string(errno);
    }
    char buf[256] = {};
    auto const n = recv(fd, buf, sizeof(buf) - 1, 0);
    long long const answered = now_ms();
    close(fd);
    if (n <= 0)
    {
        return text + (n == 0 ? "connection closed without an answer" : "NO HTTP answer within 3 s (errno " + std::to_string(errno) + ")");
    }
    std::string line{ buf };
    line = line.substr(0, line.find("\r\n"));
    return text + "HTTP answer in " + std::to_string(answered - connected) + " ms: " + clip(line, 60);
}
