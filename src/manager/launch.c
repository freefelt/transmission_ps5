/* PS5 + Transmission: launch of the ELF files (daemon, icon helper) through the loader.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"


size_t ps5tm_icon_helper_size(void)
{
    return (size_t)(ps5tm_icon_elf_end - ps5tm_icon_elf);
}

/* Size of the built-in daemon (the symbol addresses are known even when the image pages have already been released) */
size_t ps5tm_daemon_size(void)
{
    return (size_t)(ps5tm_daemon_elf_end - ps5tm_daemon_elf);
}

/* Daemon file check: it reads and looks like an ELF of the expected size. 0 = usable, otherwise -1 and the reason in why. */
static int check_daemon_file(const char *path, size_t expected_size, int *fd_out, char *why, size_t whysize)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        snprintf(why, whysize, "cannot open %s (errno %d)", path, errno);
        return -1;
    }
    struct stat st;
    unsigned char magic[4] = {0};
    if (fstat(fd, &st) != 0) {
        snprintf(why, whysize, "fstat %s: errno %d", path, errno);
        close(fd);
        return -1;
    }
    ssize_t n = read(fd, magic, 4);
    int read_errno = errno;
    if (n != 4 || memcmp(magic, "\x7f" "ELF", 4) != 0 ||
        (expected_size == 0 ? st.st_size < (1 << 20) : (size_t)st.st_size != expected_size)) {
        snprintf(why, whysize,
                 "ELF file is corrupt: size %lld (expected %zu), first bytes %02x%02x%02x%02x, read %zd (errno %d)",
                 (long long)st.st_size, expected_size, magic[0], magic[1], magic[2], magic[3], n, n < 0 ? read_errno : 0);
        close(fd);
        return -1;
    }
    *fd_out = fd;
    return 0;
}

/* Sends the ELF file to the loader at 127.0.0.1:9021 (the same way Payload Manager and nc do). Before sending, the file
 * is checked (ELF, size at least 1 MiB and equal to the expected one). Right after the console wakes from sleep the file system
 * is unavailable or returns garbage for a few seconds, so a failed check is retried for up to 30 seconds.
 * 0 = sent; otherwise -1 and the text in err. */
#define ELFLDR_PORT 9021
int ps5tm_launch_elf(const char *path, size_t expected_size, char *err, size_t errsize)
{
    int fd = -1;
    char why[240] = "";
    for (int attempt = 0; attempt < 30; attempt++) {
        if (attempt > 0)
            sleep(1);
        if (check_daemon_file(path, expected_size, &fd, why, sizeof(why)) == 0)
            break;
        fd = -1;
    }
    if (fd < 0) {
        snprintf(err, errsize, "%s", why);
        return -1;
    }
    lseek(fd, 0, SEEK_SET);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        snprintf(err, errsize, "socket: errno %d", errno);
        close(fd);
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ELFLDR_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        snprintf(err, errsize, "no connection to the ELF loader on port %d (errno %d)", ELFLDR_PORT, errno);
        close(sock);
        close(fd);
        return -1;
    }

    char buffer[8192];
    ssize_t got;
    int failed = 0;
    size_t total = 0;
    while (!failed && (got = read(fd, buffer, sizeof(buffer))) > 0) {
        ssize_t off = 0;
        while (off < got) {
            ssize_t sent = send(sock, buffer + off, (size_t)(got - off), 0);
            if (sent <= 0) {
                snprintf(err, errsize, "connection dropped while sending the ELF (errno %d)", errno);
                failed = 1;
                break;
            }
            off += sent;
        }
        total += (size_t)got;
    }
    if (!failed && got < 0) { /* read error in the middle of the file: the loader would have received a truncated ELF */
        snprintf(err, errsize, "error reading the daemon file after %zu bytes (errno %d)", total, errno);
        failed = 1;
    }
    close(sock);
    close(fd);
    return failed ? -1 : 0;
}

/* Puts the built-in daemon into /data/Transmission/transmission_d.elf. 1 = the file was rewritten (it was missing
 * or differed: for example, the manager was updated), 0 = already up to date, -1 = error. */
static int g_blob_changed;

int ps5tm_daemon_blob_changed(void)
{
    return g_blob_changed;
}

int prepare_daemon_file(void)
{
    size_t size = (size_t)(ps5tm_daemon_elf_end - ps5tm_daemon_elf);
    if (file_has_content(DAEMON_PATH, ps5tm_daemon_elf, size))
        return 0;
    if (write_file_atomic(DAEMON_PATH, ps5tm_daemon_elf, size) != 0)
        return -1;
    g_blob_changed = 1;
    return 1;
}

/* The daemon is embedded in the manager image (10 MB) and is no longer needed once unpacked into a file. The loader puts the image
 * into memory as a whole, and these pages would stay resident forever: Payload Manager would show
 * 10 MB more for the manager than it needs. On FreeBSD only munmap reduces resident memory
 * (madvise does not release pages), so we unmap whole pages lying inside the daemon block. */
void release_embedded_daemon(void)
{
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
        page = 4096;
    uintptr_t start = ((uintptr_t)ps5tm_daemon_elf + (uintptr_t)page - 1) & ~((uintptr_t)page - 1);
    uintptr_t end = (uintptr_t)ps5tm_daemon_elf_end & ~((uintptr_t)page - 1);
    if (end <= start)
        return;
    long before = self_rss_kib();
    if (munmap((void *)start, end - start) != 0) {
        shim_log("memory: munmap of the embedded daemon failed (errno %d)", errno);
        return;
    }
    shim_log("memory: embedded daemon image released (%lu KiB): resident %ld KiB -> %ld KiB", (unsigned long)((end - start) / 1024),
             before, self_rss_kib());
}
