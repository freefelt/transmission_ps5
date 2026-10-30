/* PS5 + Transmission: the manager role: main.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"


/* How many seconds the old instance gets to shut down on SIGTERM (the daemon saves
 * its state) before it is killed with SIGKILL. */
#define STOP_GRACE_SECONDS 5

/* Stops an already running instance. Everything that happens is written to
 * shim.log: on the console this part is otherwise invisible. */
static void stop_other_instances(void)
{
    for (int round = 0; round < 4; round++) {
        pid_t other = find_other_instance();
        if (other <= 0)
            return;

        shim_log("found a running instance (pid %d): SIGTERM", (int)other);
        NOTIFY_STARTUP("Transmission is already running: restarting it (pid %d)", (int)other);
        if (kill(other, SIGTERM) != 0)
            shim_log("kill(SIGTERM) failed: errno %d", errno);

        for (int i = 0; i < STOP_GRACE_SECONDS && find_other_instance() == other; i++)
            sleep(1);

        if (find_other_instance() == other) {
            shim_log("pid %d did not exit within %d s: SIGKILL", (int)other, STOP_GRACE_SECONDS);
            if (kill(other, SIGKILL) != 0)
                shim_log("kill(SIGKILL) failed: errno %d", errno);
            for (int i = 0; i < 5 && find_other_instance() == other; i++)
                sleep(1);
            if (find_other_instance() == other) {
                shim_log("pid %d is still there after SIGKILL", (int)other);
                notify_system("Transmission: cannot stop the old instance (pid %d). Stop it in Payload Manager > Active Processes.",
                       (int)other);
                return;
            }
        }
        shim_log("pid %d is gone", (int)other);
    }
}

int main(void)
{
    set_process_name(); /* before anything else, in the process main thread */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = on_sigcont;
        sa.sa_flags = SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGCONT, &sa, NULL);
    }

    if (ensure_dir(BASE_DIR) != 0) {
        char msg[160];
        snprintf(msg, sizeof(msg), "Transmission: %s is not writable (errno %d)", BASE_DIR, errno);
        unsigned char request[3120];
        memset(request, 0, sizeof(request));
        snprintf((char *)request + 45, sizeof(request) - 45, "%s", msg);
        sceKernelSendNotificationRequest(0, request, sizeof(request), 0);
        return 1;
    }

    if (ensure_dir(LOGS_DIR) != 0 || ensure_dir(DAEMON_DIR) != 0)
        notify_system("Transmission: cannot create %s or %s (errno %d)", LOGS_DIR, DAEMON_DIR, errno);
    shim_fd = open(SHIM_LOG, O_WRONLY | O_CREAT | O_APPEND, 0644);
    shim_log("==== Transmission manager " SHIM_VERSION " (built " __DATE__ " " __TIME__ ") pid=%d ====", (int)getpid());
    install_diagnostics();

    /* settings: created on the first run (read before the first notifications) */
    ps5tm_config_init(CONFIG_CFG);

    /* A repeated launch replaces only the manager (including the previous all-in-one version); the daemon stays. */
    stop_other_instances();
    detect_ip();

    int lock = open(BASE_DIR "/instance.lock", O_WRONLY | O_CREAT, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) != 0) {
        shim_log("instance lock is held by a process I cannot identify");
        notify_system("Transmission seems to be running already. Stop it in Payload Manager > Active Processes and run again.");
        return 0;
    }

    int blob = prepare_daemon_file();
    if (blob < 0) {
        shim_log("cannot write %s (errno %d)", DAEMON_PATH, errno);
        notify_system("Transmission: cannot write %s (errno %d)", DAEMON_PATH, errno);
    } else {
        shim_log("daemon file: %s (%zu bytes)", blob ? "written" : "up to date", (size_t)(ps5tm_daemon_elf_end - ps5tm_daemon_elf));
        release_embedded_daemon();
    }

    /* Icon installation helper: a small ELF that the manager launches on demand (see ps5tm_icon_helper.c) */
    if (ensure_dir(ICON_DIR) != 0 ||
        (!file_has_content(ICON_HELPER_PATH, ps5tm_icon_elf, ps5tm_icon_helper_size()) &&
         write_file_atomic(ICON_HELPER_PATH, ps5tm_icon_elf, ps5tm_icon_helper_size()) != 0))
        shim_log("warning: cannot prepare the icon helper %s (errno %d): the icon cannot be installed", ICON_HELPER_PATH, errno);

    /* Monitor (notifications and daemon supervision) and settings server */
    pthread_attr_t attr;
    pthread_t monitor;
    pthread_t ui;
    if (pthread_attr_init(&attr) != 0 || pthread_attr_setstacksize(&attr, WORKER_STACK) != 0) {
        notify_system("Transmission: cannot start the manager threads");
        return 1;
    }
    if (__real_pthread_create(&monitor, &attr, ps5tm_monitor_thread, NULL) != 0)
        shim_log("warning: cannot start the monitor (no daemon supervision!)");
    if (__real_pthread_create(&ui, &attr, ps5tm_ui_thread, NULL) != 0)
        shim_log("warning: cannot start the settings server");
    pthread_attr_destroy(&attr);

    for (;;)
        sleep(3600);
}
