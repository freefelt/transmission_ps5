/* Transmission for PS5: icon install helper (transmission_i.elf).
 *
 * A separate short-lived process. Registers the icon app on the "Media" tab in the system (or
 * removes it) through libSceAppInstUtil, the same way Payload Manager does. All work with this library
 * happens here, not in the manager: if the library hangs, only the helper suffers, while the manager and Transmission
 * keep working.
 *
 * It reads the command from /data/Transmission/icon/request ("install" or "remove") and writes the result to
 * /data/Transmission/icon/result ("ok" or "error: ..."). The application files (param.json, icon0.png) in
 * /user/app/TRNS00001/ are prepared by the manager, the helper only registers them in the system.
 *
 * Libraries: libSceIpmi and libSceAppInstUtil, in exactly this order (that is how the working samples do it: BFpilot, websrv). libSceAppInstUtil
 * has libSceIpmi in its dependencies; if it is not in the ELF list, the system hangs during loading, before main
 * (in "Active Processes" there remains a payload.elf that cannot be closed). As in BFpilot, calls to the system library go
 * in a separate thread with a timeout: if it hangs, the helper terminates itself instead of hanging.
 *
 * License: GPL-3.0-or-later (like Transmission).
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define TITLE_ID "TRNS00001"
#define PROC_NAME "transmission_i.elf"
#define REQUEST_FILE "/data/Transmission/icon/request"
#define RESULT_FILE "/data/Transmission/icon/result"
#define LOG_FILE "/data/Transmission/logs/icon.log"

#if defined(PS5TM_HOST_TEST)
/* Host test: stubs instead of the system library that write their calls to a file. The behavior is controlled by the
 * file /tmp/ps5tm_icon_mode: "fail" (registration error), "delay" (registration takes 6 seconds), "hang". */
#include <sys/prctl.h>
static int mode_has(const char *word)
{
    char buf[64] = {0};
    FILE *f = fopen("/tmp/ps5tm_icon_mode", "r");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, word) != NULL;
}
static void call_log(const char *fmt, const char *arg)
{
    FILE *f = fopen("/tmp/ps5tm_icon_calls.log", "a");
    if (!f)
        return;
    fprintf(f, fmt, arg);
    fputc('\n', f);
    fclose(f);
}
int sceAppInstUtilInitialize(void)
{
    call_log("%s", "Initialize");
    if (mode_has("hang"))
        for (;;)
            sleep(1000);
    if (mode_has("delay"))
        sleep(6);
    return mode_has("fail") ? (int)0x80990001 : 0;
}
int sceAppInstUtilTerminate(void)
{
    call_log("%s", "Terminate");
    return 0;
}
int sceAppInstUtilAppInstallAll(void *p)
{
    (void)p;
    call_log("%s", "AppInstallAll");
    return 0;
}
int sceAppInstUtilAppUnInstall(const char *id)
{
    call_log("AppUnInstall(%s)", id);
    return 0;
}
static int stub_install_title_dir(const char *id, const char *dir, void *opt)
{
    (void)opt;
    char text[200];
    snprintf(text, sizeof(text), "%s,%s", id, dir);
    call_log("AppInstallTitleDir(%s)", text);
    return 0;
}
static void *resolve_install_title_dir(void)
{
    return (void *)stub_install_title_dir;
}
static void set_name(void)
{
    prctl(PR_SET_NAME, PROC_NAME, 0, 0, 0);
}
#else
/* As in Payload Manager: the library functions (it is in the NEEDED list of this ELF) and the lookup of sceAppInstUtilAppInstallTitleDir
 * by NID (it is not in the SDK headers). */
extern int kernel_dynlib_handle(int pid, const char *basename, uint32_t *handle);
extern intptr_t kernel_dynlib_resolve(int pid, uint32_t handle, const char *nid);
int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppInstallAll(void *);
int sceAppInstUtilAppUnInstall(const char *);

static void *resolve_install_title_dir(void)
{
    uint32_t handle;
    if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle) == 0)
        return (void *)kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE"); /* sceAppInstUtilAppInstallTitleDir */
    return NULL;
}
static void set_name(void)
{
    syscall(SYS_thr_set_name, -1, PROC_NAME); /* Payload Manager shows this name in "Active Processes" */
}
#endif

static void log_line(const char *fmt, ...)
{
    FILE *f = fopen(LOG_FILE, "a");
    if (!f)
        return;
    time_t now = time(NULL);
    fprintf(f, "[%ld] ", (long)now);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void write_result(const char *text)
{
    const char *tmp = RESULT_FILE ".tmp";
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    fprintf(f, "%s\n", text);
    if (fclose(f) == 0)
        rename(tmp, RESULT_FILE);
}

static int install_app(const char *title_id, const char *dir)
{
    int (*install_title_dir)(const char *, const char *, void *) = resolve_install_title_dir();
    if (install_title_dir)
        return install_title_dir(title_id, dir, 0);
    return sceAppInstUtilAppInstallAll(0);
}

/* Work with the system library in a separate thread: the main thread waits for an answer no longer than WORK_TIMEOUT seconds */
#define WORK_TIMEOUT 20

struct job {
    char cmd[32];
    int err;
    char text[96];
    volatile const char *step;
    volatile int done;
};

static void *worker(void *arg)
{
    struct job *j = arg;
    j->err = 0;
    if (strncmp(j->cmd, "install", 7) == 0) {
        j->step = "sceAppInstUtilInitialize";
        if ((j->err = sceAppInstUtilInitialize()) != 0) {
            snprintf(j->text, sizeof(j->text), "error: sceAppInstUtilInitialize: 0x%08X", (unsigned)j->err);
        } else {
            j->step = "app registration";
            j->err = install_app(TITLE_ID, "/user/app/");
            sceAppInstUtilTerminate();
            if (j->err != 0)
                snprintf(j->text, sizeof(j->text), "error: app registration: 0x%08X", (unsigned)j->err);
        }
    } else if (strncmp(j->cmd, "remove", 6) == 0) {
        j->step = "sceAppInstUtilInitialize";
        if ((j->err = sceAppInstUtilInitialize()) != 0) {
            snprintf(j->text, sizeof(j->text), "error: sceAppInstUtilInitialize: 0x%08X", (unsigned)j->err);
        } else {
            j->step = "app removal";
            j->err = sceAppInstUtilAppUnInstall(TITLE_ID);
            sceAppInstUtilTerminate();
            if (j->err != 0)
                snprintf(j->text, sizeof(j->text), "error: app removal: 0x%08X", (unsigned)j->err);
        }
    } else {
        j->err = -1;
        snprintf(j->text, sizeof(j->text), "error: unknown command");
    }
    j->done = 1;
    return NULL;
}

int main(void)
{
#if defined(PS5TM_HOST_TEST)
    if (mode_has("prehang")) /* simulation of a hang during loading, before main (the name and the log do not appear) */
        for (;;)
            sleep(1000);
#endif
    set_name();
    log_line("started");

    static struct job job;
    FILE *f = fopen(REQUEST_FILE, "r");
    if (!f || !fgets(job.cmd, sizeof(job.cmd), f)) {
        if (f)
            fclose(f);
        log_line("no request (errno %d)", errno);
        write_result("error: no request");
        return 1;
    }
    fclose(f);
    unlink(REQUEST_FILE);
    log_line("request: %s", job.cmd);

    pthread_t thread;
    if (pthread_create(&thread, NULL, worker, &job) != 0) {
        log_line("pthread_create failed (errno %d)", errno);
        write_result("error: could not create a thread");
        return 1;
    }
    pthread_detach(thread);
    for (int i = 0; i < WORK_TIMEOUT * 10 && !job.done; i++)
        usleep(100000);

    if (!job.done) {
        snprintf(job.text, sizeof(job.text), "error: libSceAppInstUtil did not answer within %d s (step: %s)", WORK_TIMEOUT, (const char *)job.step);
        log_line("%s", job.text);
        write_result(job.text);
        _exit(3); /* the thread hung in the system library: we terminate the whole process so that it does not stay hanging */
    }
    if (job.err == 0) {
        log_line("done: ok");
        write_result("ok");
    } else {
        log_line("%s", job.text);
        write_result(job.text);
    }
    return job.err == 0 ? 0 : 1;
}
