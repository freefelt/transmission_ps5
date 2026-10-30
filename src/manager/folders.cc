// Transmission for the PS5: download folders: validation, list, add, remove, unmount, folder browser.
//
// License: GPL-3.0-or-later (like Transmission).

#include "ui_internal.h"

namespace ps5tm_ui
{

pthread_mutex_t g_folders_edit_mutex = PTHREAD_MUTEX_INITIALIZER;


struct Volume
{
    bool dir = false;
    bool mounted = false;
    double total = 0;
    double free = 0;
};

static Volume probe_volume(std::string const& path)
{
    Volume v;
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
    {
        return v;
    }
    v.dir = true;
    struct statvfs sv;
    if (statvfs(path.c_str(), &sv) == 0)
    {
        double const frsize = sv.f_frsize != 0 ? static_cast<double>(sv.f_frsize) : static_cast<double>(sv.f_bsize);
        v.total = static_cast<double>(sv.f_blocks) * frsize;
        v.free = static_cast<double>(sv.f_bavail) * frsize;
    }
    struct stat parent;
    v.mounted = stat("/mnt", &parent) == 0 && st.st_dev != parent.st_dev && v.total > 0;
    return v;
}

// Parse and normalize the path: "" if all is well (comps is filled), otherwise the error text for the user.
std::string normalize_folder(std::string in, std::vector<std::string>& comps)
{
    while (!in.empty() && (in.back() == ' ' || in.back() == '\t'))
    {
        in.pop_back();
    }
    std::size_t begin = 0;
    while (begin < in.size() && (in[begin] == ' ' || in[begin] == '\t'))
    {
        ++begin;
    }
    in = in.substr(begin);
    if (in.empty())
    {
        return ps5tm_tr("mr_f_enter");
    }
    if (in[0] != '/')
    {
        return ps5tm_tr("mr_f_slash");
    }
    if (in.size() > 300)
    {
        return ps5tm_tr("mr_f_long");
    }
    for (unsigned char const c : in)
    {
        if (c < 0x20 || c == 0x7f)
        {
            return ps5tm_tr("mr_f_chars");
        }
    }
    if (in.find(" #") != std::string::npos)
    {
        return ps5tm_tr("mr_f_space");
    }
    comps.clear();
    std::size_t i = 0;
    while (i <= in.size())
    {
        auto j = in.find('/', i);
        if (j == std::string::npos)
        {
            j = in.size();
        }
        std::string const part = in.substr(i, j - i);
        if (part == "..")
        {
            return ps5tm_tr("mr_f_dots");
        }
        if (!part.empty() && part != ".")
        {
            comps.push_back(part);
        }
        i = j + 1;
    }
    if (comps.empty())
    {
        return ps5tm_tr("mr_f_root");
    }
    return "";
}

static std::string join_path(std::vector<std::string> const& comps, std::size_t count)
{
    std::string out;
    for (std::size_t i = 0; i < count && i < comps.size(); ++i)
    {
        out += "/" + comps[i];
    }
    return out;
}

// 1 = M.2 SSD (/mnt/extN), 2 = USB stick (/mnt/usbN), 3 = internal disk (/data/...), 0 = another place. root: the disk root.
int folder_kind(std::vector<std::string> const& c0, std::string& root)
{
    // The path may be an alias (BindMount: /data/ext1 is /mnt/ext1): first the real path, then the disk kind. Nothing is hardcoded: this is asked from the system.
    std::vector<std::string> c = c0;
    {
        std::string const full = join_path(c0, c0.size());
        char real[600];
        if (ps5tm_resolve_alias(full.c_str(), real, sizeof(real)) != 0 && real[0] == '/')
        {
            std::vector<std::string> parts;
            std::string const rs{ real };
            std::size_t i = 0;
            while (i <= rs.size())
            {
                auto j = rs.find('/', i);
                if (j == std::string::npos)
                {
                    j = rs.size();
                }
                if (j > i)
                {
                    parts.push_back(rs.substr(i, j - i));
                }
                i = j + 1;
            }
            if (!parts.empty())
            {
                c = parts;
            }
        }
    }
    if (c[0] == "mnt" && c.size() >= 2 && c[1].size() == 4 && c[1][3] >= '0' && c[1][3] <= '9' && (c[1].compare(0, 3, "ext") == 0 || c[1].compare(0, 3, "usb") == 0))
    {
        root = "/mnt/" + c[1];
        return ps5tm_disk_kind_of_root(root.c_str()); // by device: 1 = M.2 SSD (/dev/ssd*), 2 = USB (/dev/es*.crypt, /dev/da*); the mount point name does not determine the kind
    }
    if (c[0] == "data" && c.size() >= 2)
    {
        root = "/data";
        return 3;
    }
    return 0;
}

static char const* kind_name(int kind)
{
    return kind == 1 ? "ssd" : kind == 2 ? "usb" : kind == 3 ? "int" : "other";
}

static std::string lower_ascii(std::string s)
{
    for (char& ch : s)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

// Adding: "" = done; need_confirm = true if confirmation is needed (internal disk).
std::string folder_add(std::string const& path_in, bool confirm, bool& need_confirm, std::string& normalized, std::string& reason)
{
    need_confirm = false;
    reason.clear();
    std::vector<std::string> c;
    std::string const err = normalize_folder(path_in, c);
    if (!err.empty())
    {
        return err;
    }
    normalized = join_path(c, c.size());
    std::string root;
    int const kind = folder_kind(c, root);
    if (kind == 0)
    {
        return ps5tm_tr("mr_f_allowed");
    }
    if (kind == 3 && c[1] == "Transmission")
    {
        return ps5tm_tr("mr_f_appdir");
    }
    FolderLock lock;
    std::vector<std::string> list = ps5tm_folders_get();
    for (auto const& f : list)
    {
        if (lower_ascii(f) == lower_ascii(normalized))
        {
            return ps5tm_tr("mr_f_dup");
        }
    }
    if (kind != 3)
    {
        if (!probe_volume(root).mounted)
        {
            return ps5tm_tr("mr_f_unmounted", { root });
        }
        if (!confirm && volume_fs(root).bad)
        {
            need_confirm = true;
            reason = "fs"; // exFAT/FAT32: a warning with an explanation (hangs, advice to format with the console tools)
            return ps5tm_tr("mr_f_confirm_fs");
        }
    }
    else if (!confirm)
    {
        need_confirm = true;
        reason = "int";
        return ps5tm_tr("mr_f_confirm");
    }
    if (list.size() >= FolderLimit)
    {
        return ps5tm_tr("mr_f_limit");
    }
    std::vector<std::string> const before = list;
    list.push_back(normalized);
    ps5tm_folders_set(list);
    if (!ps5tm_config_save())
    {
        ps5tm_folders_set(before);
        return ps5tm_tr("mr_f_save");
    }
    ps5tm_journal_add(ps5tm_tr("mr_folder_added", { normalized }).c_str(), 2);
    return "";
}

std::string folder_remove(std::string const& path_in)
{
    FolderLock lock;
    std::vector<std::string> list = ps5tm_folders_get();
    std::size_t found = list.size();
    for (std::size_t i = 0; i < list.size() && found == list.size(); ++i)
    {
        if (list[i] == path_in)
        {
            found = i; // exact match: this way even a strange hand-written line can be removed
        }
    }
    if (found == list.size())
    {
        std::vector<std::string> c;
        if (normalize_folder(path_in, c).empty())
        {
            std::string const normalized = join_path(c, c.size());
            for (std::size_t i = 0; i < list.size() && found == list.size(); ++i)
            {
                if (lower_ascii(list[i]) == lower_ascii(normalized))
                {
                    found = i;
                }
            }
        }
    }
    if (found == list.size())
    {
        return ps5tm_tr("mr_f_nolist");
    }
    std::string const removed = list[found];
    std::vector<std::string> const before = list;
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(found));
    ps5tm_folders_set(list);
    if (!ps5tm_config_save())
    {
        ps5tm_folders_set(before);
        return ps5tm_tr("mr_f_save");
    }
    // The folder that was the chosen default is gone: forget the choice, so that adding the folder again does not make it the default again (the daemon keeps the choice in this file).
    if (FILE* f = std::fopen("/data/Transmission/transmission/default_folder.txt", "r"))
    {
        char line[400] = {};
        if (std::fgets(line, sizeof(line), f) == nullptr)
        {
            line[0] = 0;
        }
        std::fclose(f);
        line[std::strcspn(line, "\r\n")] = 0;
        if (lower_ascii(line) == lower_ascii(removed))
        {
            std::remove("/data/Transmission/transmission/default_folder.txt");
        }
    }
    ps5tm_journal_add(ps5tm_tr("mr_folder_removed", { removed }).c_str(), 2);
    return "";
}

static std::string num0(double v)
{
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.0f", v);
    return buf;
}

// List for the page: folders (with disk state) and the connected disks found, /mnt/extN, /mnt/usbN (of any size).
std::string folders_json()
{
    std::string out = "{\"limit\":" + std::to_string(FolderLimit) + ",\"folders\":[";
    bool first = true;
    for (auto const& path : ps5tm_folders_get())
    {
        std::vector<std::string> c;
        std::string root;
        int kind = 0;
        if (normalize_folder(path, c).empty())
        {
            kind = folder_kind(c, root);
        }
        Volume v;
        bool mounted = false;
        if (kind == 1 || kind == 2)
        {
            v = probe_volume(root);
            mounted = v.mounted;
        }
        else
        {
            v = probe_volume(kind == 3 ? root : path);
            mounted = v.dir;
        }
        struct stat st;
        bool const exists = stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        FsInfo const fsi = (kind == 1 || kind == 2) && mounted ? volume_fs(root) : FsInfo{};
        out += std::string{ first ? "" : "," } + "{\"path\":" + jstr(path) + ",\"kind\":\"" + kind_name(kind) + "\",\"mounted\":" + jbool(mounted) + ",\"exists\":" + jbool(exists) +
               ",\"fs\":" + jstr(fsi.name) + ",\"fsbad\":" + jbool(fsi.bad) + ",\"device\":" + jstr(fsi.device) + ",\"total\":" + num0(v.total) + ",\"free\":" + num0(v.free) + "}";
        first = false;
    }
    out += "],\"candidates\":[";
    first = true;
    for (char const* const prefix : { "/mnt/ext", "/mnt/usb" })
    {
        for (int i = 0; i < 8; ++i)
        {
            std::string const root = std::string{ prefix } + std::to_string(i);
            Volume const v = probe_volume(root);
            if (!v.mounted)
            {
                continue;
            }
            int const kind = ps5tm_disk_kind_of_root(root.c_str()); // by device: extended storage on USB (/mnt/ext0, /dev/es0.crypt) is USB, not M.2
            FsInfo const fsi = volume_fs(root);
            out += std::string{ first ? "" : "," } + "{\"root\":" + jstr(root) + ",\"kind\":\"" + kind_name(kind) + "\",\"fs\":" + jstr(fsi.name) + ",\"fsbad\":" + jbool(fsi.bad) + ",\"total\":" + num0(v.total) +
                   ",\"free\":" + num0(v.free) + "}";
            first = false;
        }
    }
    out += "]}";
    return out;
}

// "Unmount disk" for a folder on USB (including USB as extended storage /mnt/extN): the disk's torrents are stopped, the links to the disk are unmounted,
// and then the system is asked to disconnect the drive, the way "Safely remove from PS5" does (usb_eject.cc); this is the only right way for the console's
// extended storage. Where the system eject is not available, an ordinary unmount of all partitions of the disk is used (not for the extended storage).
// 0 = done (message), 1 = busy (holders: JSON array of those holding it), 2 = error (message). force: sync and a forced unmount (not for the extended storage).
int folder_unmount(std::string const& path_in, bool force, std::string& message, std::string& holders)
{
    std::vector<std::string> comps;
    std::string const bad = normalize_folder(path_in, comps);
    if (!bad.empty())
    {
        message = bad;
        return 2;
    }
    std::string root;
    if (folder_kind(comps, root) != 2)
    {
        message = ps5tm_tr("mr_um_only_usb");
        return 2;
    }
    if (!probe_volume(root).mounted)
    {
        message = ps5tm_tr("mr_um_not_mounted");
        return 2;
    }
    bool const ext = volume_fs(root).device.compare(0, 7, "/dev/es") == 0; // the console's extended storage: only the system may disconnect it
    if (ext && force)
    {
        message = ps5tm_tr("mr_um_ext_fail", { root });
        return 2;
    }
    UnmountPlan const up = unmount_plan_parts(root);
    std::vector<std::string> plan = up.aliases;
    for (auto it = up.parts.rbegin(); it != up.parts.rend(); ++it) // partitions from the end: a partition with a higher number is usually mounted later
    {
        plan.push_back(*it);
    }
    std::string detail;
    int const stopped = ps5tm_stop_torrents_under(plan, detail);
    shim_log("unmount%s: %s: plan %zu mounts; %s", force ? " (forced)" : "", root.c_str(), plan.size(), detail.c_str());
    std::string done;
    // A failed ordinary unmount of one mount: the message, and for "busy" the list of those holding the disk.
    auto const failed = [&](std::string const& m, int e) -> int
    {
        shim_log("unmount: %s failed: errno %d", m.c_str(), e);
        message = ps5tm_tr(e == EBUSY ? "mr_um_busy" : "mr_um_fail", { m, errno_text(e) });
        ps5tm_journal_add(message.c_str(), 2);
        if (e == EBUSY && !force)
        {
            holders = holders_json(plan);
            return 1;
        }
        return 2;
    };
    for (auto const& m : up.aliases) // the links are ours to unmount in every case: the system does not know them
    {
        if (unmount_one(m, force) != 0)
        {
            return failed(m, errno);
        }
        done += (done.empty() ? "" : ", ") + m;
    }
    bool disconnected = false; // the system disconnected the drive
    if (!force && usb_system_eject_available())
    {
        int native = 0;
        int rc = usb_system_eject(root, native);
        for (int i = 0; rc == 0 && i < 50 && probe_volume(root).mounted; ++i) // the request only queues the work: wait until the mount is really gone
        {
            usleep(100000);
        }
        if (rc == 0 && probe_volume(root).mounted)
        {
            rc = ETIMEDOUT;
        }
        if (rc == 0)
        {
            disconnected = true;
            done += (done.empty() ? "" : ", ") + root;
            for (auto it = up.parts.rbegin(); it != up.parts.rend(); ++it) // the system should have taken the other partitions too; whatever is left is unmounted here
            {
                if (*it != root && probe_volume(*it).mounted && unmount_one(*it, false) == 0)
                {
                    done += ", " + *it;
                }
            }
        }
        else
        {
            char hex[16];
            std::snprintf(hex, sizeof(hex), "0x%08x", static_cast<unsigned>(native));
            shim_log("unmount: the system eject of %s failed: errno %d, native %s", root.c_str(), rc, hex);
            if (rc == EBUSY && !ext)
            {
                message = ps5tm_tr("mr_um_busy", { root, errno_text(rc) });
                ps5tm_journal_add(message.c_str(), 2);
                holders = holders_json(plan);
                return 1;
            }
            if (ext || rc == EBUSY)
            {
                // whatever the reason (not listed, busy, an error), the answer for the user is the same: do it with the console itself; the details are in the log above
                message = ps5tm_tr("mr_um_ext_fail", { root });
                ps5tm_journal_add(message.c_str(), 2);
                return 2;
            }
            // an ordinary drive: the system could not be asked (or refused for another reason), the old way below
        }
    }
    else if (ext)
    {
        message = ps5tm_tr("mr_um_ext_fail", { root }); // no way to ask the system: the extended storage is left alone (an ordinary unmount makes the console think the drive was removed wrongly)
        return 2;
    }
    if (!disconnected)
    {
        for (auto it = up.parts.rbegin(); it != up.parts.rend(); ++it)
        {
            if (unmount_one(*it, force) != 0)
            {
                return failed(*it, errno);
            }
            done += (done.empty() ? "" : ", ") + *it;
        }
    }
    message = ext ? ps5tm_tr("mr_um_ok_ext", { root, std::to_string(stopped < 0 ? 0 : stopped) })
                  : ps5tm_tr(force ? "mr_um_ok_forced" : "mr_um_ok", { root, done, std::to_string(stopped < 0 ? 0 : stopped) });
    ps5tm_journal_add(message.c_str(), 2);
    return 0;
}
// ---------------------------------------------------------------- folder picker: list of subfolders and folder creation
// Only places where download folders may be added are allowed: external disks (/mnt/extN, /mnt/usbN) and /data (by the same rules as folder_add).
// Reading the disk may take long or hang: the call goes through run_isolated with a time limit.
std::string fs_error(std::string const& text)
{
    return "{\"error\":" + jstr(text) + "}";
}

// "" = the path is allowed; otherwise the error text. path is normalized.
static std::string fs_check_path(std::string const& in, std::string& path, std::string& root)
{
    std::vector<std::string> comps;
    std::string const bad = normalize_folder(in, comps);
    if (!bad.empty())
    {
        return bad;
    }
    if (comps.size() == 1 && comps[0] == "data")
    {
        root = "/data"; // /data itself can be browsed (a folder inside it is chosen), but it is not suitable as a download folder: folder_add checks that
        path = "/data";
        return "";
    }
    if (folder_kind(comps, root) == 0)
    {
        return ps5tm_tr("mr_fs_outside");
    }
    path = join_path(comps, comps.size());
    return "";
}

std::string fs_list_json(std::string const in)
{
    std::string path;
    std::string root;
    std::string const bad = fs_check_path(in, path, root);
    if (!bad.empty())
    {
        return fs_error(bad);
    }
    DIR* d = opendir(path.c_str());
    if (d == nullptr)
    {
        return fs_error(ps5tm_tr("mr_fs_nodir"));
    }
    std::vector<std::string> dirs;
    bool truncated = false;
    while (dirent* e = readdir(d))
    {
        std::string const name = e->d_name;
        if (name.empty() || name[0] == '.' || name == "lost+found" || name == "System Volume Information" || name == "$RECYCLE.BIN" || (path == "/data" && name == "Transmission"))
        {
            continue;
        }
        bool is_dir = e->d_type == DT_DIR;
        if (e->d_type == DT_UNKNOWN || e->d_type == DT_LNK)
        {
            struct stat st;
            is_dir = stat((path + "/" + name).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        }
        if (!is_dir)
        {
            continue;
        }
        if (dirs.size() >= 5000)
        {
            truncated = true;
            break;
        }
        dirs.push_back(name);
    }
    closedir(d);
    std::sort(dirs.begin(), dirs.end(),
              [](std::string const& a, std::string const& b)
              {
                  std::string la = a;
                  std::string lb = b;
                  for (auto& c : la)
                  {
                      c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
                  }
                  for (auto& c : lb)
                  {
                      c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
                  }
                  return la != lb ? la < lb : a < b;
              });
    if (dirs.size() > 500)
    {
        dirs.resize(500); // show the first 500 alphabetically
        truncated = true;
    }
    std::string out = "{\"path\":" + jstr(path) + ",\"root\":" + jstr(root) + ",\"dirs\":[";
    for (std::size_t i = 0; i < dirs.size(); ++i)
    {
        out += std::string{ i != 0 ? "," : "" } + jstr(dirs[i]);
    }
    return out + "],\"truncated\":" + jbool(truncated) + "}";
}

std::string fs_mkdir_json(std::string const in, std::string name)
{
    std::string path;
    std::string root;
    std::string const bad = fs_check_path(in, path, root);
    if (!bad.empty())
    {
        return fs_error(bad);
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
    {
        name.pop_back();
    }
    std::size_t begin = 0;
    while (begin < name.size() && (name[begin] == ' ' || name[begin] == '\t'))
    {
        ++begin;
    }
    name = name.substr(begin);
    bool name_ok = !name.empty() && name.size() <= 120 && name[0] != '.';
    for (unsigned char const c : name)
    {
        name_ok = name_ok && c >= 0x20 && c != 0x7f && c != '/' && c != '\\';
    }
    if (!name_ok)
    {
        return fs_error(ps5tm_tr("mr_fs_name"));
    }
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
    {
        return fs_error(ps5tm_tr("mr_fs_nodir"));
    }
    std::string const full = path + "/" + name;
    if (mkdir(full.c_str(), 0777) != 0)
    {
        int const e = errno;
        shim_log("folder picker: mkdir %s failed: errno %d", full.c_str(), e);
        return fs_error(e == EEXIST ? ps5tm_tr("mr_fs_exists") : ps5tm_tr("mr_fs_mkfail", { errno_text(e) }));
    }
    shim_log("folder picker: created %s", full.c_str());
    return "{\"ok\":true,\"path\":" + jstr(full) + "}";
}

} // namespace ps5tm_ui
