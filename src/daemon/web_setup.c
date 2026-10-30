/* PS5 + Transmission: the files of the web interface (theme, icons, language, config).
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"
#include "cacert.h"
#include "webfiles.h"


/* The web interface files of Transmission live inside the ELF and are unpacked into WEB_DIR
 * (the daemon looks for them using the TRANSMISSION_WEB_HOME variable). Files that already match are
 * not overwritten. Returns the number of files that could not be written. */
int ensure_web_files(void)
{
    int failed = 0;
    if (ensure_dir(WEB_DIR) != 0)
        return (int)web_files_count;

    for (unsigned int i = 0; i < web_files_count; i++) {
        char path[256];
        int n = snprintf(path, sizeof(path), "%s/%s", WEB_DIR, web_files[i].path);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            failed++;
            continue;
        }
        /* subdirectories (images/) */
        for (char *p = path + sizeof(WEB_DIR); *p; p++) {
            if (*p == '/') {
                *p = '\0';
                ensure_dir(path);
                *p = '/';
            }
        }
        if (strcmp(web_files[i].path, "ps5-lang.js") == 0 && access(DAEMON_DIR "/web_lang.js", F_OK) == 0)
            continue; /* the translation dictionary is written by the manager (web_lang.js): we do not put an empty stub over it */
        if (file_has_content(path, web_files[i].data, web_files[i].size))
            continue;
        if (write_file_atomic(path, web_files[i].data, web_files[i].size) != 0) {
            shim_log("warning: cannot write %s (errno %d)", path, errno);
            failed++;
        }
    }
    return failed;
}

/* Web interface theme: ps5-theme.css is a copy of ps5-theme-dark.css or ps5-theme-light.css . The manager writes which one to take into transmission/web_theme; no file or garbage: dark. */
void apply_web_theme(void)
{
    char name[16] = "dark";
    FILE *f = fopen(DAEMON_DIR "/web_theme", "r");
    if (f) {
        char line[16] = {0};
        if (fgets(line, sizeof(line), f)) {
            size_t n = strlen(line);
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
                line[--n] = 0;
            if (strcmp(line, "light") == 0)
                snprintf(name, sizeof(name), "%s", line);
        }
        fclose(f);
    }
    const unsigned char *data = (const unsigned char *)"";
    size_t size = 0;
    {
        char want[40];
        snprintf(want, sizeof(want), "ps5-theme-%s.css", name);
        for (unsigned int i = 0; i < web_files_count; i++) {
            if (strcmp(web_files[i].path, want) == 0) {
                data = web_files[i].data;
                size = web_files[i].size;
                break;
            }
        }
    }
    char path[256];
    snprintf(path, sizeof(path), "%s/ps5-theme.css", WEB_DIR);
    if (!file_has_content(path, data, size) && write_file_atomic(path, data, size) != 0)
        shim_log("warning: cannot write %s (errno %d)", path, errno);
}

/* Web interface translation dictionary: web/ps5-lang.js is a copy of transmission/web_lang.js, which the manager writes for the selected language. Without this, on every
 * daemon start the page would get an empty stub and stay English until the language is switched. */
void apply_web_lang(void)
{
    FILE *f = fopen(DAEMON_DIR "/web_lang.js", "rb");
    char path[256];
    unsigned char *buf;
    size_t n;
    if (f == NULL)
        return;
    buf = malloc(1048576);
    if (buf == NULL) {
        fclose(f);
        return;
    }
    n = fread(buf, 1, 1048576, f);
    fclose(f);
    snprintf(path, sizeof(path), "%s/ps5-lang.js", WEB_DIR);
    if (n > 0 && !file_has_content(path, buf, n) && write_file_atomic(path, buf, n) != 0)
        shim_log("warning: cannot write %s (errno %d)", path, errno);
    free(buf);
}

/* Page icon set: ps5-icons.css is a copy of ps5-icons-<set>.css. The manager writes which one into transmission/web_icons;
 * no file or garbage: color. */
void apply_web_icons(void)
{
    char name[16] = "color";
    FILE *f = fopen(DAEMON_DIR "/web_icons", "r");
    if (f) {
        char line[16] = {0};
        if (fgets(line, sizeof(line), f)) {
            size_t n = strlen(line);
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
                line[--n] = 0;
            if (strcmp(line, "mono") == 0)
                snprintf(name, sizeof(name), "%s", line);
        }
        fclose(f);
    }
    const unsigned char *data = (const unsigned char *)"";
    size_t size = 0;
    char want[40];
    snprintf(want, sizeof(want), "ps5-icons-%s.css", name);
    for (unsigned int i = 0; i < web_files_count; i++) {
        if (strcmp(web_files[i].path, want) == 0) {
            data = web_files[i].data;
            size = web_files[i].size;
            break;
        }
    }
    char path[256];
    snprintf(path, sizeof(path), "%s/ps5-icons.css", WEB_DIR);
    if (!file_has_content(path, data, size) && write_file_atomic(path, data, size) != 0)
        shim_log("warning: cannot write %s (errno %d)", path, errno);
}

/* Settings of the Transmission page for ps5-panel.js: ps5-config.js (window.PS5_CONFIG). The manager writes the disk bar mode
 * into transmission/web_diskbar: off (do not show), all (disks of all torrents), active (only running ones); the old "0" = off, "1" = all;
 * no file: all. */
void apply_web_config(void)
{
    int mode = 1; /* 0 off, 1 all, 2 active */
    FILE *f = fopen(DAEMON_DIR "/web_diskbar", "r");
    if (f) {
        char line[16] = {0};
        if (fgets(line, sizeof(line), f)) {
            if (line[0] == '0' || strncmp(line, "off", 3) == 0)
                mode = 0;
            else if (strncmp(line, "active", 6) == 0)
                mode = 2;
        }
        fclose(f);
    }
    int open = 0; /* 0 off, 1 bfpilot, 2 webfm (the file transmission/web_openfolder) */
    f = fopen(DAEMON_DIR "/web_openfolder", "r");
    if (f) {
        char line[16] = {0};
        if (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "bfpilot", 7) == 0)
                open = 1;
            else if (strncmp(line, "webfm", 5) == 0)
                open = 2;
        }
        fclose(f);
    }
    char text[160];
    snprintf(text, sizeof(text), "window.PS5_CONFIG={diskbar:%s,diskbarMode:\"%s\",openFolder:\"%s\"};\n", mode == 0 ? "false" : "true",
             mode == 0 ? "off" : mode == 2 ? "active" : "all", open == 1 ? "bfpilot" : open == 2 ? "webfm" : "off");
    char path[256];
    snprintf(path, sizeof(path), "%s/ps5-config.js", WEB_DIR);
    if (!file_has_content(path, text, strlen(text)) && write_file_atomic(path, text, strlen(text)) != 0)
        shim_log("warning: cannot write %s (errno %d)", path, errno);
}

/* The CA certificates of the trackers: written once to CA_FILE (the data is generated into cacert.h). */
void ensure_ca_file(void)
{
    if (!file_has_content(CA_FILE, cacert_pem, cacert_pem_len) &&
        write_file_atomic(CA_FILE, cacert_pem, cacert_pem_len) != 0)
        shim_log("warning: cannot write %s (HTTPS trackers may fail)", CA_FILE);
}

/* How many files the web interface has (the data is generated into webfiles.h). */
unsigned web_file_count(void)
{
    return web_files_count;
}
