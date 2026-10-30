// Transmission for the PS5: the HTTP client for the Transmission RPC and the torrent snapshot.
//
// License: GPL-3.0-or-later (like Transmission).

#include "core_internal.h"

namespace ps5tm_core
{


// ---------------------------------------------------------------- HTTP


bool send_all(int fd, std::string const& data)
{
    std::size_t done = 0;
    while (done < data.size())
    {
        auto const n = send(fd, data.data() + done, data.size() - done, 0);
        if (n <= 0)
        {
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

// An HTTP/1.0 request with Connection: close, so the response is not chunked and the end of the response = connection close.
static bool http_post(std::string const& session_id, std::string const& body, HttpResult& out)
{
    int const fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return false;
    }

    timeval tv{};
    tv.tv_sec = 5;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(rpc_port());
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        close(fd);
        return false;
    }

    std::string request = "POST /transmission/rpc HTTP/1.0\r\nHost: 127.0.0.1:" + std::to_string(rpc_port()) + "\r\n";
    if (!session_id.empty())
    {
        request += "X-Transmission-Session-Id: " + session_id + "\r\n";
    }
    request += "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
    request += body;

    std::string response;
    bool ok = send_all(fd, request);
    char buf[8192];
    while (ok)
    {
        auto const n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0)
        {
            ok = false;
            break;
        }
        if (n == 0)
        {
            break;
        }
        response.append(buf, static_cast<std::size_t>(n));
        if (response.size() > MaxResponse)
        {
            ok = false;
        }
    }
    close(fd);
    if (!ok)
    {
        return false;
    }

    // "HTTP/1.x NNN ..."
    if (response.size() < 12 || response.compare(0, 5, "HTTP/") != 0)
    {
        return false;
    }
    out.status = std::atoi(response.c_str() + 9);

    auto const header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos)
    {
        return false;
    }

    // the X-Transmission-Session-Id header (case-insensitive)
    std::string const headers = response.substr(0, header_end);
    std::string lower = headers;
    for (auto& c : lower)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    char const* const key = "x-transmission-session-id:";
    auto pos = lower.find(key);
    if (pos != std::string::npos)
    {
        pos += std::strlen(key);
        while (pos < headers.size() && headers[pos] == ' ')
        {
            ++pos;
        }
        auto end = headers.find("\r\n", pos);
        out.session_id = headers.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    }

    out.body = response.substr(header_end + 4);
    return true;
}

// ---------------------------------------------------------------- torrent snapshot


// One RPC call; on a 409 response it takes a new session id and repeats the request.
bool rpc_call(std::string const& payload, Json& root, std::string& session_id)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        HttpResult r;
        if (!http_post(session_id, payload, r))
        {
            return false;
        }
        if (r.status == 409)
        {
            session_id = r.session_id;
            continue;
        }
        if (r.status != 200)
        {
            return false;
        }
        return Parser{ r.body.data(), r.body.data() + r.body.size() }.parse(root);
    }
    return false;
}

bool fetch(std::map<int, Torrent>& out, std::string& session_id)
{
    std::string const request =
        R"({"method":"torrent-get","arguments":{"fields":["id","name","status","error","errorString","leftUntilDone","downloadDir","rateDownload","rateUpload"]}})";

    Json root;
    if (!rpc_call(request, root, session_id))
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
        if (id == nullptr || id->type != Json::Type::Number)
        {
            continue;
        }
        Torrent t;
        if (auto const* v = item.get("status"); v != nullptr)
        {
            t.status = static_cast<int>(v->number);
        }
        if (auto const* v = item.get("leftUntilDone"); v != nullptr)
        {
            t.left = v->number;
        }
        if (auto const* v = item.get("error"); v != nullptr)
        {
            t.error = static_cast<int>(v->number);
        }
        if (auto const* v = item.get("name"); v != nullptr)
        {
            t.name = v->str;
        }
        if (auto const* v = item.get("errorString"); v != nullptr)
        {
            t.error_string = v->str;
        }
        if (auto const* v = item.get("downloadDir"); v != nullptr)
        {
            t.download_dir = v->str;
        }
        if (auto const* v = item.get("rateDownload"); v != nullptr)
        {
            t.rate_down = v->number;
        }
        if (auto const* v = item.get("rateUpload"); v != nullptr)
        {
            t.rate_up = v->number;
        }
        out.emplace(static_cast<int>(id->number), std::move(t));
    }
    return true;
}

static std::string json_escape(std::string const& s)
{
    std::string out;
    for (char const c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
        }
    }
    return out;
}

// Free space (in bytes) on the volume where the folder dir is; -1 if it could not be determined.
double free_bytes(std::string const& dir, std::string& session_id)
{
    std::string const request = R"({"method":"free-space","arguments":{"path":")" + json_escape(dir) + R"("}})";
    Json root;
    if (!rpc_call(request, root, session_id))
    {
        return -1;
    }
    auto const* args = root.get("arguments");
    auto const* size = args != nullptr ? args->get("size-bytes") : nullptr;
    return size != nullptr && size->type == Json::Type::Number ? size->number : -1;
}

std::string format_size(double bytes)
{
    char buf[32];
    if (bytes >= 1e9)
    {
        std::snprintf(buf, sizeof(buf), "%.1f", bytes / 1e9);
        return ps5tm_tr("me_size_gb", { buf });
    }
    std::snprintf(buf, sizeof(buf), "%.0f", bytes / 1e6);
    return ps5tm_tr("me_size_mb", { buf });
}

} // namespace ps5tm_core
