#include <stdio.h>
#include <poll.h>
#include <errno.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
/* PS5 platform: on the host we print notifications and write the calls to the log */
static void calllog(const char *s) { FILE *f = fopen("/tmp/ps5tm_host_calls.log", "a"); if (f) { fprintf(f, "%ld %s\n", (long)time(NULL), s); fclose(f); } }
int sceKernelSendNotificationRequest(int d, void *r, size_t s, int f) { (void)d; (void)s; (void)f; printf("NOTIFY: %s\n", (char *)r + 45); fflush(stdout); return 0; }
int klog_printf(const char *f, ...) { (void)f; return 0; }
/* TEST imitation of the auto-power-off system service: the mode is in the file /tmp/ps5tm_test_power
 * ok | missing | nomodule | returnvalue | error | hang */
#include <unistd.h>
static time_t g_last_reset = 0;
static void power_mode(char *out, size_t n) {
    snprintf(out, n, "ok");
    FILE *f = fopen("/tmp/ps5tm_test_power", "r");
    if (f) { if (fgets(out, (int)n, f)) out[strcspn(out, "\n")] = 0; fclose(f); }
}
static int stub_get_remaining(void *buf) {
    char mode[32]; power_mode(mode, sizeof mode);
    if (!g_last_reset) g_last_reset = time(NULL);
    if (!strcmp(mode, "hang")) { sleep(20); }
    int remaining = 3600 - (int)(time(NULL) - g_last_reset); if (remaining < 0) remaining = 0;
    if (!strcmp(mode, "returnvalue")) return remaining;
    if (!strcmp(mode, "error")) return (int)0x80020001;
    memcpy(buf, &remaining, 4); return 0;
}
static int stub_reset(void) { g_last_reset = time(NULL); return 0; }
/* Platform functions are resolved by NID at run time (as on the console). The modes are in the file
 * /tmp/ps5tm_test_modules (words separated by spaces): no-appinst, no-kernel, titledir, hang-appinst */
static int modules_has(const char *word) {
    FILE *f = fopen("/tmp/ps5tm_test_modules", "r"); char b[256] = {0};
    if (!f) return 0; if (!fgets(b, sizeof b, f)) b[0] = 0; fclose(f); return strstr(b, word) != NULL;
}
static int st_open_flag(void *ev, const char *name) { char b[128]; snprintf(b, sizeof b, "OpenEventFlag(%s)", name); calllog(b); *(void **)ev = (void *)0x1234; return 0; }
static int st_notify(void) { calllog("NotifySystemSuspendStart"); return 0; }
static int st_set_flag(void *ev, int bits) { (void)ev; char b[64]; snprintf(b, sizeof b, "SetEventFlag(0x%x)", bits); calllog(b); return 0; }
static int st_close_flag(void *ev) { (void)ev; calllog("CloseEventFlag"); return 0; }
static int st_ai_init(void) { calllog("AppInstUtilInitialize"); if (modules_has("hang-appinst")) sleep(40); return 0; }
static int st_ai_term(void) { calllog("AppInstUtilTerminate"); return 0; }
static int st_ai_uninstall(const char *id) { char b[96]; snprintf(b, sizeof b, "AppUnInstall(%s)", id); calllog(b); return 0; }
static int st_ai_all(void *o) { (void)o; calllog("AppInstallAll"); return 0; }
static int st_ai_titledir(const char *id, const char *dir, void *o) { (void)o; char b[160]; snprintf(b, sizeof b, "AppInstallTitleDir(%s,%s)", id, dir); calllog(b); return 0; }

static int g_ss_loaded = 0;      /* the system service module was loaded through LoadStartModule */
static int g_ss_hidden = 0;      /* loaded, but not found by name */
static int st_load_start_module(const char *path, size_t a, const void *b, unsigned f, const void *o, int *res) {
    (void)a; (void)b; (void)f; (void)o; char l[200]; snprintf(l, sizeof l, "LoadStartModule(%s)", path); calllog(l);
    if (res) *res = 0;
    if (modules_has("loadfail")) return (int)0x80020002;
    if (modules_has("loadhang")) sleep(40);
    g_ss_loaded = 1; if (modules_has("loadnoname")) g_ss_hidden = 1;
    return 0x42;
}
int kernel_dynlib_handle(int pid, const char *base, uint32_t *h) {
    (void)pid;
    if (!strcmp(base, "libkernel_web.sprx")) { if (modules_has("no-kernel")) return -1; *h = 3; return 0; }
    if (!strcmp(base, "libSceLibcInternal.sprx") || !strcmp(base, "libSceNet.sprx")) { *h = 9; return 0; }
    if (!strcmp(base, "libSceAppInstUtil.sprx")) { if (modules_has("no-appinst")) return -1; *h = 5; return 0; }
    if (!strcmp(base, "libSceSystemService.sprx")) {
        char mode[32]; power_mode(mode, sizeof mode); if (!strcmp(mode, "nomodule")) return -1;
        int loaded = modules_has("ss-notloaded") ? (g_ss_loaded && !g_ss_hidden) : 1;
        if (!loaded) return -1; *h = 7; return 0;
    }
    return -1;
}
intptr_t kernel_dynlib_resolve(int pid, uint32_t h, const char *nid) {
    (void)pid;
    if (h == 3) {
        if (!strcmp(nid, "wzvqT4UqKX8")) return (intptr_t)st_load_start_module;
        if (!strcmp(nid, "1vDaenmJtyA")) return (intptr_t)st_open_flag;
        if (!strcmp(nid, "da6-yq0GbLg")) return (intptr_t)st_notify;
        if (!strcmp(nid, "IOnSvHzqu6A")) return (intptr_t)st_set_flag;
        if (!strcmp(nid, "s9-RaxukuzQ")) return (intptr_t)st_close_flag;
    }
    if (h == 5) {
        if (!strcmp(nid, "540lotO7oHE")) return (intptr_t)st_ai_init;
        if (!strcmp(nid, "kLLazhNh6d4")) return (intptr_t)st_ai_term;
        if (!strcmp(nid, "Sx4TTyrQccE")) return (intptr_t)st_ai_uninstall;
        if (!strcmp(nid, "+scQA5stvjs")) return (intptr_t)st_ai_all;
        if (!strcmp(nid, "Wudg3Xe3heE")) return modules_has("titledir") ? (intptr_t)st_ai_titledir : 0;
    }
    if (h == 7) {
        char mode[32]; power_mode(mode, sizeof mode); if (!strcmp(mode, "missing")) return 0;
        if (!strcmp(nid, "5gIVIzipgsw")) return (intptr_t)stub_get_remaining;
        if (!strcmp(nid, "IzQN+F5q3wg")) return (intptr_t)stub_reset;
    }
    return 0;
}

