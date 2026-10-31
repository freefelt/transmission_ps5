/* PS5 + Transmission: runtime state of the manager: start time, IP address, resume generation.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"

static char g_ip[64] = "<PS5 IP>";

/* Start/stop/restart notifications: enabled by the notify_startup setting in config.cfg.
 * Startup error messages (port busy, failed to stop the old instance, etc.) go
 * through notify_system() and always arrive. */
void notify_startup(const char *fmt, ...)
{
    char text[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (ps5tm_cfg_notify_startup())
        notify_system("%s", text);
    else
        ps5tm_journal_add(text, 4); /* a system entry that is disabled in settings: do not send to console, record in journal */
}

/* Waking from rest mode. Like Payload Manager, we treat SIGCONT as the wake-up signal: on
 * entering sleep the system stops processes, on exit it resumes them. The handler only
 * sets a flag; the monitor does everything else. SIGCONT also arrives in other cases (for
 * example, when the browser opens), so the reaction must be cheap and harmless. */
static volatile sig_atomic_t resume_gen;

void on_sigcont(int sig)
{
    (void)sig;
    resume_gen = resume_gen + 1;
}

/* Wake-up counter (SIGCONT). Each consumer (monitor, settings server) remembers its
 * last value and compares it with the current one: everyone sees the same signal. */
unsigned ps5tm_resume_generation(void)
{
    return (unsigned)resume_gen;
}

/* Wrapper version and uptime (for the settings page) */
const char *ps5tm_sdk_version(void)
{
    return PS5TM_SDK_VERSION;
}

const char *ps5tm_build_time(void)
{
    return __DATE__ " " __TIME__;
}

const char *ps5tm_version(void)
{
    return SHIM_VERSION;
}

long ps5tm_uptime_seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)(now.tv_sec - t0.tv_sec);
}

/* Local IP address of the console (for monitor notification texts) */
const char *ps5tm_ip(void)
{
    return g_ip;
}

static int ip_from_route(char *out, size_t size);
static int ip_from_interfaces(char *out, size_t size);

/* Current console IP (the network may have changed: Wi-Fi -> cable, new DHCP lease). 0 = success. */
int ps5tm_current_ip(char *out, size_t size)
{
    char ip[64];
    if (ip_from_route(ip, sizeof(ip)) == 0 || ip_from_interfaces(ip, sizeof(ip)) == 0) {
        snprintf(out, size, "%s", ip);
        return 0;
    }
    return -1;
}

void ps5tm_set_ip(const char *ip)
{
    snprintf(g_ip, sizeof(g_ip), "%s", ip);
}


/* ---------- network ---------- */

/* Address of the selected route: connect() on a UDP socket sends nothing. */
static int ip_from_route(char *out, size_t size)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(9);
    inet_pton(AF_INET, "203.0.113.1", &dst.sin_addr);

    int ok = -1;
    if (connect(fd, (struct sockaddr *)&dst, sizeof(dst)) == 0) {
        struct sockaddr_in me;
        socklen_t len = sizeof(me);
        if (getsockname(fd, (struct sockaddr *)&me, &len) == 0 && me.sin_addr.s_addr != 0 &&
            inet_ntop(AF_INET, &me.sin_addr, out, (socklen_t)size))
            ok = 0;
    }
    close(fd);
    return ok;
}

static int ip_from_interfaces(char *out, size_t size)
{
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0)
        return -1;
    int found = -1;
    for (struct ifaddrs *a = list; a; a = a->ifa_next) {
        int family = a->ifa_addr ? a->ifa_addr->sa_family : -1;
        if (found == 0 || family != AF_INET)
            continue;
        struct in_addr ip = ((struct sockaddr_in *)a->ifa_addr)->sin_addr;
        uint32_t host = ntohl(ip.s_addr);
        if ((host >> 24) == 127 || (host >> 16) == 0xA9FE || host == 0)
            continue;
        if (inet_ntop(AF_INET, &ip, out, (socklen_t)size))
            found = 0;
    }
    freeifaddrs(list);
    return found;
}

void detect_ip(void)
{
    char ip[64];
    if (ip_from_route(ip, sizeof(ip)) == 0 || ip_from_interfaces(ip, sizeof(ip)) == 0)
        snprintf(g_ip, sizeof(g_ip), "%s", ip);
    else
        shim_log("warning: local IP not detected");
}
