#include <poll.h>
#include <errno.h>
#include <unistd.h>
/* TEST: injecting a poll() failure: while the file /tmp/ps5tm_test_poll_fail exists, poll() returns EINVAL
 * (as the console might return an error on losing the network). Works when linked with --wrap=poll. */
int __real_poll(struct pollfd *fds, nfds_t n, int t);
int __wrap_poll(struct pollfd *fds, nfds_t n, int t) {
    if (access("/tmp/ps5tm_test_poll_fail", F_OK) == 0) { errno = EINVAL; return -1; }
    return __real_poll(fds, n, t);
}
