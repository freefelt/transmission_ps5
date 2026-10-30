// Transmission for the PS5: the icon of the 'Media' tab: files, helper process and registration.
//
// License: GPL-3.0-or-later (like Transmission).

#include "ui_internal.h"
#include "icon0.h"

namespace ps5tm_ui
{

// ---------------------------------------------------------------- icon

static bool file_matches(char const* path, void const* data, std::size_t size)
{
    struct stat st;
    if (stat(path, &st) != 0 || static_cast<std::size_t>(st.st_size) != size)
    {
        return false;
    }
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr)
    {
        return false;
    }
    std::vector<unsigned char> buffer(size);
    bool const same = std::fread(buffer.data(), 1, size, f) == size && std::memcmp(buffer.data(), data, size) == 0;
    std::fclose(f);
    return same;
}

static bool write_file_atomic(char const* path, void const* data, std::size_t size)
{
    std::string const tmp = std::string{ path } + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (f == nullptr)
    {
        return false;
    }
    bool ok = std::fwrite(data, 1, size, f) == size;
    ok = std::fclose(f) == 0 && ok;
    if (!ok || std::rename(tmp.c_str(), path) != 0)
    {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

// Page port in the icon link: the actual one (first free), otherwise the configured one
static int icon_link_port()
{
    int const actual = g_settings_port_actual.load();
    return actual > 0 ? actual : g_cfg.settings_port.load();
}

static std::string param_json()
{
    return std::string{ "{\n    \"titleId\": \"" } + TitleId +
        "\",\n    \"applicationCategoryType\": 65536,\n    \"deeplinkUri\": \"http://127.0.0.1:" + std::to_string(icon_link_port()) +
        "/\",\n    \"localizedParameters\": {\n        \"defaultLanguage\": \"en-US\",\n        \"en-US\": {\n"
        "            \"titleName\": \"Transmission\"\n        }\n    }\n}";
}

// "Installed" = the app is registered in the system: the folder exists and so does the marker the manager writes only after
// the helper answers "ok". Files alone mean nothing: they are left over even from a failed attempt.
bool icon_installed()
{
    struct stat st;
    return stat(IconDir, &st) == 0 && stat(IconRegisteredFile, &st) == 0;
}

// Registered and nothing needs changing: the same param.json (link port) and the same image
static bool icon_up_to_date(std::string const& param)
{
    return icon_installed() && file_matches(IconRegisteredFile, param.data(), param.size()) && file_matches(IconParam, param.data(), param.size()) &&
        file_matches(IconFile, icon0_png, icon0_png_len);
}

// Registration and removal of the app are done by a separate short-lived helper process (transmission_i.elf): the work
// with libSceAppInstUtil happens there, so a possible hang does not affect the manager and Transmission. Request and answer
// are passed through files. "" = success, otherwise the reason.
static std::string icon_run_helper(char const* command)
{
    struct stat log_st;
    long const log_before = stat(IconLogFile, &log_st) == 0 ? static_cast<long>(log_st.st_size) : 0;
    std::remove(IconResultFile);
    if (!write_file_atomic(IconRequestFile, command, std::strlen(command)))
    {
        return std::string{ "could not write the request to the helper (errno " } + std::to_string(errno) + ")";
    }
    // "Attempt in progress" marker: if the helper hangs, it stays, and the manager will not repeat the installation on its own
    std::string const attempt = std::string{ command } + " " + std::to_string(static_cast<long>(std::time(nullptr))) + "\n";
    write_file_atomic(IconAttemptFile, attempt.data(), attempt.size());
    char err[220] = {};
    if (ps5tm_launch_elf(IconHelperFile, ps5tm_icon_helper_size(), err, sizeof(err)) != 0)
    {
        std::remove(IconAttemptFile); // the loader did not accept the ELF: the helper did not start
        return err;
    }
    for (int i = 0; i < 40; ++i) // wait up to 40 seconds for the answer
    {
        FILE* f = std::fopen(IconResultFile, "r");
        if (f != nullptr)
        {
            char line[200] = {};
            std::fgets(line, sizeof(line), f);
            std::fclose(f);
            std::remove(IconAttemptFile); // the helper answered: the attempt is over
            std::string text{ line };
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            {
                text.pop_back();
            }
            if (text == "ok")
            {
                return "";
            }
            if (text.compare(0, 7, "error: ") == 0)
            {
                text.erase(0, 7);
            }
            return text.empty() ? std::string{ "the helper returned an empty answer" } : text;
        }
        sleep(1);
    }
    // No answer. The helper writes to icon.log as the first line after starting: if the log did not grow, it did not reach main.
    int const pid = ps5tm_find_pid(IconHelperName);
    if (pid > 0)
    {
        kill(pid, SIGKILL); // do not touch other unnamed payload.elf processes: kill only the process with our own name
    }
    bool const reached_main = stat(IconLogFile, &log_st) == 0 && static_cast<long>(log_st.st_size) > log_before;
    return reached_main
        ? std::string{ "the helper (" } + IconHelperName + ") did not answer within 40 s: it hung while registering, details in logs/icon.log"
        : std::string{ "the helper hung while loading, before its own start (no entries in logs/icon.log): the system did not load its libraries. "
                       "payload.elf may remain in Active Processes, only a console reboot removes it" };
}

// "" = success (including "already installed and up to date"), otherwise the error text. changed = the helper actually ran.
// The manager prepares the app files (param.json, icon0.png); the helper registers them in the system.
static std::string icon_install_impl(bool& changed)
{
    changed = false;
    std::string const param = param_json();
    if (icon_up_to_date(param))
    {
        return "";
    }
    changed = true;
    bool const existed = icon_installed();
    struct stat dir_st;
    bool const dir_existed = stat(IconDir, &dir_st) == 0;
    if ((mkdir(IconDir, 0755) != 0 && errno != EEXIST) || (mkdir(IconSceSys, 0755) != 0 && errno != EEXIST))
    {
        return std::string{ "could not create the folder " } + IconDir + " (errno " + std::to_string(errno) + ")";
    }
    if (!write_file_atomic(IconParam, param.data(), param.size()) || !write_file_atomic(IconFile, icon0_png, icon0_png_len))
    {
        return std::string{ "could not write the icon files (errno " } + std::to_string(errno) + ")";
    }
    std::string const error = icon_run_helper("install");
    if (error.empty())
    {
        // registered: remember which param.json (it shows that the link is stale after a port change)
        if (!write_file_atomic(IconRegisteredFile, param.data(), param.size()))
        {
            return std::string{ "the app is registered, but the marker could not be written (errno " } + std::to_string(errno) + ")";
        }
    }
    else if (!existed && !dir_existed)
    {
        // registration failed: remove the new files so no garbage is left behind
        std::remove(IconParam);
        std::remove(IconFile);
        std::remove(IconSceSys);
        std::remove(IconDir);
    }
    return error;
}

static std::string icon_remove_impl(bool& changed)
{
    changed = false;
    struct stat dir_st;
    bool const registered = icon_installed();
    if (!registered && stat(IconDir, &dir_st) != 0)
    {
        return "";
    }
    changed = true;
    // not registered (files from a failed installation remain): nothing to tell the system, just remove the files
    std::string const error = registered ? icon_run_helper("remove") : std::string{};
    // what is left of the app after removal: remove it ourselves
    std::remove(IconRegisteredFile);
    std::remove(IconParam);
    std::remove(IconFile);
    std::remove(IconSceSys); // remove() also removes empty folders
    std::remove(IconDir);
    return error;
}

std::atomic<bool> g_icon_busy{ false };
char g_icon_error[700] = ""; // why the last operation failed (for the page); the Cyrillic messages are long
pthread_mutex_t g_icon_mutex = PTHREAD_MUTEX_INITIALIZER;

// Install or remove the icon in a separate thread: the page does not wait (the helper may take up to 40 seconds to answer).
// persist: remember the choice in config.cfg (an action from the page); at startup and on a port change the setting is not changed.
bool icon_start(std::string const& action, std::string const& why, bool persist, bool automatic)
{
    if (automatic)
    {
        struct stat st;
        if (stat(IconAttemptFile, &st) == 0)
        {
            // the previous attempt did not finish with an answer (the helper hung): do not retry on our own, so hung processes do not pile up
            char const* text = "the previous attempt did not finish (the helper hung): automatic installation is off, press Install on the page";
            shim_log("icon: automatic %s skipped (%s): an earlier attempt did not finish", action.c_str(), why.c_str());
            pthread_mutex_lock(&g_icon_mutex);
            std::snprintf(g_icon_error, sizeof(g_icon_error), "%s", text);
            pthread_mutex_unlock(&g_icon_mutex);
            ps5tm_journal_add("Icon: an earlier installation attempt did not finish; the automatic retry is disabled. Press Install on the settings page.", 2);
            return false;
        }
    }
    if (g_icon_busy.exchange(true))
    {
        return false;
    }
    std::thread(
        [action, why, persist]
        {
            bool changed = false;
            std::string const error = action == "install" ? icon_install_impl(changed) : icon_remove_impl(changed);
            pthread_mutex_lock(&g_icon_mutex);
            std::snprintf(g_icon_error, sizeof(g_icon_error), "%s", error.c_str());
            pthread_mutex_unlock(&g_icon_mutex);
            if (error.empty())
            {
                shim_log("icon: %s ok (%s)", action.c_str(), why.c_str());
                if (persist)
                {
                    g_cfg.install_icon = action == "install";
                    ps5tm_config_save();
                }
                if (changed)
                {
                    ps5tm_notify_text(ps5tm_tr(action == "install" ? "me_icon_installed" : "me_icon_removed").c_str());
                }
            }
            else
            {
                shim_log("icon: %s failed (%s): %s", action.c_str(), why.c_str(), error.c_str());
                ps5tm_notify_text(ps5tm_tr("me_icon_failed", { ps5tm_tr(action == "install" ? "me_w_install" : "me_w_remove"), error }).c_str());
            }
            g_icon_busy = false;
        })
        .detach();
    return true;
}

} // namespace ps5tm_ui
