/* PS5 + Transmission: disks and volumes: aliases of paths, kind of disk, file system.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"


#define ALIAS_PATH 600

/* Path alias: BindMount (and others) mount the disk once more at another path through nullfs (for example /data/ext1 is /mnt/ext1, so that .pkg files are installed from there).
 * We learn this from the system itself: statfs() of the deepest existing parent of the path; if the file system is nullfs, we substitute the "from" (f_mntfromname)
 * for the "to" (f_mntonname) and repeat (up to 4 times). Nothing is hardcoded: it works for any paths from any BindMount configuration.
 * Returns 1 if the path changed. On the host (Linux) there is no nullfs: for the tests the table is set by the variable PS5TM_TEST_ALIAS="/data/ext1=/mnt/ext1;/data/usb0=/mnt/usb0". */
int ps5tm_resolve_alias(const char *in, char *out, size_t size)
{
    char cur[ALIAS_PATH];
    int changed = 0, hop;
    snprintf(cur, sizeof(cur), "%s", in);
    for (hop = 0; hop < 4; hop++) {
        char next[ALIAS_PATH];
        int hit = 0;
#ifdef __linux__
        const char *table = getenv("PS5TM_TEST_ALIAS");
        while (table != NULL && *table && !hit) {
            const char *eq = strchr(table, '='), *end = strchr(table, ';');
            size_t onlen, fromlen;
            if (eq == NULL)
                break;
            if (end == NULL)
                end = table + strlen(table);
            onlen = (size_t)(eq - table);
            fromlen = (size_t)(end - eq - 1);
            if (strncmp(cur, table, onlen) == 0 && (cur[onlen] == 0 || cur[onlen] == '/')) {
                snprintf(next, sizeof(next), "%.*s%s", (int)fromlen, eq + 1, cur + onlen);
                hit = 1;
            }
            table = *end ? end + 1 : end;
        }
#else
        struct statfs sf;
        char probe[ALIAS_PATH];
        snprintf(probe, sizeof(probe), "%s", cur);
        while (statfs(probe, &sf) != 0) { /* the folder may not exist yet: take the deepest existing parent */
            char *slash = strrchr(probe, '/');
            if (slash == NULL || slash == probe)
                break;
            *slash = 0;
        }
        if (statfs(probe, &sf) == 0 && strcmp(sf.f_fstypename, "nullfs") == 0) {
            size_t onlen = strlen(sf.f_mntonname);
            if (strncmp(cur, sf.f_mntonname, onlen) == 0 && (cur[onlen] == 0 || cur[onlen] == '/')) {
                snprintf(next, sizeof(next), "%s%s", sf.f_mntfromname, cur + onlen);
                hit = 1;
            }
        }
#endif
        if (!hit || strcmp(next, cur) == 0)
            break;
        snprintf(cur, sizeof(cur), "%s", next);
        changed = 1;
    }
    /* We substitute the real path only if it leads to an external disk (/mnt/extN, /mnt/usbN). The internal disk itself is also visible twice (/data is nullfs over
     * /user/data): we do not need such a "real" path, /data/... stays as it is. */
    if (changed && !(strncmp(cur, "/mnt/", 5) == 0 && (strncmp(cur + 5, "ext", 3) == 0 || strncmp(cur + 5, "usb", 3) == 0) && cur[8] >= '0' && cur[8] <= '9' &&
                     (cur[9] == 0 || cur[9] == '/'))) {
        snprintf(out, size, "%s", in);
        return 0;
    }
    snprintf(out, size, "%s", cur);
    return changed;
}

/* Volume info by disk root (/mnt/extN, /mnt/usbN): file system type and device (statfs). Examples from the console: /mnt/ext1 bfs /dev/ssd1.user (M.2),
 * /mnt/ext0 ufs /dev/es0.crypt (USB as the console's extended storage), /mnt/usb0 exfatfs /dev/da1p1 (USB with exFAT). Host (Linux): a table for the tests
 * PS5TM_TEST_MOUNTINFO="/mnt/usb2=exfatfs=/dev/da1p1;/mnt/ext0=ufs=/dev/es0.crypt"; without it, by mount point name (usb: exfatfs, da; otherwise: bfs, ssd). 1 = found. */
int ps5tm_volume_info(const char *root, char *fstype, size_t fssize, char *device, size_t devsize)
{
    fstype[0] = 0;
    device[0] = 0;
#ifdef __linux__
    const char *table = getenv("PS5TM_TEST_MOUNTINFO");
    while (table != NULL && *table) {
        const char *e1 = strchr(table, '='), *e2 = e1 != NULL ? strchr(e1 + 1, '=') : NULL, *end = strchr(table, ';');
        if (e1 == NULL || e2 == NULL)
            break;
        if (end == NULL)
            end = table + strlen(table);
        if ((size_t)(e1 - table) == strlen(root) && strncmp(root, table, (size_t)(e1 - table)) == 0) {
            snprintf(fstype, fssize, "%.*s", (int)(e2 - e1 - 1), e1 + 1);
            snprintf(device, devsize, "%.*s", (int)(end - e2 - 1), e2 + 1);
            return 1;
        }
        table = *end ? end + 1 : end;
    }
    if (strncmp(root, "/mnt/usb", 8) == 0) {
        snprintf(fstype, fssize, "exfatfs");
        snprintf(device, devsize, "/dev/da1p1");
    } else {
        snprintf(fstype, fssize, "bfs");
        snprintf(device, devsize, "/dev/ssd1.user");
    }
    return 1;
#else
    struct statfs sf;
    if (statfs(root, &sf) != 0)
        return 0;
    snprintf(fstype, fssize, "%s", sf.f_fstypename);
    snprintf(device, devsize, "%s", sf.f_mntfromname);
    return 1;
#endif
}

/* Kind of external disk by device, not by mount point name (extended storage on USB is also mounted as /mnt/extN): 1 = M.2 SSD (/dev/ssd*),
 * 2 = USB (/dev/es*.crypt, /dev/da*). Device not determined: by mount point name. */
int ps5tm_disk_kind_of_root(const char *root)
{
    char fs[32], dev[128];
    if (ps5tm_volume_info(root, fs, sizeof(fs), dev, sizeof(dev))) {
        if (strncmp(dev, "/dev/ssd", 8) == 0)
            return 1;
        if (strncmp(dev, "/dev/es", 7) == 0 || strncmp(dev, "/dev/da", 7) == 0)
            return 2;
    }
    return strncmp(root, "/mnt/usb", 8) == 0 ? 2 : 1;
}

/* File systems without sparse files (exFAT, FAT32): torrents on them hang while the system fills the gaps with zeros; the "USB fix" is needed. */
int ps5tm_fs_needs_fix(const char *fstype)
{
    return strcmp(fstype, "exfatfs") == 0 || strcmp(fstype, "msdosfs") == 0 || strcmp(fstype, "exfat") == 0 || strcmp(fstype, "vfat") == 0;
}
