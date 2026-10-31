/* A check of src/daemon/dns.c on the host: getaddrinfo() through the wrapper (the test is linked with -Wl,--wrap=getaddrinfo).
 * Run it with a DNS server that never answers (tests/suites/core/test_dns_does_not_block.py sets that up). Prints "name seconds result" lines. */
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>

void shim_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("log: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

static void look(const char *name)
{
    struct timespec a, b;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    clock_gettime(CLOCK_MONOTONIC, &a);
    int rc = getaddrinfo(name, "80", &hints, &res);
    clock_gettime(CLOCK_MONOTONIC, &b);
    printf("lookup %s %.2f %s\n", name, (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9, rc == 0 ? "ok" : rc == EAI_AGAIN ? "again" : "error");
    if (rc == 0)
        freeaddrinfo(res);
    fflush(stdout);
}

int main(void)
{
    look("127.0.0.1"); /* a number never waits */
    look("localhost"); /* /etc/hosts: no DNS server involved */
    look("one.invalid"); /* the first name that the dead DNS server does not answer: given up after the time limit */
    look("two.invalid"); /* the second one: the breaker opens */
    look("three.invalid"); /* the breaker is open: at once */
    look("127.0.0.1");
    return 0;
}
