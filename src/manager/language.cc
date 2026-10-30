// Transmission for the PS5: interface language: tables, messages, translation of the settings page and of the web client.
//
// License: GPL-3.0-or-later (like Transmission).

#include "ui_internal.h"
#include "ps5tm_lang_data.h" // kLangRuStrings, kLangEnStrings: the built-in Russian and English tables (created by 02_transmission.sh from locales/*.strings)

namespace ps5tm_ui
{

// ---------------------------------------------------------------- interface language (settings page)
// English and Russian are built into the ELF (kLangEnStrings, kLangRuStrings) and cannot be damaged. Other languages are files in
// /data/Transmission/locales/*.strings. The format is like Xcode .strings but without the trailing ";": "key" = "value";
// comments start with // or #. The first entry of a file is "LanguageName" = "name shown in the list".
// The English page is the source text of the page, unchanged. For another language every fragment of the page (the text between tags
// and quotes; comments are skipped, see lang_localize) that is EXACTLY an English text of the table is replaced by the translation with
// the same key; a fragment without a translation stays English. Only whole fragments that are in the table are replaced, so the code of the
// page is never touched: that is why a text must not be a bare lowercase word (d.state==='running' would be replaced too).
// tools/lang-tool.js checks the tables against the source: the same rules.
constexpr char const* LocalesDir = "/data/Transmission/locales";
constexpr std::size_t LangMaxFileSize = 262144;


// Message keys (console, journal, errors): me_ (previously English text) and mr_ (previously Russian); placeholders {1}..{9}.
static bool lang_is_message_key(std::string const& key)
{
    if (key.size() > 3 && key[0] == 'm' && (key[1] == 'e' || key[1] == 'r') && key[2] == '_')
    {
        return true; // me_*, mr_*: messages
    }
    if (key == "LanguageName")
    {
        return false;
    }
    for (char const c : key) // a web interface key: the English text itself (it has a space, a capital letter or a symbol), not a settings page "slug" made of [a-z0-9_]
    {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
        {
            return true;
        }
    }
    return false;
}

static bool utf8_valid(std::string const& s)
{
    std::size_t i = 0;
    while (i < s.size())
    {
        unsigned char const c = static_cast<unsigned char>(s[i]);
        std::size_t const len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (len == 0 || i + len > s.size())
        {
            return false;
        }
        for (std::size_t k = 1; k < len; ++k)
        {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80)
            {
                return false;
            }
        }
        i += len;
    }
    return true;
}

// mode 0: the English table (values as they are: they are the anchors to look for in the page text); 1: a built-in translation; 2: a user file.
// In modes 1 and 2 quotes in the values are replaced by typographic ones (the value is put into the page text as it is, also inside JS strings);
// in mode 2 the characters "\", "<", ">" are not allowed. Returns "" or an error code: size, utf8, syntax:N, first, name, chars:N, dup:key.
static std::string lang_parse(std::string text, int mode, LangData& out)
{
    if (text.size() > LangMaxFileSize)
    {
        return "size";
    }
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
    {
        text.erase(0, 3);
    }
    if (!utf8_valid(text))
    {
        return "utf8";
    }
    out = LangData{};
    std::size_t pos = 0;
    int line_no = 0;
    bool first = true;
    while (pos <= text.size())
    {
        auto const eol = text.find('\n', pos);
        std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = eol == std::string::npos ? text.size() + 1 : eol + 1;
        ++line_no;
        std::size_t a = 0;
        while (a < line.size() && (line[a] == ' ' || line[a] == '\t' || line[a] == '\r'))
        {
            ++a;
        }
        std::size_t b = line.size();
        while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r'))
        {
            --b;
        }
        line = line.substr(a, b - a);
        if (line.empty() || line[0] == '#' || line.compare(0, 2, "//") == 0)
        {
            continue;
        }
        std::size_t i = 0;
        auto const skip_spaces = [&]
        {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            {
                ++i;
            }
        };
        auto const read_string = [&](std::string& dst) -> bool
        {
            if (i >= line.size() || line[i] != '"')
            {
                return false;
            }
            ++i;
            for (; i < line.size(); ++i)
            {
                char const c = line[i];
                if (c == '\\')
                {
                    if (i + 1 >= line.size())
                    {
                        return false;
                    }
                    char const e = line[++i];
                    if (e != '"' && e != '\\' && e != 'n')
                    {
                        return false;
                    }
                    dst += e == 'n' ? '\n' : e;
                }
                else if (c == '"')
                {
                    ++i;
                    return true;
                }
                else
                {
                    dst += c;
                }
            }
            return false;
        };
        std::string key;
        std::string value;
        bool ok = read_string(key);
        skip_spaces();
        ok = ok && i < line.size() && line[i] == '=';
        if (ok)
        {
            ++i;
            skip_spaces();
            ok = read_string(value);
        }
        skip_spaces();
        if (ok && i < line.size() && line[i] == ';')
        {
            ++i; // a trailing ";" is allowed
            skip_spaces();
        }
        if (!ok || i != line.size() || key.empty() || key.size() > 1000 || value.size() > 4000)
        {
            return "syntax:" + std::to_string(line_no);
        }
        bool const message = lang_is_message_key(key);
        if (mode != 0 && message)
        {
            for (char const c : value) // console and journal messages: one entry may span several lines ("\n"), < > are allowed (this is text, not markup)
            {
                if (static_cast<unsigned char>(c) < 32 && c != '\n')
                {
                    return "chars:" + std::to_string(line_no);
                }
            }
        }
        else if (mode != 0)
        {
            std::string clean;
            for (std::size_t k = 0; k < value.size(); ++k)
            {
                char const c = value[k];
                if ((mode == 2 && (c == '\\' || c == '<' || c == '>')) || static_cast<unsigned char>(c) < 32)
                {
                    return "chars:" + std::to_string(line_no);
                }
                clean += c == '\'' ? std::string{ "\xE2\x80\x99" } : c == '"' ? std::string{ "\xE2\x80\x9D" } : std::string(1, c);
            }
            value = clean;
        }
        if (first)
        {
            if (key != "LanguageName")
            {
                return "first";
            }
            bool bad = value.empty() || value.size() > 80;
            for (char const c : value)
            {
                bad = bad || static_cast<unsigned char>(c) < 32 || (mode == 2 && (c == '\\' || c == '<' || c == '>'));
            }
            if (bad)
            {
                return "name";
            }
            out.name = value;
            first = false;
            continue;
        }
        if (!out.map.emplace(key, value).second)
        {
            std::string safe;
            for (char const c : key.substr(0, 40))
            {
                safe += (ps5tm_alnum(c) || c == '.' || c == '_' || c == '-') ? c : '?';
            }
            return "dup:" + safe;
        }
    }
    return first ? "first" : "";
}

struct LangEmbedded
{
    LangData ru;
    LangData en;
    std::map<std::string, std::string> key_by_text; // English fragment of the page -> key
    std::string error; // not empty: the built-in table is damaged (a build error), translation is off
};

static LangEmbedded const& lang_embedded()
{
    static LangEmbedded const emb = []
    {
        LangEmbedded e;
        std::string err = lang_parse(kLangEnStrings, 0, e.en);
        if (err.empty())
        {
            err = lang_parse(kLangRuStrings, 1, e.ru);
        }
        if (!err.empty())
        {
            e.error = err;
            shim_log("settings page: the embedded language tables are broken (%s): translation is off", err.c_str());
            return e;
        }
        for (auto const& entry : e.en.map)
        {
            if (!lang_is_message_key(entry.first)) // messages are not anchors of the page
            {
                e.key_by_text.emplace(entry.second, entry.first);
            }
        }
        e.ru.known = 0;
        for (auto const& entry : e.ru.map)
        {
            e.ru.known += e.en.map.count(entry.first);
        }
        return e;
    }();
    return emb;
}

static bool lang_file_name_ok(std::string const& n)
{
    if (n.size() < 9 || n.size() > 64 || n.compare(n.size() - 8, 8, ".strings") != 0 || n[0] == '.' || n.find("..") != std::string::npos)
    {
        return false;
    }
    for (char const c : n)
    {
        if (!(ps5tm_alnum(c) || c == '.' || c == '_' || c == '-'))
        {
            return false;
        }
    }
    return true;
}

// A file from the locales folder: reads and validates it. Returns "" or an error code (bad, notfound, read, size, utf8, syntax:N, first, name, chars:N, dup:key, empty).
static std::string lang_load_file(std::string const& file, LangData& out)
{
    if (!lang_file_name_ok(file))
    {
        return "bad";
    }
    FILE* f = std::fopen((std::string{ LocalesDir } + "/" + file).c_str(), "rb");
    if (f == nullptr)
    {
        return errno == ENOENT ? "notfound" : "read";
    }
    std::string text;
    char buf[8192];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && text.size() <= LangMaxFileSize)
    {
        text.append(buf, n);
    }
    bool const read_error = std::ferror(f) != 0;
    std::fclose(f);
    if (read_error)
    {
        return "read";
    }
    std::string const err = lang_parse(text, 2, out);
    if (!err.empty())
    {
        return err;
    }
    auto const& emb = lang_embedded();
    for (auto const& entry : out.map)
    {
        out.known += emb.en.map.count(entry.first);
        if (lang_is_message_key(entry.first)) // only the {n} placeholders present in the English text of this message are allowed
        {
            auto const en = emb.en.map.find(entry.first);
            for (std::size_t p = entry.second.find('{'); p != std::string::npos; p = entry.second.find('{', p + 1))
            {
                if (p + 2 < entry.second.size() + 0 && entry.second[p + 2] == '}' && entry.second[p + 1] >= '1' && entry.second[p + 1] <= '9')
                {
                    std::string const ph = entry.second.substr(p, 3);
                    if (en == emb.en.map.end() || en->second.find(ph) == std::string::npos)
                    {
                        std::string safe;
                        for (char const c : entry.first.substr(0, 40))
                        {
                            safe += (ps5tm_alnum(c) || c == '.' || c == '_' || c == '-') ? c : '?';
                        }
                        return "ph:" + safe;
                    }
                }
            }
        }
    }
    return out.known == 0 ? "empty" : "";
}

// Default language (first start and an unavailable saved language): Russian if the console language is Russian (code 8 from the helper), otherwise English.
// "" means the helper has not answered yet: the page is English this time, but the language is not remembered (we will ask again on the next open).
std::string lang_default()
{
    char const* const forced = std::getenv("PS5TM_TEST_LANG"); // host checks set the language so the page stays Russian
    if (forced != nullptr && (std::strcmp(forced, "ru") == 0 || std::strcmp(forced, "en") == 0))
    {
        return forced;
    }
    int const code = ps5tm_system_language(); // Russian is code 8 (confirmed on the console), any other code: English
    if (code < 0)
    {
        return ""; // the helper has not answered yet: the language is still unknown
    }
    return code == 8 ? "ru" : "en";
}

// Validation of the value selected in the list (for saving the settings): "" or an error code.
std::string lang_validate_choice(std::string const& value)
{
    if (value == "ru" || value == "en")
    {
        return "";
    }
    if (value.compare(0, 5, "file:") != 0)
    {
        return "bad";
    }
    LangData d;
    return lang_load_file(value.substr(5), d);
}

// ---- Messages in the selected language. ps5tm_tr("me_key", {a, b}): the key value with {1}, {2}... substituted.
// Lookup order: the language (user file, ru or en) -> English -> Russian -> the key itself. Test mode PS5TM_TEST_MSG=mixed:
// me_ gives the English text, mr_ the Russian one (as before the messages were translated), so the old text checks do not change.
static void replace_all(std::string& s, std::string const& from, std::string const& to);

static std::mutex g_msg_mutex;
static std::string g_msg_id; // which language g_msg_file is loaded for
static LangData g_msg_file;
static long g_msg_file_mtime = 0;

static std::string ps5tm_tr_impl(char const* key, std::initializer_list<std::string> args)
{
    auto const& emb = lang_embedded();
    LangData const* primary = nullptr;
    char const* const mode = std::getenv("PS5TM_TEST_MSG");
    if (mode != nullptr && std::strcmp(mode, "mixed") == 0)
    {
        primary = key[1] == 'r' ? &emb.ru : &emb.en; // mr_ Russian, me_ and web interface keys (w_, wp_, wf_) English
    }
    else
    {
        std::string id = ps5tm_language();
        if (id.empty())
        {
            id = lang_default();
        }
        std::lock_guard<std::mutex> lock(g_msg_mutex);
        if (id == "ru")
        {
            primary = &emb.ru;
        }
        else if (id.compare(0, 5, "file:") == 0)
        {
            struct stat st;
            std::string const path = std::string{ LocalesDir } + "/" + id.substr(5);
            long const mtime = stat(path.c_str(), &st) == 0 ? static_cast<long>(st.st_mtime) : -1;
            if (g_msg_id != id || g_msg_file_mtime != mtime)
            {
                g_msg_file = LangData{};
                g_msg_id = id;
                g_msg_file_mtime = mtime;
                if (!lang_load_file(id.substr(5), g_msg_file).empty())
                {
                    g_msg_file = LangData{};
                }
            }
            primary = &g_msg_file;
        }
        else
        {
            primary = &emb.en;
        }
    }
    std::string const k = key;
    std::string value;
    auto found = primary->map.find(k);
    if (found != primary->map.end())
    {
        value = found->second;
    }
    else if ((found = emb.en.map.find(k)) != emb.en.map.end())
    {
        value = found->second;
    }
    else if ((found = emb.ru.map.find(k)) != emb.ru.map.end())
    {
        value = found->second;
    }
    else
    {
        return k;
    }
    int n = 1;
    for (auto const& a : args)
    {
        replace_all(value, std::string{ "{" } + static_cast<char>('0' + n) + "}", a);
        ++n;
    }
    return value;
}

// ---- Transmission web interface (port 9091) and daemon rejections in the selected language.
// The keys w_ (string), wp_ (template with {1}..) and wf_ (fragment) live in the same .strings files. For each entry BOTH texts (Russian and English) are anchors: the client
// (ps5-i18n.js) looks for one or the other in the interface and substitutes the text of the selected language (user file, then English, then Russian).
static std::string js_quote(std::string const& s)
{
    std::string r = "\"";
    for (char const c : s)
    {
        if (c == '"' || c == '\\')
        {
            r += '\\';
            r += c;
        }
        else if (c == '\n')
        {
            r += "\\n";
        }
        else if (static_cast<unsigned char>(c) >= 32)
        {
            r += c;
        }
    }
    return r + "\"";
}

static std::string ps5tm_web_lang_js_impl()
{
    char const* const test_mode = std::getenv("PS5TM_TEST_MSG");
    if (test_mode != nullptr && std::strcmp(test_mode, "mixed") == 0)
    {
        return "window.PS5_I18N=null;\n"; // test mode: web interface without translation (English, as in the client code)
    }
    auto const& emb = lang_embedded();
    std::string id = ps5tm_language();
    if (id.empty())
    {
        id = lang_default();
    }
    std::string map = "{";
    bool first = true;
    for (auto const& kv : emb.ru.map)
    {
        if (kv.first.compare(0, 3, "me_") == 0 || kv.first.compare(0, 3, "mr_") == 0 || !lang_is_message_key(kv.first))
        {
            continue; // only English web interface texts (the key is the text itself)
        }
        std::string const target = ps5tm_tr_impl(kv.first.c_str(), {});
        if (target == kv.first || target.empty())
        {
            continue;
        }
        map += std::string{ first ? "" : "," } + js_quote(kv.first) + ":" + js_quote(target);
        first = false;
    }
    return "window.PS5_I18N={lang:" + js_quote(id) + ",plural:" + js_quote(id == "ru" ? "ru" : "en") + ",map:" + map + "}};\n";
}

static std::string ps5tm_guard_messages_text_impl()
{
    auto const& emb = lang_embedded();
    std::string out;
    for (auto const& kv : emb.en.map)
    {
        if (kv.first.compare(0, 5, "me_g_") != 0)
        {
            continue;
        }
        std::string const target = ps5tm_tr_impl(kv.first.c_str(), {});
        if (target == kv.second)
        {
            continue; // English: the daemon uses its own text
        }
        std::string legacy = kv.second;
        replace_all(legacy, "{1}", "%s");
        out += legacy + "\t" + target + "\n";
    }
    return out;
}

struct LangListing
{
    std::vector<std::pair<std::string, std::string>> items; // (id, name for the list)
    std::vector<std::pair<std::string, std::string>> skipped; // (file, error code)
};

static LangListing lang_list()
{
    LangListing l;
    auto const& emb = lang_embedded();
    l.items.emplace_back("ru", emb.ru.name.empty() ? std::string{ "Русский" } : emb.ru.name);
    l.items.emplace_back("en", emb.en.name.empty() ? std::string{ "English" } : emb.en.name);
    std::vector<std::string> files;
    if (DIR* d = opendir(LocalesDir))
    {
        while (struct dirent* e = readdir(d))
        {
            std::string const n = e->d_name;
            if (n.size() > 8 && n.compare(n.size() - 8, 8, ".strings") == 0 && files.size() < 30)
            {
                files.push_back(n);
            }
        }
        closedir(d);
    }
    std::sort(files.begin(), files.end());
    for (auto const& file : files)
    {
        LangData d;
        std::string const err = lang_load_file(file, d);
        if (err.empty())
        {
            l.items.emplace_back("file:" + file, d.name);
        }
        else
        {
            l.skipped.emplace_back(file.substr(0, 64), err);
        }
    }
    return l;
}


// The saved language when the page opens: empty: the default language (and it is remembered); invalid: the default language (and it is remembered), the page reports why.
static LangResolved lang_resolve()
{
    LangResolved r;
    std::string const saved = ps5tm_language();
    std::string const def = lang_default();
    auto const use_default = [&]
    {
        r.id = def.empty() ? std::string{ "en" } : def;
        if (!def.empty()) // the console language is unknown: English this time, but do not remember it
        {
            ps5tm_config_set("language", def);
            ps5tm_config_save();
            ps5tm_web_theme_apply(); // Transmission web interface and daemon rejections in the remembered language
        }
    };
    if (saved.empty())
    {
        use_default();
    }
    else if (saved == "ru" || saved == "en")
    {
        r.id = saved;
    }
    else if (saved.compare(0, 5, "file:") == 0)
    {
        std::string const err = lang_load_file(saved.substr(5), r.file);
        if (err.empty())
        {
            r.id = saved;
        }
        else
        {
            r.fallback = saved.substr(5);
            r.reason = err;
            use_default();
        }
    }
    else
    {
        r.fallback = saved;
        r.reason = "bad";
        use_default();
    }
    return r;
}

// Replaces the fragments of the page by their translation. A fragment is the text between the delimiters ' " < > and the end of a line;
// the comments /* */ and // (after a blank or ;{}) and the escaped \' and \" are skipped. Only a fragment that is exactly an English
// text of the table (and has a translation in tr) is replaced.
static std::string lang_localize(std::string const& page, LangEmbedded const& emb, LangData const& tr, int& translated)
{
    std::string out;
    out.reserve(page.size() + 4096);
    std::size_t const n = page.size();
    std::size_t i = 0;
    std::size_t seg = 0;
    auto const flush = [&](std::size_t end)
    {
        if (end > seg)
        {
            auto const k = emb.key_by_text.find(page.substr(seg, end - seg));
            if (k != emb.key_by_text.end())
            {
                auto const t = tr.map.find(k->second);
                if (t != tr.map.end())
                {
                    out += t->second;
                    ++translated;
                    seg = end;
                    return;
                }
            }
            out.append(page, seg, end - seg);
        }
        seg = end;
    };
    while (i < n)
    {
        char const ch = page[i];
        if (ch == '\\' && i + 1 < n && (page[i + 1] == '\'' || page[i + 1] == '"'))
        {
            i += 2;
            continue;
        }
        if (ch == '/' && i + 1 < n && page[i + 1] == '*')
        {
            flush(i);
            auto e = page.find("*/", i + 2);
            e = e == std::string::npos ? n : e + 2;
            out.append(page, i, e - i);
            i = e;
            seg = i;
            continue;
        }
        if (ch == '/' && i + 1 < n && page[i + 1] == '/' && (i == 0 || std::strchr(" \t\n;{}", page[i - 1]) != nullptr))
        {
            flush(i);
            auto e = page.find('\n', i);
            e = e == std::string::npos ? n : e;
            out.append(page, i, e - i);
            i = e;
            seg = i;
            continue;
        }
        if (ch == '\'' || ch == '"' || ch == '<' || ch == '>' || ch == '\n')
        {
            flush(i);
            out += ch;
            ++i;
            seg = i;
            continue;
        }
        ++i;
    }
    flush(n);
    return out;
}

static std::string lang_js_safe(std::string const& s)
{
    std::string r;
    for (char const c : s.substr(0, 80))
    {
        r += (ps5tm_alnum(c) || c == '.' || c == '_' || c == '-' || c == ':') ? c : '?';
    }
    return r;
}

static void replace_all(std::string& s, std::string const& from, std::string const& to)
{
    for (std::size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size()))
    {
        s.replace(p, from.size(), to);
    }
}

// The settings page for the selected language: translation of fragments, then the language list and the unavailable-language message (after translation: language names are not translated).
std::string build_settings_page(char const* source)
{
    LangResolved const r = lang_resolve();
    auto const& emb = lang_embedded();
    std::string page = source;
    std::string code = r.id == "ru" ? "ru" : "en";
    if (r.id != "en" && emb.error.empty())
    {
        int translated = 0;
        page = lang_localize(page, emb, r.id == "ru" ? emb.ru : r.file, translated);
        shim_log("settings page: language %s: %d fragments translated", r.id.c_str(), translated);
    }
    if (r.id.compare(0, 5, "file:") == 0)
    {
        code = lang_js_safe(r.id.substr(5, r.id.size() - 13));
    }
    replace_all(page, "<html lang=\"en\">", "<html lang=\"" + code + "\">");
    LangListing const l = lang_list();
    std::string opts = "[";
    for (std::size_t i = 0; i < l.items.size(); ++i)
    {
        opts += std::string{ i ? "," : "" } + "['" + l.items[i].first + "','" + l.items[i].second + "']";
    }
    opts += "]";
    std::string info = "{fallback:'" + lang_js_safe(r.fallback) + "',reason:'" + lang_js_safe(r.reason) + "',skipped:[";
    for (std::size_t i = 0; i < l.skipped.size(); ++i)
    {
        info += std::string{ i ? "," : "" } + "['" + lang_js_safe(l.skipped[i].first) + "','" + lang_js_safe(l.skipped[i].second) + "']";
    }
    info += "]}";
    replace_all(page, "@@LANG_OPTS@@", opts);
    replace_all(page, "@@LANG_INFO@@", info);
    return page;
}

} // namespace ps5tm_ui

// The public functions of the settings server (declared in ps5tm_cfg.h)
using namespace ps5tm_ui;

std::string ps5tm_tr(char const* key, std::initializer_list<std::string> args)
{
    return ps5tm_tr_impl(key, args);
}

std::string ps5tm_web_lang_js()
{
    return ps5tm_web_lang_js_impl();
}

std::string ps5tm_guard_messages_text()
{
    return ps5tm_guard_messages_text_impl();
}
