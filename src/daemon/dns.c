/* PS5 + Transmission: a time limit for name lookups of the daemon.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>


/* ---------- getaddrinfo with a time limit ----------
 * When the DNS server of the network does not answer (a home network that lets only some DNS servers through, no internet), one lookup takes 30 seconds
 * (the resolver tries again and again), and Transmission waits for it in places where it must not: the web thread that does all the tracker announces, the
 * shutdown that waits for the "stopped" announces. Every lookup of a name therefore runs in a helper thread and is given up after DNS_WAIT_MS (the helper
 * finishes in the background and throws its answer away). Two lookups in a row that time out open a "circuit breaker": for DNS_DOWN_MS every lookup of a name
 * fails at once (EAI_AGAIN, "try again later"); after that one lookup is let through as a probe. A good answer closes the breaker. Numeric addresses
 * never wait. The function is linked with --wrap=getaddrinfo, so the libraries inside the daemon (libtransmission, libcurl) use it as well. */
#define DNS_WAIT_MS 4000
#define DNS_DOWN_MS 30000
#define DNS_TRIPS 2
#define DNS_MAX_THREADS 32
#define DNS_STACK (192 * 1024)

extern int __real_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);

typedef struct {
    char *node;
    char *service;
    struct addrinfo hints;
    int has_hints;
    int rc;
    struct addrinfo *res;
    volatile int state; /* 0 running, 1 finished (the caller takes the answer), 2 abandoned by the caller (the thread cleans up) */
} dns_job_t;

static volatile int dns_threads;
static volatile int dns_timeouts_in_a_row;
static volatile long long dns_down_until_ms;
static volatile int dns_logged;

static long long dns_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void dns_job_free(dns_job_t *j)
{
    free(j->node);
    free(j->service);
    free(j);
}

static void *dns_thread(void *arg)
{
    dns_job_t *j = (dns_job_t *)arg;
    j->rc = __real_getaddrinfo(j->node, j->service, j->has_hints ? &j->hints : NULL, &j->res);
    if (__atomic_exchange_n(&j->state, 1, __ATOMIC_SEQ_CST) == 2) { /* nobody is waiting any more */
        if (j->rc == 0 && j->res != NULL)
            freeaddrinfo(j->res);
        dns_job_free(j);
    }
    __atomic_fetch_sub(&dns_threads, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

static int dns_is_numeric(const char *node)
{
    unsigned char buf[sizeof(struct in6_addr)];
    return inet_pton(AF_INET, node, buf) == 1 || inet_pton(AF_INET6, node, buf) == 1;
}

static void dns_log(const char *fmt, const char *name, long long arg)
{
    int n = __atomic_fetch_add(&dns_logged, 1, __ATOMIC_SEQ_CST);
    if (n < 12 || n % 50 == 0) /* the first ones and then now and then: no flood while the DNS is down */
        shim_log(fmt, name, arg);
}

int __wrap_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
    if (node == NULL || node[0] == 0 || dns_is_numeric(node) || (hints != NULL && (hints->ai_flags & AI_NUMERICHOST) != 0))
        return __real_getaddrinfo(node, service, hints, res);

    long long now = dns_now_ms();
    if (now < dns_down_until_ms) {
        dns_log("dns: '%s': the DNS server did not answer lately, not asking for another %lld ms", node, dns_down_until_ms - now);
        return EAI_AGAIN;
    }
    if (__atomic_load_n(&dns_threads, __ATOMIC_SEQ_CST) >= DNS_MAX_THREADS)
        return EAI_AGAIN;

    dns_job_t *j = (dns_job_t *)calloc(1, sizeof(*j));
    if (j == NULL)
        return __real_getaddrinfo(node, service, hints, res);
    j->node = strdup(node);
    j->service = service != NULL ? strdup(service) : NULL;
    if (hints != NULL) {
        j->hints = *hints;
        j->hints.ai_next = NULL;
        j->hints.ai_addr = NULL;
        j->hints.ai_canonname = NULL;
        j->has_hints = 1;
    }
    pthread_attr_t attr;
    pthread_t th;
    __atomic_fetch_add(&dns_threads, 1, __ATOMIC_SEQ_CST);
    int started = 0;
    if (j->node != NULL && (service == NULL || j->service != NULL) && pthread_attr_init(&attr) == 0) {
        pthread_attr_setstacksize(&attr, DNS_STACK);
        started = pthread_create(&th, &attr, dns_thread, j) == 0;
        pthread_attr_destroy(&attr);
    }
    if (!started) { /* no thread: the old way, without a limit */
        __atomic_fetch_sub(&dns_threads, 1, __ATOMIC_SEQ_CST);
        dns_job_free(j);
        return __real_getaddrinfo(node, service, hints, res);
    }
    pthread_detach(th);

    long long deadline = now + DNS_WAIT_MS;
    while (__atomic_load_n(&j->state, __ATOMIC_SEQ_CST) == 0 && dns_now_ms() < deadline)
        usleep(5000);

    if (__atomic_exchange_n(&j->state, 2, __ATOMIC_SEQ_CST) == 0) { /* gave up: the thread throws its answer away */
        if (++dns_timeouts_in_a_row >= DNS_TRIPS)
            dns_down_until_ms = dns_now_ms() + DNS_DOWN_MS;
        dns_log("dns: '%s' did not answer in %lld ms: giving up", node, (long long)DNS_WAIT_MS);
        return EAI_AGAIN;
    }
    dns_timeouts_in_a_row = 0;
    dns_down_until_ms = 0;
    int rc = j->rc;
    *res = j->res;
    dns_job_free(j);
    return rc;
}
