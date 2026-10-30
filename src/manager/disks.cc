// Transmission for the PS5: disks and mounts: reports, unmount plan, who holds a disk.
//
// License: GPL-3.0-or-later (like Transmission).

#include "ui_internal.h"

namespace ps5tm_ui
{

static std::string fmt_gib(double bytes)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

// All mount points the manager sees: USB and M.2 (/mnt/usbN, /mnt/extN) first, service ones last, system app sandboxes
// (/mnt/sandbox/...) are counted, not listed. This list used to be on the helper page (there it was truncated at 60 lines).
// The volume file system (a human-readable label) and whether it needs the "USB fix" (no sparse files: exFAT, FAT32).

FsInfo volume_fs(std::string const& root)
{
    char fs[32] = {};
    char dev[128] = {};
    FsInfo r;
    if (ps5tm_volume_info(root.c_str(), fs, sizeof(fs), dev, sizeof(dev)) != 0)
    {
        std::string const t{ fs };
        r.name = t == "exfatfs" || t == "exfat" ? "exFAT" : t == "msdosfs" || t == "vfat" ? "FAT32" : t == "ufs" ? "UFS" : t == "bfs" ? "BFS" : t;
        r.bad = ps5tm_fs_needs_fix(fs) != 0;
        r.device = dev;
    }
    return r;
}


// Mount table (PS5: getmntinfo; host: /proc/self/mounts).
static std::vector<MountInfo> read_mounts()
{
    std::vector<MountInfo> all;
#ifdef __linux__
    if (FILE* f = std::fopen("/proc/self/mounts", "r"))
    {
        char line[600];
        while (std::fgets(line, sizeof(line), f) != nullptr)
        {
            char from[200], on[200], type[60];
            if (std::sscanf(line, "%199s %199s %59s", from, on, type) == 3)
            {
                all.push_back({ on, type, from });
            }
        }
        std::fclose(f);
    }
#else
    struct statfs* buf = nullptr;
    int const count = getmntinfo(&buf, MNT_NOWAIT);
    for (int i = 0; i < count; ++i)
    {
        all.push_back({ buf[i].f_mntonname, buf[i].f_fstypename, buf[i].f_mntfromname });
    }
#endif
    return all;
}

// All mount points the manager sees: USB and M.2 (/mnt/usbN, /mnt/extN) first, service ones last, system app sandboxes
// (/mnt/sandbox/...) are counted, not listed.
std::string mounts_report_text()
{
    std::vector<MountInfo> const all = read_mounts();
    std::string removable;
    std::string other;
    std::size_t sandbox = 0;
    for (auto const& m : all)
    {
        std::string const row = m.on + "\t" + m.type + "\t" + m.from + "\n";
        if (m.on.compare(0, 8, "/mnt/usb") == 0 || m.on.compare(0, 8, "/mnt/ext") == 0)
        {
            removable += row;
        }
        else if (m.on.compare(0, 13, "/mnt/sandbox/") == 0)
        {
            ++sandbox;
        }
        else
        {
            other += row;
        }
    }
    std::string out = "Mount points the manager process sees (mount point, type, device). Total: " + std::to_string(all.size()) + "\n\n";
    out += "USB and M.2 (/mnt/usbN, /mnt/extN):\n" + (removable.empty() ? std::string{ "(none)\n" } : removable);
    out += "\nOther mounts:\n" + (other.empty() ? std::string{ "(none)\n" } : other);
    out += "\nSandbox mounts of system apps (/mnt/sandbox/...): " + std::to_string(sandbox) + " (not listed)\n";
    return out;
}

static bool mount_path_under(std::string const& p, std::string const& root)
{
    return p == root || (p.size() > root.size() && p.compare(0, root.size(), root) == 0 && p[root.size()] == '/');
}

// "/dev/da1p2" -> "/dev/da1" (the physical USB disk); empty if the device name is not of this form
static std::string usb_disk_base(std::string const& from)
{
    if (from.compare(0, 7, "/dev/da") != 0)
    {
        return "";
    }
    std::size_t i = 7;
    while (i < from.size() && from[i] >= '0' && from[i] <= '9')
    {
        ++i;
    }
    return i > 7 ? from.substr(0, i) : std::string{};
}

// What must be unmounted so the USB disk can be removed: first the nullfs links to it (BindMount: /data/usb0 etc.), then ALL partitions
// of the same physical disk (in your case da1p1 and da1p2). The order of the result is the unmount order.
UnmountPlan unmount_plan_parts(std::string const& root)
{
    std::vector<MountInfo> const all = read_mounts();
    std::string base;
    for (auto const& m : all)
    {
        if (m.on == root)
        {
            base = usb_disk_base(m.from);
        }
    }
    std::vector<std::string> parts;
    for (auto const& m : all)
    {
        if (m.type == "nullfs")
        {
            continue;
        }
        bool const same_disk = !base.empty() && m.from.compare(0, base.size(), base) == 0 && m.from.size() > base.size() && (m.from[base.size()] == 'p' || m.from[base.size()] == 's');
        if (m.on == root || same_disk)
        {
            parts.push_back(m.on);
        }
    }
    if (parts.empty())
    {
        parts.push_back(root);
    }
    UnmountPlan plan;
    for (auto const& m : all)
    {
        if (m.type != "nullfs" || m.on.compare(0, 13, "/mnt/sandbox/") == 0)
        {
            continue; // the disk mounts inside app sandboxes belong to the system: leave them alone
        }
        char real[600];
        ps5tm_resolve_alias(m.from.c_str(), real, sizeof(real));
        for (auto const& p : parts)
        {
            if (mount_path_under(m.from, p) || mount_path_under(real, p))
            {
                plan.aliases.push_back(m.on);
                break;
            }
        }
    }
    plan.parts = parts;
    return plan;
}

// The whole list in the order of an ordinary unmount: the links first, then the partitions from the end (a partition with a higher number is usually mounted later).
std::vector<std::string> unmount_plan(std::string const& root)
{
    UnmountPlan const up = unmount_plan_parts(root);
    std::vector<std::string> plan = up.aliases;
    for (auto it = up.parts.rbegin(); it != up.parts.rend(); ++it)
    {
        plan.push_back(*it);
    }
    return plan;
}

// Unmounting one mount point. Normal: if busy, three attempts with a one-second pause. force: sync first, then MNT_FORCE (open files of other processes
// are torn away; on the Linux host MNT_DETACH is used instead for the checks).
int unmount_one(std::string const& path, bool force)
{
    int rc = -1;
    if (force)
    {
        sync();
#ifdef __linux__
        rc = umount2(path.c_str(), MNT_DETACH);
#else
        rc = unmount(path.c_str(), MNT_FORCE);
#endif
        return rc;
    }
    for (int attempt = 0; attempt < 3; ++attempt)
    {
#ifdef __linux__
        rc = umount2(path.c_str(), 0);
#else
        rc = unmount(path.c_str(), 0);
#endif
        if (rc == 0 || errno != EBUSY)
        {
            break;
        }
        sleep(1);
    }
    return rc;
}

// Who holds the disk (process, number of open files, sample path): a JSON array for the window. kind: ours (our manager or daemon), system (Sce* system processes), app.
std::string holders_json(std::vector<std::string> const& mounts)
{
    std::vector<char const*> roots;
    for (auto const& m : mounts)
    {
        roots.push_back(m.c_str());
    }
    char text[4096];
    int const n = ps5tm_disk_holders(roots.data(), static_cast<int>(roots.size()), text, sizeof(text));
    std::string out = "[";
    if (n > 0)
    {
        bool first = true;
        std::string const all{ text };
        std::size_t pos = 0;
        while (pos < all.size())
        {
            auto end = all.find('\n', pos);
            if (end == std::string::npos)
            {
                end = all.size();
            }
            std::string const line = all.substr(pos, end - pos);
            pos = end + 1;
            std::vector<std::string> f;
            std::size_t p = 0;
            for (int i = 0; i < 4; ++i)
            {
                auto q = i == 3 ? line.size() : line.find('\t', p);
                if (q == std::string::npos)
                {
                    q = line.size();
                }
                f.push_back(line.substr(p, q - p));
                p = q + 1;
            }
            if (f[0].empty())
            {
                continue;
            }
            std::string const& name = f[1];
            bool const ours = name.compare(0, 12, "Transmission") == 0 || name.compare(0, 12, "transmission") == 0 || std::atoi(f[0].c_str()) == static_cast<int>(getpid());
            bool const system = name.compare(0, 3, "Sce") == 0 || name.compare(0, 3, "sce") == 0 || name == "shellcore" || name == "SceShellCore";
            out += std::string{ first ? "" : "," } + "{\"pid\":" + f[0] + ",\"name\":" + jstr(name) + ",\"kind\":\"" + (ours ? "ours" : system ? "system" : "app") + "\",\"count\":" + f[2] +
                   ",\"sample\":" + jstr(f[3]) + "}";
            first = false;
        }
    }
    return out + "]";
}

std::string errno_text(int e)
{
    char const* name = e == EBUSY ? "EBUSY" : e == EPERM ? "EPERM" : e == EINVAL ? "EINVAL" : e == ENOENT ? "ENOENT" : e == EIO ? "EIO" : e == EACCES ? "EACCES" : "";
    return std::string{ name } + (name[0] != 0 ? " " : "") + "(errno " + std::to_string(e) + ")";
}

std::string disks_report_text()
{
    std::string out = "Whether the drive is connected: a connected one has its own device id (dev), different from that of /mnt.\n"
                      "Press the button with the drive connected and without it and send both results.\n\n";
    struct stat parent_st;
    unsigned long long parent_dev = 0;
    bool const have_parent = stat("/mnt", &parent_st) == 0;
    if (have_parent)
    {
        parent_dev = static_cast<unsigned long long>(parent_st.st_dev);
    }
    auto line = [&](std::string const& path, bool compare)
    {
        struct stat st;
        char head[64];
        std::snprintf(head, sizeof(head), "%-11s ", path.c_str());
        out += head;
        if (stat(path.c_str(), &st) != 0)
        {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "no such folder (errno %d)\n", errno);
            out += msg;
            return;
        }
        unsigned long long const dev = static_cast<unsigned long long>(st.st_dev);
        bool const dir = S_ISDIR(st.st_mode);
        bool const mounted = dir && compare && have_parent && dev != parent_dev;
        char info[160];
        std::snprintf(info, sizeof(info), "dev=%llu  ", dev);
        std::string text = info;
        struct statvfs sv;
        if (statvfs(path.c_str(), &sv) == 0)
        {
            double const frsize = sv.f_frsize != 0 ? static_cast<double>(sv.f_frsize) : static_cast<double>(sv.f_bsize);
            text += "total " + fmt_gib(static_cast<double>(sv.f_blocks) * frsize) + ", free " + fmt_gib(static_cast<double>(sv.f_bavail) * frsize);
        }
        else
        {
            char msg[64];
            std::snprintf(msg, sizeof(msg), "could not get the free space (errno %d)", errno);
            text += msg;
        }
        if (!dir)
        {
            out += "this is not a folder  " + text + "\n";
        }
        else if (!compare)
        {
            out += "              " + text + "\n";
        }
        else
        {
            out += std::string{ mounted ? "CONNECTED      " : "NOT connected  " } + text + "\n";
        }
    };
    line("/", false);
    line("/data", false);
    line("/mnt", false);
    out += "\n";
    for (int i = 0; i < 8; ++i)
    {
        line("/mnt/ext" + std::to_string(i), true);
    }
    for (int i = 0; i < 8; ++i)
    {
        line("/mnt/usb" + std::to_string(i), true);
    }
    return out;
}

} // namespace ps5tm_ui
