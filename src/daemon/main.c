/* PS5 + Transmission: the daemon role: main and the thread of Transmission.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"

static int daemon_argc;
static char **daemon_argv;
static int daemon_result;

static void *daemon_thread(void *unused)
{
    (void)unused;
    daemon_result = tr_daemon_main(daemon_argc, daemon_argv);
    shim_log("daemon thread: tr_daemon_main returned %d", daemon_result);
    ps5tm_daemon_finished = 1;
    return NULL;
}

/* ======================= role: daemon ======================= */

/* Web interface port from daemon.port (1024-65535); no file or garbage: the default port */
static int read_daemon_port(void)
{
    int port = RPC_PORT;
    FILE *f = fopen(DAEMON_DIR "/daemon.port", "r");
    if (f) {
        char line[32] = {0};
        if (fgets(line, sizeof(line), f)) {
            long value = strtol(line, NULL, 10);
            if (value >= 1024 && value <= 65535)
                port = (int)value;
        }
        fclose(f);
    }
    return port;
}

int main(void)
{
    set_process_name(); /* before everything else, in the main thread of the process */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);

    if (ensure_dir(BASE_DIR) != 0 || ensure_dir(LOGS_DIR) != 0 || ensure_dir(DAEMON_DIR) != 0 || ensure_dir(CONFIG_DIR) != 0 ||
        ensure_dir(TMP_DIR) != 0) {
        notify("Transmission daemon: cannot create %s (errno %d)", BASE_DIR, errno);
        return 1;
    }

    shim_fd = open(SHIM_LOG, O_WRONLY | O_CREAT | O_APPEND, 0644);
    shim_log("==== Transmission daemon " SHIM_VERSION " (built " __DATE__ " " __TIME__ ") pid=%d ====", (int)getpid());
    install_diagnostics();
    event_set_log_callback(ps5tm_event_log);

    /* There must be only one daemon; what to start and what to stop is decided by the manager. */
    pid_t other = find_other_instance();
    if (other > 0) {
        shim_log("another daemon is already running (pid %d): exiting", (int)other);
        return 0;
    }

    ensure_ca_file();

    int web_failed = ensure_web_files();
    apply_web_theme();
    apply_web_icons();
    apply_web_lang();
    apply_web_config();
    if (web_failed)
        shim_log("warning: %d of %u web UI file(s) could not be written to %s", web_failed, web_file_count(), WEB_DIR);

    if (write_default_settings() != 0)
        shim_log("warning: cannot write default settings.json (errno %d)", errno);
    guard_enforce_settings();

    /* stdout/stderr of the daemon and our messages: into a file so that nothing is lost */
    int console = open(CONSOLE_LOG, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (console >= 0) {
        dup2(console, 1);
        dup2(console, 2);
        if (console > 2)
            close(console);
    }

    setenv("HOME", DAEMON_DIR, 1);
    setenv("TMPDIR", TMP_DIR, 1);
    setenv("CURL_CA_BUNDLE", CA_FILE, 1);
    setenv("TRANSMISSION_WEB_HOME", WEB_DIR, 1);

    /* The web interface port is set by the manager with the daemon.port file (the loader passes no arguments) */
    static char port_arg[16];
    snprintf(port_arg, sizeof(port_arg), "%d", read_daemon_port());
    shim_log("RPC port: %s", port_arg);
    static char *args[] = {
        "transmission-daemon", "-f", "-T",
        "-g", CONFIG_DIR,
        "-e", LOG_FILE,
        "--log-level=info",
        "-p", port_arg,
        NULL,
    };
    daemon_argc = (int)(sizeof(args) / sizeof(args[0])) - 1;
    daemon_argv = args;

#if defined(__linux__)
    /* Host only (tests): on Linux the daemon accepts SIGTERM/SIGHUP through signalfd and blocks them only in its own
     * thread; a signal delivered to another thread would kill the process at once. On PS5 (FreeBSD) there is no signalfd, there the
     * libevent handler is used, and this is not needed. */
    {
        sigset_t block;
        sigemptyset(&block);
        sigaddset(&block, SIGTERM);
        sigaddset(&block, SIGHUP);
        pthread_sigmask(SIG_BLOCK, &block, NULL);
    }
#endif

    iow_start();
    pthread_attr_t attr;
    pthread_t thread;
    if (pthread_attr_init(&attr) != 0 ||
        pthread_attr_setstacksize(&attr, DAEMON_STACK) != 0 ||
        __real_pthread_create(&thread, &attr, daemon_thread, NULL) != 0) {
        shim_log("cannot start the daemon thread");
        return 1;
    }
    pthread_attr_destroy(&attr);

    pthread_join(thread, NULL);
    shim_log("daemon exits with code %d", daemon_result);
    return daemon_result;
}
