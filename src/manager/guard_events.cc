// Transmission for the PS5: events of the download folders protection and of the I/O watchdog.
//
// License: GPL-3.0-or-later (like Transmission).

#include "core_internal.h"

namespace ps5tm_core
{


// Rejections by the download folders protection: the daemon writes "guard: rejected: <text>" lines to daemon.log. We move them to the notification bell so that it is visible
// what was not added and why (the Transmission web interface often shows only a generic error).
// An I/O watchdog line of the daemon ("io-watch: blocked|done <seconds> <operation> off=<bytes> len=<bytes> path=<path>") into bell text.
// Empty string: it is not such a line or not meant for the bell (finished operations shorter than 10 s stay only in daemon.log).
static std::string io_watch_text(std::string const& line)
{
    auto const at = line.find("io-watch: ");
    if (at == std::string::npos)
    {
        return {};
    }
    char state[16] = {}, op[24] = {};
    double secs = 0;
    long long off = 0, len = 0;
    if (std::sscanf(line.c_str() + at + 10, "%15s %lf %23s off=%lld len=%lld", state, &secs, op, &off, &len) != 5)
    {
        return {};
    }
    bool const blocked = std::strcmp(state, "blocked") == 0;
    bool const done = std::strcmp(state, "done") == 0;
    if ((!blocked && !done) || (done && secs < 10.0))
    {
        return {};
    }
    std::string path;
    if (auto const pp = line.find(" path=", at); pp != std::string::npos)
    {
        path = line.substr(pp + 6);
    }
    while (!path.empty() && (path.back() == '\r' || path.back() == ' '))
    {
        path.pop_back();
    }
    std::string name = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
    if (name.empty())
    {
        name = path.empty() ? "?" : path;
    }
    if (name.size() > 60)
    {
        name = name.substr(0, 57) + "...";
    }
    struct OpName
    {
        char const* op;
        char const* ru;
    };
    static constexpr OpName names[] = { { "pwrite", "write to" },         { "pread", "read from" },          { "open", "opening" },
                                        { "ftruncate", "resizing" }, { "rename", "renaming" }, { "mkdir", "creating folder" },
                                        { "unlink", "deleting" },         { "stat", "check" },            { "lstat", "check" },
                                        { "statvfs", "checking free space on" }, { "fsync", "flushing to disk" } };
    std::string ru = op;
    for (auto const& n : names)
    {
        if (std::strcmp(n.op, op) == 0)
        {
            ru = ps5tm_tr((std::string{ "mr_op_" } + n.op).c_str());
        }
    }
    std::string where;
    if ((std::strcmp(op, "pwrite") == 0 || std::strcmp(op, "pread") == 0) && off > 0)
    {
        char b[48];
        bool gb = false;
        if (off >= (1LL << 30))
        {
            std::snprintf(b, sizeof(b), "%.1f", static_cast<double>(off) / (1024.0 * 1024.0 * 1024.0));
            gb = true;
        }
        else
        {
            std::snprintf(b, sizeof(b), "%.0f", static_cast<double>(off) / (1024.0 * 1024.0));
        }
        where = ps5tm_tr(gb ? "mr_io_off_gb" : "mr_io_off_mb", { b });
    }
    return ps5tm_tr(blocked ? "mr_io_blocked" : "mr_io_done", { ru, name, where, duration_text(static_cast<long>(secs)) });
}

void check_guard_events()
{
    static long offset = -1;
    static std::map<std::string, std::time_t> recent;
    struct stat st;
    if (stat(DaemonLog, &st) != 0)
    {
        offset = -1;
        return;
    }
    long const size = static_cast<long>(st.st_size);
    if (offset < 0)
    {
        offset = size; // do not retell the old lines
        return;
    }
    if (offset > size)
    {
        offset = 0; // the log was rotated: read the new one from the start
    }
    if (size == offset)
    {
        return;
    }
    FILE* f = std::fopen(DaemonLog, "rb");
    if (f == nullptr)
    {
        return;
    }
    std::string chunk(static_cast<std::size_t>(std::min<long>(size - offset, 65536)), '\0');
    std::fseek(f, offset, SEEK_SET);
    std::size_t const got = std::fread(&chunk[0], 1, chunk.size(), f);
    std::fclose(f);
    chunk.resize(got);
    auto const last_newline = chunk.rfind('\n');
    if (last_newline == std::string::npos)
    {
        if (got < 65536)
        {
            return; // the line is still being written
        }
        offset += static_cast<long>(got);
        return;
    }
    chunk.resize(last_newline + 1);
    auto const now = std::time(nullptr);
    for (auto it = recent.begin(); it != recent.end();)
    {
        it = (now - it->second > 60) ? recent.erase(it) : std::next(it);
    }
    int added = 0;
    std::size_t pos = 0;
    std::size_t consumed = 0; // how many bytes were parsed: the rest (if the batch exceeds the limit) is parsed in the next second instead of being lost
    while (pos < chunk.size() && added < 5)
    {
        auto end = chunk.find('\n', pos);
        if (end == std::string::npos)
        {
            end = chunk.size();
        }
        std::string const line = chunk.substr(pos, end - pos);
        pos = end + 1;
        consumed = pos;
        char const* const marker = "guard: rejected: ";
        auto const at = line.find(marker);
        if (at == std::string::npos)
        {
            if (line.find("io-watch: blocked") != std::string::npos)
            {
                g_d.io_blocked_at = now;
            }
            else if (line.find("io-watch: done") != std::string::npos)
            {
                g_d.io_done_at = now;
            }
            std::string const io = io_watch_text(line);
            if (!io.empty() && recent.count(io) == 0)
            {
                recent[io] = now;
                journal_add(io.c_str(), 2);
                ++added;
            }
            continue;
        }
        std::string text = line.substr(at + std::strlen(marker));
        while (!text.empty() && (text.back() == '\r' || text.back() == ' '))
        {
            text.pop_back();
        }
        if (text.empty() || recent.count(text) != 0)
        {
            continue; // the same rejection is not repeated more than once a minute
        }
        recent[text] = now;
        journal_add(ps5tm_tr("mr_rejected", { text }).c_str(), 2);
        ++added;
    }
    offset += static_cast<long>(consumed);
}

} // namespace ps5tm_core
