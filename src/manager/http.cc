// Transmission for the PS5: HTTP requests and responses, the listening socket and its self-test.
//
// License: GPL-3.0-or-later (like Transmission).

#include "ui_internal.h"

namespace ps5tm_ui
{

// ---------------------------------------------------------------- HTTP


// Waits for data on the socket for no longer than seconds. Needed because on BSD (and on PS5) a socket accepted
// via accept() inherits the non-blocking mode from the listening one: recv() then returns EAGAIN immediately.
static bool wait_readable(int fd, int seconds)
{
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(fd, &readable);
    timeval tv{};
    tv.tv_sec = seconds;
    return select(fd + 1, &readable, nullptr, nullptr, &tv) > 0;
}

// recv() with waiting: EAGAIN is not treated as end of connection
static std::ptrdiff_t recv_wait(int fd, char* buf, std::size_t size)
{
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        auto const n = recv(fd, buf, size, 0);
        if (n >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
        {
            return n;
        }
        if (!wait_readable(fd, 5))
        {
            return -1;
        }
    }
    return -1;
}

bool read_request(int fd, Request& req)
{
    timeval tv{};
    tv.tv_sec = 5;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::string data;
    char buf[2048];
    std::size_t header_end = std::string::npos;
    while (header_end == std::string::npos)
    {
        auto const n = recv_wait(fd, buf, sizeof(buf));
        if (n <= 0)
        {
            return false;
        }
        data.append(buf, static_cast<std::size_t>(n));
        header_end = data.find("\r\n\r\n");
        if (data.size() > MaxHeader && header_end == std::string::npos)
        {
            return false;
        }
    }

    std::string const head = data.substr(0, header_end);
    auto const line_end = head.find("\r\n");
    std::string const request_line = head.substr(0, line_end);
    auto const sp1 = request_line.find(' ');
    auto const sp2 = request_line.find(' ', sp1 == std::string::npos ? 0 : sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos)
    {
        return false;
    }
    req.method = request_line.substr(0, sp1);
    req.path = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    auto const query = req.path.find('?');
    if (query != std::string::npos)
    {
        req.path.erase(query);
    }

    std::size_t content_length = 0;
    std::size_t pos = line_end;
    while (pos != std::string::npos && pos < head.size())
    {
        pos += 2;
        auto const next = head.find("\r\n", pos);
        std::string const line = head.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        auto const colon = line.find(':');
        if (colon != std::string::npos)
        {
            std::string name = line.substr(0, colon);
            for (auto& c : name)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            std::string value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ')
            {
                value.erase(0, 1);
            }
            if (name == "content-length")
            {
                content_length = static_cast<std::size_t>(std::strtoul(value.c_str(), nullptr, 10));
            }
            else if (name == "content-type")
            {
                req.content_type = value;
            }
        }
        pos = next;
    }
    if (content_length > MaxBody)
    {
        return false;
    }

    req.body = data.substr(header_end + 4);
    while (req.body.size() < content_length)
    {
        auto const n = recv_wait(fd, buf, sizeof(buf));
        if (n <= 0)
        {
            return false;
        }
        req.body.append(buf, static_cast<std::size_t>(n));
    }
    req.body.resize(content_length);
    return true;
}

void send_response(int fd, int code, char const* status, char const* content_type, std::string const& body)
{
    std::string head = "HTTP/1.0 " + std::to_string(code) + " " + status + "\r\nContent-Type: " + content_type +
        "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    std::string const all = head + body;
    std::size_t done = 0;
    while (done < all.size())
    {
        auto const n = send(fd, all.data() + done, all.size() - done, 0);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(fd, &writable);
            timeval tv{};
            tv.tv_sec = 5;
            if (select(fd + 1, nullptr, &writable, nullptr, &tv) > 0)
            {
                continue;
            }
            return;
        }
        if (n <= 0)
        {
            return;
        }
        done += static_cast<std::size_t>(n);
    }
}

void send_json(int fd, int code, char const* status, std::string const& json)
{
    send_response(fd, code, status, "application/json; charset=utf-8", json);
}
// ---------------------------------------------------------------- listening socket

static int make_listener(int port, int& error)
{
    int const fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        error = errno;
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(fd, 8) != 0)
    {
        error = errno;
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    error = 0;
    return fd;
}

// First free port starting from wanted (the Transmission port is skipped). preferred: the port already listened on, tried
// first so that the page address does not change after sleep or a network change. The scan continues only while the port is busy.
int make_listener_scan(int wanted, int preferred, int& actual, int& error)
{
    if (preferred >= 1024)
    {
        int const fd = make_listener(preferred, error);
        if (fd >= 0)
        {
            actual = preferred;
            return fd;
        }
    }
    for (int i = 0; i < 50 && wanted + i <= 65535; ++i)
    {
        int const port = wanted + i;
        if (port == g_cfg.daemon_port.load())
        {
            continue;
        }
        int const fd = make_listener(port, error);
        if (fd >= 0)
        {
            actual = port;
            return fd;
        }
        if (error != EADDRINUSE && error != EACCES)
        {
            return -1; // another reason (for example, no network): scanning will not help
        }
    }
    return -1;
}

// Whether our own socket accepts connections (a check "from the inside" over loopback).
bool probe_listener(int port)
{
    int const fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return false;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool ok = false;
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
    {
        ok = true;
    }
    else if (errno == EINPROGRESS)
    {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(fd, &w);
        timeval tv{};
        tv.tv_sec = 2;
        int err = 0;
        socklen_t len = sizeof(err);
        ok = select(fd + 1, nullptr, &w, nullptr, &tv) > 0 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
    }
    close(fd);
    return ok;
}

// Self-test with a real HTTP request (not just a TCP connection): a separate thread requests
// /api/state from our own server. The result is written to the log on the first check and on every change.
static std::atomic<int> g_selftest_last{ -1 }; // -1 not checked yet, 0 not responding, 1 responding

void http_selftest_in_background(int port)
{
    std::thread(
        [port]
        {
            bool ok = false;
            std::string why = "no connection";
            int const fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd >= 0)
            {
                timeval tv{};
                tv.tv_sec = 3;
                setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(static_cast<std::uint16_t>(port));
                addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
                {
                    char const request[] = "GET /api/state HTTP/1.0\r\n\r\n";
                    if (send(fd, request, sizeof(request) - 1, 0) > 0)
                    {
                        char buf[64] = {};
                        auto const n = recv(fd, buf, sizeof(buf) - 1, 0);
                        ok = n >= 12 && std::strncmp(buf, "HTTP/1.", 7) == 0 && std::strncmp(buf + 8, " 200", 4) == 0;
                        why = n <= 0 ? "empty answer (the connection was closed without data)" : std::string{ "unexpected answer: " } + buf;
                    }
                    else
                    {
                        why = "the request was not sent";
                    }
                }
                close(fd);
            }
            int const previous = g_selftest_last.exchange(ok ? 1 : 0);
            if (previous != (ok ? 1 : 0))
            {
                if (ok)
                {
                    shim_log("settings server: HTTP self-test OK (port %d)", port);
                }
                else
                {
                    shim_log("settings server: HTTP self-test FAILED (port %d): %s", port, why.c_str());
                    char ip[64];
                    if (ps5tm_current_ip(ip, sizeof(ip)) == 0) // without a network (sleep, switching) even loopback may not work: do not alarm
                    {
                        ps5tm_notify_text(ps5tm_tr("me_page_nohttp").c_str());
                    }
                }
            }
        })
        .detach();
}

} // namespace ps5tm_ui
