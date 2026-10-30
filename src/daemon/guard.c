/* PS5 + Transmission: protection of the download folders.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"


/* ---------- download folder protection ----------
 * Called from Transmission (insertions in libtransmission/rpcimpl.cc, see patches/transmission/0002-download-folders-guard-rpc.patch): torrent-add, torrent-set-location,
 * session-set/get. The list of folders lives in config.cfg as download_folder=/path lines (the manager maintains it); here it is read and checked only.
 * Rules: torrents are added and moved only into folders from the list; folders on external disks /mnt/extN, /mnt/usbN are available only while
 * the disk is CONNECTED (on the console /mnt/usbN exists and is writable even without a disk). A connected disk is a separate file
 * system: st_dev of its root differs from st_dev of /mnt and the capacity is greater than zero. Already added torrents are not affected by these functions.
 * The functions return 0 (allowed) or -1 (denied), in msg a text for the user. */
#define GUARD_MAX 64
#define GUARD_PATH 512
#define GUARD_MSG_NO_FOLDERS "There are no download folders. Add one on the settings page of Transmission for PS5 (the Download folders section)."
#define GUARD_MSG_NOT_IN_LIST "The folder '%s' is not in the list of download folders (Transmission for PS5, the Download folders section)."
#define GUARD_MSG_BAD_PATH "Invalid download folder path."
#define GUARD_MSG_DISK_ADD "The drive %s is not connected: the torrent was not added."
#define GUARD_MSG_DISK_MOVE "The drive %s is not connected: the move is impossible."
#define GUARD_MSG_DISK_START "The drive %s is not connected: the torrent was not started. Connect the drive and start the torrent again."
#define GUARD_MSG_DISK_VERIFY "The drive %s is not connected: the check is impossible (the torrent progress would be reset otherwise). Connect the drive and try again."
/* The download-dir value in settings.json when the download folder list has no suitable folder: a path that cannot be created. A torrent without saved
 * state (if it is ever lost) will be downloaded exactly there and cannot silently end up in a foreign folder. */
#define GUARD_NO_FOLDER "/dev/null/ps5tm-no-download-folder"
#define GUARD_MSG_NO_FOLDER_TORRENT "The folder of this torrent is unknown (its saved state was lost). Set it through 'Set location' (a folder from the Download folders list)."
#define GUARD_MSG_TEMP_OFF "The temporary folder is disabled in Transmission for PS5."

/* Translation of a rejection: the manager writes guard_messages.txt (lines "English format<TAB>translation with {1}"). The value from the file does NOT go to printf: {1}
 * is replaced with the first string argument manually. No file or no line: the English text remains. */
static int guard_translate(const char *english, const char *arg, char *out, size_t outsize)
{
    FILE *f = fopen(BASE_DIR "/guard_messages.txt", "r");
    char line[1400];
    size_t elen = strlen(english);
    int found = 0;
    if (!f)
        return 0;
    while (!found && fgets(line, sizeof(line), f)) {
        if (strncmp(line, english, elen) == 0 && line[elen] == '\t') {
            char *tr = line + elen + 1;
            size_t o = 0;
            tr[strcspn(tr, "\r\n")] = 0;
            for (const char *p = tr; *p && o + 1 < outsize;) {
                if (strncmp(p, "{1}", 3) == 0) {
                    for (const char *a = arg ? arg : ""; *a && o + 1 < outsize; a++)
                        out[o++] = *a;
                    p += 3;
                } else {
                    out[o++] = *p++;
                }
            }
            out[o] = 0;
            found = 1;
        }
    }
    fclose(f);
    return found;
}

static void guard_reject(char *msg, size_t size, const char *fmt, ...)
{
    va_list ap, ap2;
    const char *arg = NULL;
    char translated[1000];
    va_start(ap, fmt);
    va_copy(ap2, ap);
    if (strstr(fmt, "%s"))
        arg = va_arg(ap2, const char *);
    vsnprintf(msg, size, fmt, ap);
    va_end(ap2);
    va_end(ap);
    shim_log("guard: rejected: %s", msg);
    if (guard_translate(fmt, arg, translated, sizeof(translated)))
        snprintf(msg, size, "%s", translated);
}

/* Normalization as in the manager: absolute path, no ".." and control characters, "//" and "." are collapsed, no trailing "/". */
int guard_normalize(const char *in, char *out, size_t size)
{
    char tmp[GUARD_PATH];
    size_t len, used = 0;
    char *p;

    if (in == NULL || size < 2)
        return -1;
    while (*in == ' ' || *in == '\t')
        in++;
    len = strlen(in);
    while (len > 0 && (in[len - 1] == ' ' || in[len - 1] == '\t'))
        len--;
    if (len == 0 || in[0] != '/' || len >= sizeof(tmp))
        return -1;
    memcpy(tmp, in, len);
    tmp[len] = 0;
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)tmp[i] < 0x20 || (unsigned char)tmp[i] == 0x7f)
            return -1;
    }
    out[0] = 0;
    p = tmp;
    while (*p) {
        char *q;
        size_t part;
        while (*p == '/')
            p++;
        if (!*p)
            break;
        q = p;
        while (*q && *q != '/')
            q++;
        part = (size_t)(q - p);
        if (part == 2 && p[0] == '.' && p[1] == '.')
            return -1;
        if (!(part == 1 && p[0] == '.')) {
            if (used + 1 + part + 1 > size)
                return -1;
            out[used++] = '/';
            memcpy(out + used, p, part);
            used += part;
            out[used] = 0;
        }
        p = q;
    }
    return used > 0 ? 0 : -1;
}

/* case-insensitive ASCII equality (as when adding a folder in the manager) */
static int guard_same(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z')
            x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z')
            y = (char)(y + 32);
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
}

/* The list from config.cfg: the same parsing rules as in the manager (comment after " #", case-insensitive key, lines up to 511 characters). */
int guard_load(char (*list)[GUARD_PATH], int max)
{
    FILE *f = fopen(CONFIG_CFG, "r");
    char raw[512];
    int n = 0, line = 0;

    if (f == NULL)
        return 0;
    while (n < max && fgets(raw, sizeof(raw), f) != NULL) {
        char *s = raw, *c, *eq, *end, *key, *value;
        char norm[GUARD_PATH];
        int dup = 0;
        line++;
        if (line == 1 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
            s += 3;
        c = strstr(s, " #");
        if (c)
            *c = 0;
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
            s++;
        end = s + strlen(s);
        while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
            *--end = 0;
        if (*s == 0 || *s == '#' || *s == ';')
            continue;
        eq = strchr(s, '=');
        if (eq == NULL)
            continue;
        *eq = 0;
        key = s;
        value = eq + 1;
        end = key + strlen(key);
        while (end > key && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = 0;
        for (char *k = key; *k; k++) {
            if (*k >= 'A' && *k <= 'Z')
                *k = (char)(*k + 32);
        }
        while (*value == ' ' || *value == '\t')
            value++;
        if (strcmp(key, "download_folder") != 0 || value[0] != '/')
            continue;
        if (guard_normalize(value, norm, sizeof(norm)) != 0)
            continue; /* a strange line written by hand is not treated as a download folder (the safe side) */
        for (int i = 0; i < n; i++) {
            if (guard_same(list[i], norm))
                dup = 1;
        }
        if (!dup) {
            snprintf(list[n], GUARD_PATH, "%s", norm);
            n++;
        }
    }
    fclose(f);
    return n;
}

int guard_find(char (*list)[GUARD_PATH], int n, const char *norm)
{
    for (int i = 0; i < n; i++) {
        if (guard_same(list[i], norm))
            return i;
    }
    return -1;
}


/* Disk root for a checked path: /mnt/extN or /mnt/usbN (N is one digit); 0 if the path is not on an external disk. The path is first resolved to the real one (see above). */
static int guard_disk_root(const char *norm0, char *root, size_t size)
{
    char norm[GUARD_PATH];
    const char *n;
    ps5tm_resolve_alias(norm0, norm, sizeof(norm));
    if (size < 10 || strncmp(norm, "/mnt/", 5) != 0)
        return 0;
    n = norm + 5;
    if (!(strncmp(n, "ext", 3) == 0 || strncmp(n, "usb", 3) == 0))
        return 0;
    if (n[3] < '0' || n[3] > '9' || (n[4] != 0 && n[4] != '/'))
        return 0;
    memcpy(root, norm, 9);
    root[9] = 0;
    return 1;
}

/* The disk is connected: the root is a folder on a SEPARATE file system (st_dev unlike /mnt) with capacity greater than zero. */
static int guard_disk_mounted(const char *root)
{
    struct stat st, parent;
    struct statvfs sv;
    if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode) || stat("/mnt", &parent) != 0)
        return 0;
    if (st.st_dev == parent.st_dev)
        return 0;
    if (statvfs(root, &sv) != 0)
        return 0;
    return sv.f_blocks > 0;
}

/* The default folder is the one the user CHOSE (session-set download-dir, "Download to"), kept in a file of ours; without a choice (or when the chosen folder is no
 * longer in the list) it is the first folder of the list. The download-dir of the running session is NOT used: after an update or a folder that was deleted from the list
 * it can hold a stale path, and adding that folder again would silently make it the default. The manager deletes the file when the chosen folder is removed from the list. */
#define DEFAULT_FOLDER_FILE "/data/Transmission/transmission/default_folder.txt"

/* 0 and the normalized path in out if the user has chosen a default folder. */
static int guard_explicit_default(char *out, size_t size)
{
    char raw[GUARD_PATH];
    FILE *f = fopen(DEFAULT_FOLDER_FILE, "r");
    if (f == NULL)
        return -1;
    if (fgets(raw, sizeof(raw), f) == NULL)
        raw[0] = 0;
    fclose(f);
    raw[strcspn(raw, "\r\n")] = 0;
    return raw[0] != 0 && guard_normalize(raw, out, size) == 0 ? 0 : -1;
}

/* Remembers the chosen default folder (normalized). only_if_missing: at daemon start, to keep a choice made before this file existed. */
void ps5tm_guard_remember_default(const char *path, int only_if_missing)
{
    char norm[GUARD_PATH];
    if (path == NULL || guard_normalize(path, norm, sizeof(norm)) != 0)
        return;
    if (only_if_missing && access(DEFAULT_FOLDER_FILE, F_OK) == 0)
        return;
    write_file_atomic(DEFAULT_FOLDER_FILE, norm, strlen(norm));
}

/* Default folder: the one the user chose if it is in the list, otherwise the first one in the list. -1 if there is no list. */
int ps5tm_guard_default_dir(const char *session_dir, char *out, size_t size)
{
    (void)session_dir; /* see above: the path held by the session is not trusted */
    char (*list)[GUARD_PATH] = malloc((size_t)GUARD_MAX * GUARD_PATH);
    char norm[GUARD_PATH];
    int n, pick = 0;
    if (list == NULL)
        return -1;
    n = guard_load(list, GUARD_MAX);
    if (n == 0) {
        if (size > 0)
            out[0] = 0; /* there are no download folders: there is no "default folder" either (the interface will show an empty field) */
        free(list);
        return 0;
    }
    if (guard_explicit_default(norm, sizeof(norm)) == 0) {
        int i = guard_find(list, n, norm);
        if (i >= 0)
            pick = i;
    }
    snprintf(out, size, "%s", list[pick]);
    free(list);
    return 0;
}

/* torrent-add: requested (may be empty) -> resolved. An empty path = the default folder. */
int ps5tm_guard_check_add(const char *requested, const char *session_dir, char *resolved, size_t rsize, char *msg, size_t msize)
{
    char (*list)[GUARD_PATH] = malloc((size_t)GUARD_MAX * GUARD_PATH);
    char norm[GUARD_PATH], root[16];
    int n, pick = 0, rc = -1;

    (void)session_dir; /* the default folder is the user's explicit choice, not the path held by the session */
    if (list == NULL) {
        guard_reject(msg, msize, "Not enough memory to check the download folder.");
        return -1;
    }
    n = guard_load(list, GUARD_MAX);
    if (n == 0) {
        guard_reject(msg, msize, GUARD_MSG_NO_FOLDERS);
        goto done;
    }
    if (requested != NULL && requested[0] != 0) {
        if (guard_normalize(requested, norm, sizeof(norm)) != 0) {
            guard_reject(msg, msize, GUARD_MSG_BAD_PATH);
            goto done;
        }
        pick = guard_find(list, n, norm);
        if (pick < 0) {
            guard_reject(msg, msize, GUARD_MSG_NOT_IN_LIST, norm);
            goto done;
        }
    } else if (guard_explicit_default(norm, sizeof(norm)) == 0) {
        int i = guard_find(list, n, norm);
        pick = i >= 0 ? i : 0;
    }
    if (guard_disk_root(list[pick], root, sizeof(root)) && !guard_disk_mounted(root)) {
        guard_reject(msg, msize, GUARD_MSG_DISK_ADD, root);
        goto done;
    }
    snprintf(resolved, rsize, "%s", list[pick]);
    rc = 0;
done:
    free(list);
    return rc;
}

/* torrent-set-location: the destination only from the list and only on a connected disk. */
int ps5tm_guard_check_location(const char *location, char *msg, size_t msize)
{
    char (*list)[GUARD_PATH] = malloc((size_t)GUARD_MAX * GUARD_PATH);
    char norm[GUARD_PATH], root[16];
    int n, pick, rc = -1;

    if (list == NULL) {
        guard_reject(msg, msize, "Not enough memory to check the download folder.");
        return -1;
    }
    n = guard_load(list, GUARD_MAX);
    if (n == 0) {
        guard_reject(msg, msize, GUARD_MSG_NO_FOLDERS);
        goto done;
    }
    if (guard_normalize(location, norm, sizeof(norm)) != 0) {
        guard_reject(msg, msize, GUARD_MSG_BAD_PATH);
        goto done;
    }
    pick = guard_find(list, n, norm);
    if (pick < 0) {
        guard_reject(msg, msize, GUARD_MSG_NOT_IN_LIST, norm);
        goto done;
    }
    if (guard_disk_root(list[pick], root, sizeof(root)) && !guard_disk_mounted(root)) {
        guard_reject(msg, msize, GUARD_MSG_DISK_MOVE, root);
        goto done;
    }
    rc = 0;
done:
    free(list);
    return rc;
}

/* torrent-set-location: where we move from. If the torrent's disk is not connected, there is nothing to move. */
int ps5tm_guard_check_source(const char *dir, char *msg, size_t msize)
{
    char norm[GUARD_PATH], root[16];
    if (dir == NULL || guard_normalize(dir, norm, sizeof(norm)) != 0)
        return 0; /* the torrent is outside our rules (for example, added before this version): we do not interfere */
    if (guard_disk_root(norm, root, sizeof(root)) && !guard_disk_mounted(root)) {
        guard_reject(msg, msize, GUARD_MSG_DISK_MOVE, root);
        return -1;
    }
    return 0;
}

/* Starting (kind 0) and checking Verify (kind 1) of a torrent: its disk must be connected. Called from the core (tr_torrent::start and
 * tr_torrentVerify, see patches/transmission/0003-guard-start-and-verify.patch), that is, for any start: a button, the queue, a download at daemon start.
 * A torrent outside our rules (path not on /mnt/extN, /mnt/usbN) is not affected. */
int ps5tm_guard_check_disk(const char *dir, int kind, char *msg, size_t msize)
{
    char norm[GUARD_PATH], root[16];
    if (dir != NULL && strncmp(dir, GUARD_NO_FOLDER, strlen(GUARD_NO_FOLDER)) == 0) {
        guard_reject(msg, msize, GUARD_MSG_NO_FOLDER_TORRENT);
        return -1;
    }
    if (dir == NULL || guard_normalize(dir, norm, sizeof(norm)) != 0)
        return 0;
    if (guard_disk_root(norm, root, sizeof(root)) && !guard_disk_mounted(root)) {
        guard_reject(msg, msize, kind == 0 ? GUARD_MSG_DISK_START : GUARD_MSG_DISK_VERIFY, root);
        return -1;
    }
    return 0;
}

/* Log: which folder the torrent went to at start. */
void ps5tm_guard_log_start(const char *name, const char *dir)
{
    shim_log("torrent start: %.120s -> %.300s", name != NULL ? name : "?", dir != NULL ? dir : "?");
}

/* 1 if the folder is on a USB disk (/mnt/usbN): there sequential download is used (see patches/transmission/0004-torrent-start-log-and-sequential-usb.patch). */
int ps5tm_guard_prefer_sequential(const char *dir)
{
    char norm[GUARD_PATH], root[16];
    if (dir == NULL || guard_normalize(dir, norm, sizeof(norm)) != 0)
        return 0;
    if (!guard_disk_root(norm, root, sizeof(root)))
        return 0;
    {
        char fs[32], dev[16];
        ps5tm_volume_info(root, fs, sizeof(fs), dev, sizeof(dev));
        return ps5tm_fs_needs_fix(fs); /* by the file system of the volume (exFAT, FAT32), not by the mount point name: on UFS (extended storage) the fix is not needed */
    }
}

/* "USB fix" (see patches/transmission/0008-per-torrent-usb-fix.patch): enabled by default for torrents on /mnt/usbN. Torrents for which it was turned off when adding
 * are listed by hash (one per line) in USBFIX_OFF_FILE. */
#define USBFIX_OFF_FILE "/data/Transmission/transmission/usbfix_off.txt"
#define USBFIX_CAP 65536
static pthread_mutex_t usbfix_mutex = PTHREAD_MUTEX_INITIALIZER;
static char usbfix_buf[USBFIX_CAP];

static int usbfix_hash_ok(const char *h)
{
    size_t n = h != NULL ? strlen(h) : 0;
    if (n != 40)
        return 0;
    for (size_t i = 0; i < n; i++) {
        char c = h[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return 0;
    }
    return 1;
}

/* reads the whole file into usbfix_buf (under usbfix_mutex) */
static void usbfix_read(void)
{
    FILE *f = fopen(USBFIX_OFF_FILE, "r");
    size_t n = 0;
    if (f != NULL) {
        n = fread(usbfix_buf, 1, USBFIX_CAP - 1, f);
        fclose(f);
    }
    usbfix_buf[n] = 0;
}

static int usbfix_has(const char *hash)
{
    const char *p = usbfix_buf;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len = e != NULL ? (size_t)(e - p) : strlen(p);
        if (len == 40 && strncmp(p, hash, 40) == 0)
            return 1;
        if (e == NULL)
            break;
        p = e + 1;
    }
    return 0;
}

int ps5tm_guard_usbfix_mode(const char *hash, const char *dir)
{
    int off;
    if (!ps5tm_guard_prefer_sequential(dir))
        return -1; /* not USB: we change nothing */
    if (!usbfix_hash_ok(hash))
        return 1;
    pthread_mutex_lock(&usbfix_mutex);
    usbfix_read();
    off = usbfix_has(hash);
    pthread_mutex_unlock(&usbfix_mutex);
    return off ? 0 : 1;
}

void ps5tm_guard_usbfix_set(const char *hash, int enabled)
{
    char *out;
    size_t n = 0;
    if (!usbfix_hash_ok(hash))
        return;
    out = malloc(USBFIX_CAP + 64);
    if (out == NULL)
        return;
    pthread_mutex_lock(&usbfix_mutex);
    usbfix_read();
    for (const char *p = usbfix_buf; *p;) { /* all lines except this hash */
        const char *e = strchr(p, '\n');
        size_t len = e != NULL ? (size_t)(e - p) : strlen(p);
        if (len == 40 && strncmp(p, hash, 40) != 0 && n + 41 < USBFIX_CAP) {
            memcpy(out + n, p, 40);
            n += 40;
            out[n++] = '\n';
        }
        if (e == NULL)
            break;
        p = e + 1;
    }
    if (!enabled) {
        memcpy(out + n, hash, 40);
        n += 40;
        out[n++] = '\n';
    }
    if (write_file_atomic(USBFIX_OFF_FILE, out, n) == 0)
        shim_log("usb-fix: %s for torrent %s", enabled ? "ON" : "OFF (chosen by the user when adding)", hash);
    pthread_mutex_unlock(&usbfix_mutex);
    free(out);
}

/* The list of "Download folders" with disk state for the folder picker tiles in the web interface (RPC method ps5_folders, patches/transmission/0009-ps5-folders-rpc-method.patch). */
int ps5tm_guard_folder_count(void)
{
    char (*list)[GUARD_PATH] = malloc((size_t)GUARD_MAX * GUARD_PATH);
    int n;
    if (list == NULL)
        return 0;
    n = guard_load(list, GUARD_MAX);
    free(list);
    return n;
}

/* File system of a folder from the list for the web client: a human-readable name (exFAT, FAT32, UFS, BFS) and whether it needs the "USB fix" (no sparse files). Empty for the internal disk. */
int ps5tm_guard_folder_fs(int index, char *fs, size_t fs_size, int *bad)
{
    char (*list)[GUARD_PATH] = malloc((size_t)GUARD_MAX * GUARD_PATH);
    char root[16], name[32], dev[16];
    int n;
    fs[0] = 0;
    *bad = 0;
    if (list == NULL)
        return -1;
    n = guard_load(list, GUARD_MAX);
    if (index < 0 || index >= n) {
        free(list);
        return -1;
    }
    if (guard_disk_root(list[index], root, sizeof(root)) && ps5tm_volume_info(root, name, sizeof(name), dev, sizeof(dev))) {
        snprintf(fs, fs_size, "%s", strcmp(name, "exfatfs") == 0 || strcmp(name, "exfat") == 0 ? "exFAT" : strcmp(name, "msdosfs") == 0 || strcmp(name, "vfat") == 0 ? "FAT32" :
                                    strcmp(name, "ufs") == 0 ? "UFS" : strcmp(name, "bfs") == 0 ? "BFS" : name);
        *bad = ps5tm_fs_needs_fix(name);
    }
    free(list);
    return 0;
}

/* kind: ssd (/mnt/extN), usb (/mnt/usbN), int (internal disk and other). For a disconnected disk total and free are 0. */
int ps5tm_guard_folder_info(int index, char *path, size_t psize, char *kind, size_t ksize, int *mounted, long long *total, long long *free_bytes)
{
    char (*list)[GUARD_PATH] = malloc((size_t)GUARD_MAX * GUARD_PATH);
    char root[16];
    const char *probe;
    struct statvfs sv;
    int n;
    if (list == NULL)
        return -1;
    n = guard_load(list, GUARD_MAX);
    if (index < 0 || index >= n) {
        free(list);
        return -1;
    }
    snprintf(path, psize, "%s", list[index]);
    *mounted = 1;
    *total = 0;
    *free_bytes = 0;
    if (guard_disk_root(list[index], root, sizeof(root))) {
        snprintf(kind, ksize, "%s", ps5tm_disk_kind_of_root(root) == 2 ? "usb" : "ssd");
        *mounted = guard_disk_mounted(root) ? 1 : 0;
        probe = root;
    } else {
        snprintf(kind, ksize, "int");
        probe = "/data";
    }
    if (*mounted) {
        memset(&sv, 0, sizeof(sv));
        if (statvfs(list[index], &sv) != 0 && statvfs(probe, &sv) != 0)
            memset(&sv, 0, sizeof(sv));
        {
            long long unit = sv.f_frsize != 0 ? (long long)sv.f_frsize : (long long)sv.f_bsize;
            *total = (long long)sv.f_blocks * unit;
            *free_bytes = (long long)sv.f_bavail * unit;
        }
    }
    free(list);
    return 0;
}

/* session-set: kind 0 = download-dir (only from the list), 1 = incomplete-dir and 2 = enabling the temporary folder (forbidden entirely). */
int ps5tm_guard_check_session_dir(const char *path, int kind, char *msg, size_t msize)
{
    char (*list)[GUARD_PATH];
    char norm[GUARD_PATH];
    int n, rc = -1;

    if (kind != 0) {
        guard_reject(msg, msize, GUARD_MSG_TEMP_OFF);
        return -1;
    }
    list = malloc((size_t)GUARD_MAX * GUARD_PATH);
    if (list == NULL) {
        guard_reject(msg, msize, "Not enough memory to check the download folder.");
        return -1;
    }
    n = guard_load(list, GUARD_MAX);
    if (n == 0) {
        guard_reject(msg, msize, GUARD_MSG_NO_FOLDERS);
    } else if (guard_normalize(path, norm, sizeof(norm)) != 0) {
        guard_reject(msg, msize, GUARD_MSG_BAD_PATH);
    } else if (guard_find(list, n, norm) < 0) {
        guard_reject(msg, msize, GUARD_MSG_NOT_IN_LIST, norm);
    } else {
        ps5tm_guard_remember_default(norm, 0); /* the user chose this folder as the default */
        rc = 0;
    }
    free(list);
    return rc;
}
