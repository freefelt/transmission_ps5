// Transmission for the PS5: the files of the Transmission web interface (theme, icons, language).
//
// License: GPL-3.0-or-later (like Transmission).

#include "core_internal.h"

namespace ps5tm_core
{


// Web interface theme: we write the name for the daemon (it applies the theme on each of its starts), and we put the ready ps5-theme.css ourselves
// so that a setting change takes effect immediately, without restarting the daemon. The theme files are in web/ after the daemon's first run.
constexpr char const* WebThemeFile = "/data/Transmission/transmission/web_theme";

constexpr char const* WebIconsFile = "/data/Transmission/transmission/web_icons";
constexpr char const* WebLangFile = "/data/Transmission/transmission/web_lang.js"; // web translation dictionary: the daemon copies it to web/ps5-lang.js on every start

// Copies web/ps5-icons-<set>.css to web/ps5-icons.css (the daemon lays out the set files at start)
static void web_icons_apply()
{
    char const* const name = ps5tm_web_icons_name(g_cfg.web_icons.load());
    write_text_file(WebIconsFile, std::string{ name } + "\n");
    std::string const source = std::string{ WebDir } + "/ps5-icons-" + name + ".css";
    FILE* f = std::fopen(source.c_str(), "rb");
    if (f == nullptr)
    {
        return; // the daemon has not laid out the files yet: it will apply the set itself at start
    }
    std::string content;
    char buffer[8192];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0)
    {
        content.append(buffer, n);
    }
    std::fclose(f);
    std::string const target = std::string{ WebDir } + "/ps5-icons.css";
    if (!write_text_file(target.c_str(), content))
    {
        shim_log("web icons: cannot write %s (errno %d)", target.c_str(), errno);
    }
}

constexpr char const* GuardMessagesFile = "/data/Transmission/guard_messages.txt";

// Web interface translation dictionary (ps5-lang.js) and daemon rejection translations (guard_messages.txt) for the selected language.
static void web_lang_apply()
{
    write_text_file(GuardMessagesFile, ps5tm_guard_messages_text());
    std::string const dictionary = ps5tm_web_lang_js();
    write_text_file(WebLangFile, dictionary);
    struct stat web_st;
    if (stat(WebDir, &web_st) == 0)
    {
        std::string const target = std::string{ WebDir } + "/ps5-lang.js";
        if (!write_text_file(target.c_str(), dictionary))
        {
            shim_log("web language: cannot write %s (errno %d)", target.c_str(), errno);
        }
    }
}

void web_theme_apply()
{
    web_icons_apply();
    web_lang_apply();
    // disk panel flag: a file for the daemon (it writes ps5-config.js on every start) and ps5-config.js itself so that a change takes effect immediately
    int const diskbar_mode = g_cfg.web_diskbar.load();
    char const* const diskbar_name = ps5tm_web_diskbar_name(diskbar_mode);
    write_text_file(WebDiskbarFile, std::string{ diskbar_name } + "\n");
    char const* const openfolder_name = ps5tm_web_openfolder_name(g_cfg.web_openfolder.load());
    write_text_file(WebOpenFolderFile, std::string{ openfolder_name } + "\n");
    {
        struct stat web_st;
        if (stat(WebDir, &web_st) == 0)
        {
            std::string const target = std::string{ WebDir } + "/ps5-config.js";
            if (!write_text_file(target.c_str(), std::string{ "window.PS5_CONFIG={diskbar:" } + (diskbar_mode != 0 ? "true" : "false") + ",diskbarMode:\"" + diskbar_name + "\",openFolder:\"" + openfolder_name + "\"};\n"))
            {
                shim_log("web config: cannot write %s (errno %d)", target.c_str(), errno);
            }
        }
    }
    int const index = g_cfg.web_theme.load();
    char const* const name = ps5tm_web_theme_name(index);
    write_text_file(WebThemeFile, std::string{ name } + "\n");
    std::string content;
    {
        std::string const source = std::string{ WebDir } + "/ps5-theme-" + name + ".css";
        FILE* f = std::fopen(source.c_str(), "rb");
        if (f == nullptr)
        {
            return; // the daemon has not laid out the files yet: it will apply the theme itself at start
        }
        char buffer[8192];
        std::size_t n = 0;
        while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0)
        {
            content.append(buffer, n);
        }
        std::fclose(f);
    }
    std::string const target = std::string{ WebDir } + "/ps5-theme.css";
    if (!write_text_file(target.c_str(), content))
    {
        shim_log("web theme: cannot write %s (errno %d)", target.c_str(), errno);
    }
}

} // namespace ps5tm_core

using namespace ps5tm_core;


void ps5tm_web_theme_apply()
{
    web_theme_apply();
}
