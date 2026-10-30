// Transmission for the PS5: rotation and clean-up of the debug logs.
//
// License: GPL-3.0-or-later (like Transmission).

#include "core_internal.h"

namespace ps5tm_core
{


// ---- debug log storage (logs/): split by date once a day, delete the old ones

static std::string utc_date(std::time_t t)
{
    std::tm tmv{};
    gmtime_r(&t, &tmv);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return buf;
}

// Appends the contents of file from to file to. true = everything was copied.
static bool append_file_to(std::string const& from, std::string const& to)
{
    FILE* in = std::fopen(from.c_str(), "rb");
    if (in == nullptr)
    {
        return false;
    }
    FILE* out = std::fopen(to.c_str(), "ab");
    if (out == nullptr)
    {
        std::fclose(in);
        return false;
    }
    char buffer[8192];
    bool ok = true;
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), in)) > 0)
    {
        if (std::fwrite(buffer, 1, n, out) != n)
        {
            ok = false;
            break;
        }
    }
    std::fclose(in);
    ok = std::fclose(out) == 0 && ok;
    return ok;
}

// Once a day (in UTC): the current log is copied to <name>.<date>.log and reset. The logs are opened with O_APPEND
// (Transmission writes in "a" mode), so writing continues from the start of the file. That way "logs for N days" are
// N files rather than one endless one: the logs contain no calendar dates (they hold time since launch).
void logs_rotate_if_needed(std::time_t now)
{
    std::string const today = utc_date(now);
    std::string stamp;
    if (FILE* f = std::fopen(LogsStampFile, "r"))
    {
        char line[32] = {};
        if (std::fgets(line, sizeof(line), f) != nullptr)
        {
            stamp = line;
            while (!stamp.empty() && (stamp.back() == '\n' || stamp.back() == '\r'))
            {
                stamp.pop_back();
            }
        }
        std::fclose(f);
    }
    if (stamp == today)
    {
        return;
    }
    for (char const* base : DebugLogBases)
    {
        std::string const path = std::string{ LogsDir } + "/" + base + ".log";
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || st.st_size == 0)
        {
            continue;
        }
        // date of the contents: the date of the previous rotation; without a stamp, the date of the last write to the file
        std::string const date = !stamp.empty() ? stamp : utc_date(st.st_mtime);
        std::string const dest = std::string{ LogsDir } + "/" + base + "." + date + ".log";
        if (append_file_to(path, dest))
        {
            FILE* cut = std::fopen(path.c_str(), "wb"); // reset it (truncate does the same, but without a new import)
            if (cut == nullptr)
            {
                shim_log("logs: cannot truncate %s (errno %d)", path.c_str(), errno);
            }
            else
            {
                std::fclose(cut);
            }
        }
        else
        {
            shim_log("logs: cannot copy %s to %s (errno %d): it stays as it is", path.c_str(), dest.c_str(), errno);
        }
    }
    if (FILE* f = std::fopen(LogsStampFile, "w"))
    {
        std::fprintf(f, "%s\n", today.c_str());
        std::fclose(f);
    }
}

// Deletes <name>.<date>.log files older than logs_keep_days days
void logs_prune(std::time_t now)
{
    std::string const cutoff = utc_date(now - 86400L * g_cfg.logs_keep_days.load());
    DIR* d = opendir(LogsDir);
    if (d == nullptr)
    {
        return;
    }
    std::vector<std::string> doomed;
    while (struct dirent* e = readdir(d))
    {
        std::string const name = e->d_name;
        for (char const* base : DebugLogBases)
        {
            std::string const prefix = std::string{ base } + ".";
            // <name>.YYYY-MM-DD.log
            if (name.size() == prefix.size() + 10 + 4 && name.compare(0, prefix.size(), prefix) == 0 && name.compare(name.size() - 4, 4, ".log") == 0)
            {
                std::string const date = name.substr(prefix.size(), 10);
                if (date[4] == '-' && date[7] == '-' && date < cutoff)
                {
                    doomed.push_back(std::string{ LogsDir } + "/" + name);
                }
            }
        }
    }
    closedir(d);
    for (auto const& path : doomed)
    {
        if (std::remove(path.c_str()) == 0)
        {
            shim_log("logs: removed %s (older than %d days)", path.c_str(), g_cfg.logs_keep_days.load());
        }
    }
}

} // namespace ps5tm_core
