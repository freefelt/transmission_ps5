/* PS5 + Transmission: the starting settings.json and the forced settings.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"


/* The download folder on first start: /mnt/ext1/torrents if an SSD is installed
 * (/mnt/ext1 exists and is a separate volume, not an empty folder on the root FS),
 * otherwise /data/torrents. USB drives are not used. The folder can be changed
 * later in the web interface. */
static void pick_download_dir(char *out, size_t size)
{
    struct stat st, parent;
    if (stat(SSD_ROOT, &st) == 0 && S_ISDIR(st.st_mode) && stat("/mnt", &parent) == 0 &&
        st.st_dev != parent.st_dev && ensure_dir(SSD_DOWNLOAD_DIR) == 0) {
        snprintf(out, size, "%s", SSD_DOWNLOAD_DIR);
        return;
    }
    ensure_dir(INTERNAL_DOWNLOAD_DIR);
    snprintf(out, size, "%s", INTERNAL_DOWNLOAD_DIR);
}

/* Starting settings. There is no password, access to the web interface is open to
 * the local network (private ranges). The file is created once and from then on
 * belongs to Transmission: any changes through the web interface are saved. */
int write_default_settings(void)
{
    if (access(SETTINGS_FILE, F_OK) == 0)
        return 0;

    char download_dir[128];
    pick_download_dir(download_dir, sizeof(download_dir));

    char json[2048];
    int n = snprintf(
        json, sizeof(json),
        "{\n"
        "    \"download-dir\": \"%s\",\n"
        "    \"incomplete-dir-enabled\": false,\n"
        "    \"rpc-enabled\": true,\n"
        "    \"rpc-port\": %d,\n"
        "    \"rpc-bind-address\": \"0.0.0.0\",\n"
        "    \"rpc-authentication-required\": false,\n"
        "    \"rpc-host-whitelist-enabled\": false,\n"
        "    \"rpc-whitelist-enabled\": true,\n"
        "    \"rpc-whitelist\": \"127.0.0.1,::1,10.*.*.*,192.168.*.*,"
        "172.16.*.*,172.17.*.*,172.18.*.*,172.19.*.*,172.20.*.*,172.21.*.*,"
        "172.22.*.*,172.23.*.*,172.24.*.*,172.25.*.*,172.26.*.*,172.27.*.*,"
        "172.28.*.*,172.29.*.*,172.30.*.*,172.31.*.*\",\n"
        "    \"watch-dir-enabled\": false,\n"
        "    \"port-forwarding-enabled\": false,\n"
        "    \"lpd-enabled\": false,\n"
        "    \"peer-port\": 51413,\n"
        "    \"peer-port-random-on-start\": false,\n"
        "    \"preallocation\": 0\n"
        "}\n",
        download_dir, RPC_PORT);
    if (n < 0 || (size_t)n >= sizeof(json))
        return -1;
    shim_log("creating settings.json (download-dir=%s)", download_dir);
    return write_file_atomic(SETTINGS_FILE, json, (size_t)n);
}

/* Editing the value of a key in the settings.json text: replaces the value (number, string or true/false) with new_value (JSON text). If the key is missing and
 * insert_if_missing, inserts it after the first "{". Returns 1 if the buffer was changed. */
static int settings_force_value(char *buf, size_t cap, const char *key, const char *new_value, int insert_if_missing)
{
    char pat[96];
    char *k;
    size_t nl = strlen(new_value);

    snprintf(pat, sizeof(pat), "\"%s\"", key);
    k = strstr(buf, pat);
    if (k != NULL) {
        char *p = k + strlen(pat), *end;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')
            p++;
        end = p;
        if (*p == '"') {
            end = p + 1;
            while (*end && *end != '"')
                end += (*end == '\\' && end[1]) ? 2 : 1;
            if (*end == '"')
                end++;
        } else {
            while (*end && *end != ',' && *end != '}' && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n')
                end++;
        }
        if ((size_t)(end - p) == nl && strncmp(p, new_value, nl) == 0)
            return 0;
        if (strlen(buf) - (size_t)(end - p) + nl + 1 > cap)
            return 0;
        memmove(p + nl, end, strlen(end) + 1);
        memcpy(p, new_value, nl);
        return 1;
    } else if (insert_if_missing) {
        char ins[160];
        char *brace = strchr(buf, '{'), *q;
        int empty;
        size_t il;
        if (brace == NULL)
            return 0;
        q = brace + 1;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')
            q++;
        empty = (*q == '}');
        il = (size_t)snprintf(ins, sizeof(ins), "\n    \"%s\": %s%s", key, new_value, empty ? "" : ",");
        if (strlen(buf) + il + 1 > cap)
            return 0;
        memmove(brace + 1 + il, brace + 1, strlen(brace + 1) + 1);
        memcpy(brace + 1, ins, il);
        return 1;
    }
    return 0;
}

/* String value of a key from the settings.json text (with parsing of \\ and \"); 0 if the key is missing or it is not a string. */
static int settings_get_string(const char *buf, const char *key, char *out, size_t size)
{
    char pat[96];
    const char *k, *p;
    size_t n = 0;

    snprintf(pat, sizeof(pat), "\"%s\"", key);
    k = strstr(buf, pat);
    if (k == NULL)
        return 0;
    p = k + strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')
        p++;
    if (*p != '"')
        return 0;
    for (p++; *p && *p != '"' && n + 1 < size; p++) {
        if (*p == '\\' && p[1])
            p++;
        out[n++] = *p;
    }
    out[n] = 0;
    return 1;
}

/* Quoted string for JSON: \ and " are escaped. */
static void json_quote(const char *in, char *out, size_t size)
{
    size_t n = 0;
    if (size < 3) {
        if (size > 0)
            out[0] = 0;
        return;
    }
    out[n++] = '"';
    for (; *in && n + 3 < size; in++) {
        if (*in == '"' || *in == '\\')
            out[n++] = '\\';
        out[n++] = *in;
    }
    out[n++] = '"';
    out[n] = 0;
}

/* Transmission settings that we keep forced (checked on every daemon start, so old installations are fixed too):
 *  - "Use temporary folder" (incomplete-dir-enabled) is off: the temporary folder is forbidden by the download folder protection;
 *  - preallocation = 0 (no preallocation). By default Transmission, on the first write to a file, stretches it to the full
 *    size (ftruncate). On file systems without sparse files (exFAT and FAT32 on USB disks) the system then physically writes
 *    zeros for the whole file size: minutes during which the single Transmission thread (the web interface runs in it too) does not respond and the process cannot be killed.
 *    Without preallocation the file grows as it downloads;
 *  - download-dir (Transmission's own default folder, where a torrent without saved state would end up) is either in the "Download folders" list
 *    (for example, chosen as the default folder) or there is the GUARD_NO_FOLDER stub. The path of the torrent itself is never changed. */
void guard_enforce_settings(void)
{
    char buf[65536];
    FILE *f = fopen(SETTINGS_FILE, "rb");
    size_t n;
    int temp_fixed, prealloc_fixed, dir_fixed = 0;
    char cur_dir[GUARD_PATH] = "";

    if (f == NULL)
        return;
    n = fread(buf, 1, sizeof(buf) - 8, f);
    fclose(f);
    if (n >= sizeof(buf) - 8)
        return;
    buf[n] = 0;
    temp_fixed = settings_force_value(buf, sizeof(buf), "incomplete-dir-enabled", "false", 0);
    prealloc_fixed = settings_force_value(buf, sizeof(buf), "preallocation", "0", 1);
    settings_get_string(buf, "download-dir", cur_dir, sizeof(cur_dir));
    {
        /* The path of the torrent is never substituted: it stays the one it had when adding. Only "Transmission's own default folder" changes
         * (for a torrent without saved state): if it is not in the "Download folders" list (or the list is empty), there is an unused stub. */
        char (*list)[GUARD_PATH] = malloc((size_t)GUARD_MAX * GUARD_PATH);
        char norm[GUARD_PATH];
        int n = 0, in_list = 0;
        if (list != NULL) {
            n = guard_load(list, GUARD_MAX);
            in_list = n > 0 && guard_normalize(cur_dir, norm, sizeof(norm)) == 0 && guard_find(list, n, norm) >= 0;
            free(list);
            if (in_list)
                ps5tm_guard_remember_default(norm, 1); /* a default folder chosen before the choice was kept in a file of its own: keep it */
        }
        if (!in_list && strcmp(cur_dir, GUARD_NO_FOLDER) != 0) {
            char quoted[GUARD_PATH * 2 + 4];
            json_quote(GUARD_NO_FOLDER, quoted, sizeof(quoted));
            dir_fixed = settings_force_value(buf, sizeof(buf), "download-dir", quoted, 1);
        }
    }
    if (!temp_fixed && !prealloc_fixed && !dir_fixed)
        return;
    if (write_file_atomic(SETTINGS_FILE, buf, strlen(buf)) == 0) {
        if (temp_fixed)
            shim_log("guard: the temporary folder was ON in settings.json: switched OFF");
        if (dir_fixed)
            shim_log("settings: download-dir '%s' is not in the list of download folders: replaced by the unusable placeholder (no torrent can silently land in a wrong folder)", cur_dir);
        if (prealloc_fixed)
            shim_log("settings: preallocation switched OFF in settings.json (files grow as data arrives; no long zero-fill on exFAT/FAT disks)");
    }
}
