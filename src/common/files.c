/* PS5 + Transmission: small file helpers.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"




/* ---------- files ---------- */

int ensure_dir(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        return -1;
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : -1;
}

int write_file_atomic(const char *path, const void *data, size_t length)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;

    const unsigned char *p = data;
    size_t done = 0;
    while (done < length) {
        ssize_t n = write(fd, p + done, length - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            close(fd);
            unlink(tmp);
            return -1;
        }
        done += (size_t)n;
    }
    int rc = fsync(fd);
    if (close(fd) != 0)
        rc = -1;
    if (rc != 0 || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int file_has_content(const char *path, const void *data, size_t length)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || (size_t)st.st_size != length)
        return 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    char buffer[4096];
    const unsigned char *expect = data;
    size_t offset = 0;
    int same = 1;
    while (offset < length) {
        size_t want = length - offset < sizeof(buffer) ? length - offset : sizeof(buffer);
        ssize_t n = read(fd, buffer, want);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0 || memcmp(buffer, expect + offset, (size_t)n) != 0) {
            same = 0;
            break;
        }
        offset += (size_t)n;
    }
    close(fd);
    return same;
}
