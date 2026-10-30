/* PS5 + Transmission: the log of the stages (file and ring in memory) and the notifications on the console.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"

/* ---------- stage log ---------- */

int shim_fd = -1;
struct timespec t0;

/* The log is duplicated in memory (a 64 KiB ring): right after waking up the file system is
 * unavailable for writing for a few seconds, and the lines do not get into the file. From memory they are visible on the settings page. */
#define LOG_RING_SIZE 65536
static char g_ring[LOG_RING_SIZE];
static size_t g_ring_pos;
static int g_ring_wrapped;
static pthread_mutex_t g_ring_mutex = PTHREAD_MUTEX_INITIALIZER;

static void ring_append(const char *s, size_t len)
{
    pthread_mutex_lock(&g_ring_mutex);
    for (size_t i = 0; i < len; i++) {
        g_ring[g_ring_pos++] = s[i];
        if (g_ring_pos == LOG_RING_SIZE) {
            g_ring_pos = 0;
            g_ring_wrapped = 1;
        }
    }
    pthread_mutex_unlock(&g_ring_mutex);
}

/* The last bytes of the log from memory (at most size-1), starting from the beginning of a line. Returns the length. */
size_t ps5tm_log_ring_copy(char *out, size_t size)
{
    if (size == 0)
        return 0;
    pthread_mutex_lock(&g_ring_mutex);
    size_t total = g_ring_wrapped ? LOG_RING_SIZE : g_ring_pos;
    size_t take = total < size - 1 ? total : size - 1;
    size_t start = (g_ring_pos + LOG_RING_SIZE - take) % LOG_RING_SIZE;
    for (size_t i = 0; i < take; i++)
        out[i] = g_ring[(start + i) % LOG_RING_SIZE];
    pthread_mutex_unlock(&g_ring_mutex);
    out[take] = 0;
    if (take < total) {
        char *nl = strchr(out, '\n');
        if (nl) {
            size_t skip = (size_t)(nl - out) + 1;
            memmove(out, out + skip, take - skip + 1);
            take -= skip;
        }
    }
    return take;
}

void shim_log(const char *fmt, ...)
{
    char line[512];
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long ms = (long)(now.tv_sec - t0.tv_sec) * 1000L + (now.tv_nsec - t0.tv_nsec) / 1000000L;
    int n = snprintf(line, sizeof(line), "[%4ld.%03ld] ", ms / 1000, ms % 1000);
    if (n < 0)
        n = 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
    va_end(ap);

    size_t len = strlen(line);
    if (len > sizeof(line) - 2)
        len = sizeof(line) - 2;
    line[len++] = '\n';
    line[len] = '\0';

    ring_append(line, len);
    if (shim_fd >= 0) {
        (void)!write(shim_fd, line, len);
    }
    klog_printf("[Transmission] %s", line);
}


/* ---------- notifications ---------- */

/* journal_kind: how the bell records it (1 an ordinary notification, 3 a system one: a plain English message of the wrapper itself, marked "system") */
static void notify_text(int journal_kind, const char *text)
{
    shim_log("NOTIFY: %s", text);
#if defined(PS5TM_MANAGER)
    ps5tm_journal_add(text, journal_kind);
#else
    (void)journal_kind;
#endif

    unsigned char request[3120];
    memset(request, 0, sizeof(request));
    snprintf((char *)request + 45, sizeof(request) - 45, "%s", text);
    sceKernelSendNotificationRequest(0, request, sizeof(request), 0);
}

void notify(const char *fmt, ...)
{
    char text[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    notify_text(1, text);
}

/* The same, but the bell marks the entry "system": the messages about starting, stopping and failing of the payload itself */
void notify_system(const char *fmt, ...)
{
    char text[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    notify_text(3, text);
}
#define NOTIFY_STARTUP(...) notify_startup(__VA_ARGS__)


/* Notification with ready-made text (used by the torrent monitor, ps5tm_notify.cc) */
void ps5tm_notify_text(const char *text)
{
    notify("%s", text);
}
