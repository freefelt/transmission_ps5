// Transmission for the PS5: events and notifications (the bell journal).
//
// License: GPL-3.0-or-later (like Transmission).

#include "core_internal.h"

namespace ps5tm_core
{


// ---------------------------------------------------------------- events and notifications


// Trim at a UTF-8 character boundary so the text does not get corrupted
std::string clip(std::string s, std::size_t max_bytes)
{
    for (auto& c : s)
    {
        if (c == '\n' || c == '\r' || c == '\t')
        {
            c = ' ';
        }
    }
    if (s.size() <= max_bytes)
    {
        return s;
    }
    std::size_t cut = max_bytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
    {
        --cut;
    }
    return s.substr(0, cut) + "...";
}

// Notification journal (the bell on the page): ALL manager notifications, including those disabled in settings.
// Only POD arrays and a statically initialized mutex: there are no constructors when the ELF loads.
static JournalEntry g_journal[JournalSize];
static long g_journal_last_id = 0;
static pthread_mutex_t g_journal_mutex = PTHREAD_MUTEX_INITIALIZER;

void journal_add(char const* text, int shown)
{
    pthread_mutex_lock(&g_journal_mutex);
    long const id = ++g_journal_last_id;
    JournalEntry& e = g_journal[(id - 1) % JournalSize];
    e.id = id;
    e.time = static_cast<long>(std::time(nullptr));
    e.shown = shown;
    std::snprintf(e.text, sizeof(e.text), "%s", text);
    FILE* f = std::fopen(JournalFile, "a");
    if (f != nullptr)
    {
        std::string line = e.text;
        for (char& c : line)
        {
            if (c == '\n' || c == '\t')
            {
                c = static_cast<char>(0x1f); // newline and tab on one line of the file
            }
        }
        std::fprintf(f, "%ld\t%ld\t%d\t%s\n", e.id, e.time, e.shown, line.c_str());
        std::fclose(f);
    }
    pthread_mutex_unlock(&g_journal_mutex);
}

// On manager startup: the latest notifications from the file (so the bell survives a restart)
void journal_load()
{
    FILE* f = std::fopen(JournalFile, "r");
    if (f == nullptr)
    {
        return;
    }
    std::vector<JournalEntry> entries;
    char raw[512];
    while (std::fgets(raw, sizeof(raw), f) != nullptr)
    {
        JournalEntry e{};
        int consumed = 0;
        if (std::sscanf(raw, "%ld\t%ld\t%d\t%n", &e.id, &e.time, &e.shown, &consumed) < 3 || consumed <= 0)
        {
            continue;
        }
        std::snprintf(e.text, sizeof(e.text), "%s", raw + consumed);
        std::size_t n = std::strlen(e.text);
        while (n > 0 && (e.text[n - 1] == '\n' || e.text[n - 1] == '\r'))
        {
            e.text[--n] = 0;
        }
        for (char* c = e.text; *c != 0; ++c)
        {
            if (*c == 0x1f)
            {
                *c = '\n';
            }
        }
        entries.push_back(e);
    }
    std::fclose(f);
    std::size_t const keep = entries.size() > static_cast<std::size_t>(JournalSize) ? static_cast<std::size_t>(JournalSize) : entries.size();
    pthread_mutex_lock(&g_journal_mutex);
    for (std::size_t i = entries.size() - keep; i < entries.size(); ++i)
    {
        g_journal[(entries[i].id - 1) % JournalSize] = entries[i];
        g_journal_last_id = entries[i].id > g_journal_last_id ? entries[i].id : g_journal_last_id;
    }
    pthread_mutex_unlock(&g_journal_mutex);
    if (entries.size() > static_cast<std::size_t>(JournalSize) * 2) // the file has grown: only the latest entries remain on disk
    {
        // the file has grown: keep only the latest entries
        FILE* out = std::fopen(JournalFile, "w");
        if (out != nullptr)
        {
            for (std::size_t i = entries.size() - keep; i < entries.size(); ++i)
            {
                std::string line = entries[i].text;
                for (char& c : line)
                {
                    if (c == '\n' || c == '\t')
                    {
                        c = static_cast<char>(0x1f);
                    }
                }
                std::fprintf(out, "%ld\t%ld\t%d\t%s\n", entries[i].id, entries[i].time, entries[i].shown, line.c_str());
            }
            std::fclose(out);
        }
    }
}

// Single entry point for notifications with a setting: send to the console if allowed; the journal gets them either way
// (the wrapper itself records what is sent in notify(); we record here what is disabled).
void announce(bool enabled, std::string const& text)
{
    if (enabled)
    {
        ps5tm_notify_text(text.c_str());
    }
    else
    {
        journal_add(text.c_str(), 0);
    }
}

// Event into the journal (bell) without sending to the console: what happened with sleep and auto-sleep.
// Duration in Russian: "45 s", "12 min 5 s", "1 h 20 min".
std::string duration_text(long s)
{
    if (s < 0)
    {
        s = 0;
    }
    if (s >= 3600)
    {
        return ps5tm_tr("mr_dur_hm", { std::to_string(s / 3600), std::to_string((s % 3600) / 60) });
    }
    if (s >= 60)
    {
        return ps5tm_tr("mr_dur_ms", { std::to_string(s / 60), std::to_string(s % 60) });
    }
    return ps5tm_tr("mr_dur_s", { std::to_string(s) });
}

void journal_event(std::string const& text)
{
    journal_add(text.c_str(), 2);
}

// Rewrites the journal file from the entries left in memory (in id order). Call under g_journal_mutex.
static void journal_rewrite_locked()
{
    FILE* out = std::fopen(JournalFile, "w");
    if (out == nullptr)
    {
        return;
    }
    long const first = g_journal_last_id > JournalSize ? g_journal_last_id - JournalSize + 1 : 1;
    for (long id = first; id <= g_journal_last_id; ++id)
    {
        JournalEntry const& e = g_journal[(id - 1) % JournalSize];
        if (e.id != id)
        {
            continue;
        }
        std::string line = e.text;
        for (char& c : line)
        {
            if (c == '\n' || c == '\t')
            {
                c = static_cast<char>(0x1f);
            }
        }
        std::fprintf(out, "%ld\t%ld\t%d\t%s\n", e.id, e.time, e.shown, line.c_str());
    }
    std::fclose(out);
}

// Removes notifications older than notifications_keep_days
void journal_prune(std::time_t now)
{
    long const cutoff = static_cast<long>(now) - 86400L * g_cfg.notifications_keep_days.load();
    pthread_mutex_lock(&g_journal_mutex);
    bool removed = false;
    for (JournalEntry& e : g_journal)
    {
        if (e.id != 0 && e.time < cutoff)
        {
            std::memset(&e, 0, sizeof(e));
            removed = true;
        }
    }
    if (removed)
    {
        journal_rewrite_locked();
    }
    pthread_mutex_unlock(&g_journal_mutex);
}

} // namespace ps5tm_core

using namespace ps5tm_core;


// ---------------------------------------------------------------- notification journal and port: for the settings page

extern "C" void ps5tm_journal_add(char const* text, int shown)
{
    journal_add(text, shown);
}

void ps5tm_announce(bool enabled, char const* text)
{
    announce(enabled, text);
}

std::vector<Ps5tmNotification> ps5tm_notifications_snapshot()
{
    std::vector<Ps5tmNotification> out;
    pthread_mutex_lock(&g_journal_mutex);
    long const first = g_journal_last_id > JournalSize ? g_journal_last_id - JournalSize + 1 : 1;
    for (long id = first; id <= g_journal_last_id; ++id)
    {
        JournalEntry const& e = g_journal[(id - 1) % JournalSize];
        if (e.id == id)
        {
            out.push_back({ e.id, e.time, e.shown == 1, e.text, e.shown });
        }
    }
    pthread_mutex_unlock(&g_journal_mutex);
    return out;
}

long ps5tm_notifications_latest()
{
    pthread_mutex_lock(&g_journal_mutex);
    long const id = g_journal_last_id;
    pthread_mutex_unlock(&g_journal_mutex);
    return id;
}

void ps5tm_notifications_clear()
{
    pthread_mutex_lock(&g_journal_mutex);
    std::memset(g_journal, 0, sizeof(g_journal));
    std::remove(JournalFile); // numbering continues: the page will not get confused
    pthread_mutex_unlock(&g_journal_mutex);
}
