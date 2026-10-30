/* PS5 + Transmission: the I/O watchdog of the daemon.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"


/* ---------- the daemon I/O watchdog ----------
 * Transmission does all file I/O (and the web interface) in a single thread. If a disk system call hangs (USB disk, exFAT), the daemon goes silent,
 * and from the outside it is not visible WHAT exactly has hung. The file calls of the daemon are wrapped (--wrap): the thread's slot remembers which operation is running, since
 * when, on which file and at what offset. A separate thread once a second looks at the slots and writes to daemon.log "io-watch: blocked <sec> <operation> off=.. len=.. path=.."
 * (at 3, 10, 60, 300, 900 seconds, then once every 30 minutes); when an operation longer than 3 s ends: "io-watch: done <sec> ...". The manager moves these lines
 * into the bell with the duration. Thread-local storage (__thread) is not used: on PS5 under the loader it is not guaranteed. */
#define IOW_SLOTS 48
#define IOW_FDS 4096
#define IOW_PATH 200
enum { IOW_OP_NONE = 0, IOW_OP_OPEN, IOW_OP_PWRITE, IOW_OP_PREAD, IOW_OP_FTRUNCATE, IOW_OP_RENAME, IOW_OP_MKDIR, IOW_OP_UNLINK, IOW_OP_FSYNC, IOW_OP_STAT, IOW_OP_LSTAT, IOW_OP_STATVFS };
static const char *const iow_names[] = { "", "open", "pwrite", "pread", "ftruncate", "rename", "mkdir", "unlink", "fsync", "stat", "lstat", "statvfs" };
typedef struct {
    volatile uintptr_t tid;
    volatile int op;
    volatile long long start_ms;
    volatile int next_report;
    volatile int fd;
    const char *volatile path_ptr;
    long long off, len;
} iow_slot_t;
static iow_slot_t iow_slots[IOW_SLOTS];
static char (*iow_fdpath)[IOW_PATH];

static long long iow_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static iow_slot_t *iow_slot_for_thread(void)
{
    uintptr_t me = (uintptr_t)pthread_self();
    for (int i = 0; i < IOW_SLOTS; i++) {
        if (iow_slots[i].tid == me)
            return &iow_slots[i];
        if (iow_slots[i].tid == 0 && __sync_bool_compare_and_swap(&iow_slots[i].tid, (uintptr_t)0, me))
            return &iow_slots[i];
    }
    return NULL;
}

static iow_slot_t *iow_begin(int op, int fd, long long off, long long len, const char *path)
{
    iow_slot_t *sl = iow_slot_for_thread();
    if (sl == NULL || sl->op != IOW_OP_NONE)
        return NULL; /* no slot, or this is a nested call inside an operation of this thread that is already running: we do not overwrite the slot */
    sl->off = off;
    sl->len = len;
    sl->fd = fd;
    sl->path_ptr = path;
    sl->next_report = 0;
    sl->start_ms = iow_now_ms();
    __sync_synchronize();
    sl->op = op;
    return sl;
}

static const char *iow_path_of(const iow_slot_t *sl, char *tmp, size_t tmpsize)
{
    const char *p = sl->path_ptr;
    int fd = sl->fd;
    if (p != NULL)
        return p;
    if (fd >= 0 && fd < IOW_FDS && iow_fdpath != NULL && iow_fdpath[fd][0] != 0)
        return iow_fdpath[fd];
    snprintf(tmp, tmpsize, "fd %d", fd);
    return tmp;
}

static void iow_end(iow_slot_t *sl)
{
    if (sl == NULL)
        return;
    long long took = iow_now_ms() - sl->start_ms;
    int op = sl->op;
    sl->op = IOW_OP_NONE;
    if (took >= 3000 && op > 0) {
        char tmp[32];
        shim_log("io-watch: done %.1f %s off=%lld len=%lld path=%s", (double)took / 1000.0, iow_names[op], sl->off, sl->len, iow_path_of(sl, tmp, sizeof(tmp)));
    }
}

static void *iow_thread(void *unused)
{
    static const long long thr[] = { 3, 10, 60, 300, 900 }; /* seconds: when to report that the operation is still not finished */
    (void)unused;
    for (;;) {
        sleep(1);
        long long now = iow_now_ms();
        for (int i = 0; i < IOW_SLOTS; i++) {
            iow_slot_t *sl = &iow_slots[i];
            int op = sl->op;
            if (op <= 0)
                continue;
            long long el = (now - sl->start_ms) / 1000;
            int idx = sl->next_report;
            long long limit = idx < 5 ? thr[idx] : 900 + (long long)(idx - 4) * 1800;
            if (el >= limit && sl->op == op) {
                char tmp[32];
                sl->next_report = idx + 1;
                shim_log("io-watch: blocked %lld %s off=%lld len=%lld path=%s", el, iow_names[op], sl->off, sl->len, iow_path_of(sl, tmp, sizeof(tmp)));
            }
        }
    }
    return NULL;
}

void iow_start(void)
{
    pthread_attr_t attr;
    pthread_t th;
    iow_fdpath = calloc((size_t)IOW_FDS, IOW_PATH);
    shim_log("io-watch: started (file calls of the daemon are watched; slow ones are reported here)");
    if (pthread_attr_init(&attr) == 0) {
        pthread_attr_setstacksize(&attr, 128 * 1024);
        if (__real_pthread_create(&th, &attr, iow_thread, NULL) == 0)
            pthread_detach(th);
        pthread_attr_destroy(&attr);
    }
}

#if defined(PS5TM_HOST_TEST)
int __real_unlink(const char *path);
/* Host tests only: the file /tmp/ps5tm_test_io_delay with the text "<ms> <path part>" delays a write to a file whose path contains that part
 * once (simulation of a disk hang). The file is deleted after it triggers. */
static void iow_test_delay(const char *path)
{
    char buf[256];
    FILE *f = fopen("/tmp/ps5tm_test_io_delay", "r");
    int ms = 0;
    char sub[200];
    if (f == NULL)
        return;
    buf[0] = 0;
    if (fgets(buf, sizeof(buf), f) == NULL) {
        fclose(f);
        return;
    }
    fclose(f);
    if (sscanf(buf, "%d %199s", &ms, sub) == 2 && path != NULL && strstr(path, sub) != NULL) {
        __real_unlink("/tmp/ps5tm_test_io_delay");
        usleep((useconds_t)ms * 1000U);
    }
}
#endif


int __wrap_open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    iow_slot_t *sl = iow_begin(IOW_OP_OPEN, -1, 0, 0, path);
    int fd = __real_open(path, flags, (int)mode);
    iow_end(sl);
    if (fd >= 0 && fd < IOW_FDS && iow_fdpath != NULL && path != NULL)
        snprintf(iow_fdpath[fd], IOW_PATH, "%s", path);
    return fd;
}

int __wrap_close(int fd)
{
    if (fd >= 0 && fd < IOW_FDS && iow_fdpath != NULL)
        iow_fdpath[fd][0] = 0;
    return __real_close(fd);
}

ssize_t __wrap_pwrite(int fd, const void *buf, size_t n, off_t off)
{
    iow_slot_t *sl = iow_begin(IOW_OP_PWRITE, fd, (long long)off, (long long)n, NULL);
#if defined(PS5TM_HOST_TEST)
    if (fd >= 0 && fd < IOW_FDS && iow_fdpath != NULL)
        iow_test_delay(iow_fdpath[fd]);
#endif
    ssize_t r = __real_pwrite(fd, buf, n, off);
    iow_end(sl);
    return r;
}

ssize_t __wrap_pread(int fd, void *buf, size_t n, off_t off)
{
    iow_slot_t *sl = iow_begin(IOW_OP_PREAD, fd, (long long)off, (long long)n, NULL);
    ssize_t r = __real_pread(fd, buf, n, off);
    iow_end(sl);
    return r;
}

int __wrap_ftruncate(int fd, off_t length)
{
    iow_slot_t *sl = iow_begin(IOW_OP_FTRUNCATE, fd, (long long)length, 0, NULL);
    int r = __real_ftruncate(fd, length);
    iow_end(sl);
    return r;
}

int __wrap_rename(const char *from, const char *to)
{
    iow_slot_t *sl = iow_begin(IOW_OP_RENAME, -1, 0, 0, from);
    int r = __real_rename(from, to);
    iow_end(sl);
    return r;
}

int __wrap_mkdir(const char *path, mode_t mode)
{
    iow_slot_t *sl = iow_begin(IOW_OP_MKDIR, -1, 0, 0, path);
    int r = __real_mkdir(path, mode);
    iow_end(sl);
    return r;
}

int __wrap_unlink(const char *path)
{
    iow_slot_t *sl = iow_begin(IOW_OP_UNLINK, -1, 0, 0, path);
    int r = __real_unlink(path);
    iow_end(sl);
    return r;
}

int __wrap_fsync(int fd)
{
    iow_slot_t *sl = iow_begin(IOW_OP_FSYNC, fd, 0, 0, NULL);
    int r = __real_fsync(fd);
    iow_end(sl);
    return r;
}

int __wrap_stat(const char *path, struct stat *st)
{
    iow_slot_t *sl = iow_begin(IOW_OP_STAT, -1, 0, 0, path);
    int r = __real_stat(path, st);
    iow_end(sl);
    return r;
}

int __wrap_lstat(const char *path, struct stat *st)
{
    iow_slot_t *sl = iow_begin(IOW_OP_LSTAT, -1, 0, 0, path);
    int r = __real_lstat(path, st);
    iow_end(sl);
    return r;
}

int __wrap_statvfs(const char *path, struct statvfs *sv)
{
    iow_slot_t *sl = iow_begin(IOW_OP_STATVFS, -1, 0, 0, path);
    int r = __real_statvfs(path, sv);
    iow_end(sl);
    return r;
}
