/* PS5 + Transmission: processes, threads, memory and who holds a disk.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"



/* Whether a process with this name is running (the settings page checks kstuff.elf) */
int ps5tm_process_running(const char *name)
{
    return find_process_by_name(name) > 0;
}


/* All console processes (pid, state, name): to see whether the loader started our daemon and under which name.
 * Returns the number of processes or -1 (with errno) if the list could not be obtained. */
#if !defined(__linux__)
int ps5tm_process_list(char *out, size_t size)
{
    out[0] = 0;
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t len = 0;
    if (sysctl(mib, 4, NULL, &len, NULL, 0) != 0 || len == 0)
        return -1;
    len += len / 8 + 4096;
    char *buf = malloc(len);
    if (!buf)
        return -1;
    int count = 0;
    size_t used = 0;
    if (sysctl(mib, 4, buf, &len, NULL, 0) != 0) {
        int e = errno;
        free(buf);
        errno = e;
        return -1;
    }
    const size_t need = offsetof(struct kinfo_proc, ki_comm) + sizeof(((struct kinfo_proc *)0)->ki_comm);
    static const char states[] = "?IRSTZWL";
    for (char *p = buf; p < buf + len;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)p;
        if (ki->ki_structsize <= 0 || (size_t)ki->ki_structsize < need)
            break;
        p += ki->ki_structsize;
        int n = snprintf(out + used, size - used, "%d %c %s\n", (int)ki->ki_pid, (ki->ki_stat >= 1 && ki->ki_stat <= 7) ? states[(int)ki->ki_stat] : '?', ki->ki_comm);
        if (n < 0 || (size_t)n >= size - used)
            break;
        used += (size_t)n;
        count++;
    }
    free(buf);
    return count;
}
#else
int ps5tm_process_list(char *out, size_t size)
{
    out[0] = 0;
    DIR *d = opendir("/proc");
    if (!d)
        return -1;
    int count = 0;
    size_t used = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        int pid = atoi(e->d_name);
        if (pid <= 0)
            continue;
        char path[64], line[512];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        if (fgets(line, sizeof(line), f)) {
            char *op = strchr(line, '('), *cp = strrchr(line, ')');
            if (op && cp && cp > op) {
                *cp = 0;
                int n = snprintf(out + used, size - used, "%d %c %s\n", pid, cp[2] ? cp[2] : '?', op + 1);
                if (n > 0 && (size_t)n < size - used) {
                    used += (size_t)n;
                    count++;
                }
            }
        }
        fclose(f);
    }
    closedir(d);
    return count;
}
#endif

/* Thread snapshot of our process for "hang" diagnostics: one line per thread:
 * tid, name, state (I idle, R running, S sleeping, T stopped, Z zombie, W waiting, L blocked)
 * and the kernel "wait channel" (for example poll, select, umtxn, sbwait: it shows where the thread is stuck),
 * plus the accumulated CPU time. Returns the number of threads or -1. Read-only. */
#if !defined(__linux__)
static int fetch_kinfo(int *mib, u_int miblen, char **out_buf, size_t *out_len)
{
    size_t len = 0;
    if (sysctl(mib, miblen, NULL, &len, NULL, 0) != 0)
        return errno;
    if (len == 0)
        return ENOENT;
    len += len / 4 + 4096;
    char *buf = malloc(len);
    if (!buf)
        return ENOMEM;
    if (sysctl(mib, miblen, buf, &len, NULL, 0) != 0) {
        int e = errno;
        free(buf);
        return e;
    }
    *out_buf = buf;
    *out_len = len;
    return 0;
}

/* Tries three methods in turn and writes at the start of the text which one worked (and the error codes of the others):
 * 1) only our process with threads; 2) all processes with threads (filtered by pid); 3) the process record without threads
 * (at least the thread count and state). */
int ps5tm_thread_snapshot(int pid, char *out, size_t size)
{
    out[0] = 0;
    size_t used = 0;
    int me = pid;
    struct Mode {
        const char *name;
        int mib[4];
        int filter_pid;
        int per_thread;
    } modes[3] = {
        {"PID|INC_THREAD", {CTL_KERN, KERN_PROC, KERN_PROC_PID | KERN_PROC_INC_THREAD, me}, 0, 1},
        {"PROC|INC_THREAD", {CTL_KERN, KERN_PROC, KERN_PROC_PROC | KERN_PROC_INC_THREAD, 0}, 1, 1},
        {"PID", {CTL_KERN, KERN_PROC, KERN_PROC_PID, me}, 0, 0},
    };
    static const char states[] = "?IRSTZWL"; /* index = ki_stat: 1 SIDL, 2 SRUN, 3 SSLEEP, 4 SSTOP, 5 SZOMB, 6 SWAIT, 7 SLOCK */
    const size_t need = offsetof(struct kinfo_proc, ki_tid) + sizeof(((struct kinfo_proc *)0)->ki_tid);

    for (int m = 0; m < 3; m++) {
        char *buf = NULL;
        size_t len = 0;
        int err = fetch_kinfo(modes[m].mib, 4, &buf, &len);
        int n = snprintf(out + used, size - used, "# sysctl %s: %s%d\n", modes[m].name, err ? "errno " : "ok ", err);
        if (n < 0 || (size_t)n >= size - used)
            return -1;
        used += (size_t)n;
        if (err)
            continue;

        int count = 0;
        for (char *p = buf; p < buf + len;) {
            struct kinfo_proc *ki = (struct kinfo_proc *)p;
            if (ki->ki_structsize <= 0 || (size_t)ki->ki_structsize < need)
                break;
            p += ki->ki_structsize;
            if (modes[m].filter_pid && ki->ki_pid != me)
                continue;
            char state = (ki->ki_stat >= 1 && ki->ki_stat <= 7) ? states[(int)ki->ki_stat] : '?';
            if (modes[m].per_thread)
                n = snprintf(out + used, size - used, "tid=%d name=%s state=%c wait=%s cpu=%llums\n", (int)ki->ki_tid,
                             ki->ki_tdname[0] ? ki->ki_tdname : "-", state, ki->ki_wmesg[0] ? ki->ki_wmesg : "-",
                             (unsigned long long)(ki->ki_runtime / 1000));
            else
                n = snprintf(out + used, size - used, "process: threads=%d state=%c wait=%s cpu=%llums (threads are not available individually)\n",
                             (int)ki->ki_numthreads, state, ki->ki_wmesg[0] ? ki->ki_wmesg : "-",
                             (unsigned long long)(ki->ki_runtime / 1000));
            if (n < 0 || (size_t)n >= size - used)
                break;
            used += (size_t)n;
            count++;
        }
        free(buf);
        if (count > 0)
            return count;
    }
    return -1;
}
#else
/* Host tests only (Linux): the same fields from /proc/self/task */
int ps5tm_thread_snapshot(int pid, char *out, size_t size)
{
    out[0] = 0;
    char task_dir[64];
    snprintf(task_dir, sizeof(task_dir), "/proc/%d/task", pid);
    DIR *d = opendir(task_dir);
    if (!d)
        return -1;
    int count = 0;
    size_t used = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        int tid = atoi(e->d_name);
        if (tid <= 0)
            continue;
        char path[96], comm[64] = "-", wchan[64] = "-", line[512];
        char state = '?';
        snprintf(path, sizeof(path), "/proc/%d/task/%d/comm", pid, tid);
        FILE *f = fopen(path, "r");
        if (f) {
            if (fgets(comm, sizeof(comm), f))
                comm[strcspn(comm, "\n")] = 0;
            fclose(f);
        }
        snprintf(path, sizeof(path), "/proc/%d/task/%d/wchan", pid, tid);
        f = fopen(path, "r");
        if (f) {
            if (fgets(wchan, sizeof(wchan), f) && (wchan[0] == '0' && wchan[1] == 0))
                snprintf(wchan, sizeof(wchan), "-");
            fclose(f);
        }
        snprintf(path, sizeof(path), "/proc/%d/task/%d/stat", pid, tid);
        f = fopen(path, "r");
        if (f) {
            if (fgets(line, sizeof(line), f)) {
                char *close_paren = strrchr(line, ')');
                if (close_paren && close_paren[1] == ' ')
                    state = close_paren[2];
            }
            fclose(f);
        }
        int n = snprintf(out + used, size - used, "tid=%d name=%s state=%c wait=%s cpu=0ms\n", tid, comm, state, wchan);
        if (n < 0 || (size_t)n >= size - used)
            break;
        used += (size_t)n;
        count++;
    }
    closedir(d);
    return count;
}
#endif

/* Whether the TCP port is taken by someone else (bind the same way as the daemon: SO_REUSEADDR). */
int ps5tm_port_in_use(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    int busy = bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0 && errno == EADDRINUSE;
    close(fd);
    return busy;
}

/* ======================= role: manager ======================= */

/* PID of the process with this name (for daemon supervision) or -1 */
int ps5tm_find_pid(const char *name)
{
    return (int)find_process_by_name(name);
}

/* Who holds the disk: processes that have a file, folder or working directory open inside one of the mount points roots. Result lines: pid TAB name TAB open
 * count TAB sample path. PS5: sysctl kern.proc.filedesc (kinfo_file, the path is at the end of the structure); host: /proc/<pid>/fd and cwd. Returns the number of processes (-1: the list is unavailable). */
static int disk_path_under(const char *path, const char *const *roots, int nroots)
{
    for (int i = 0; i < nroots; i++) {
        size_t n = strlen(roots[i]);
        if (strncmp(path, roots[i], n) == 0 && (path[n] == 0 || path[n] == '/'))
            return 1;
    }
    return 0;
}

#if !defined(__linux__)
static int disk_proc_holds(pid_t pid, const char *const *roots, int nroots, char *sample, size_t ssize)
{
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_FILEDESC, (int)pid};
    size_t len = 0;
    int count = 0;
    char *buf;
    if (sysctl(mib, 4, NULL, &len, NULL, 0) != 0 || len == 0)
        return 0;
    len += len / 4 + 4096;
    buf = malloc(len);
    if (buf == NULL)
        return 0;
    if (sysctl(mib, 4, buf, &len, NULL, 0) == 0) {
        for (char *p = buf; p < buf + len;) {
            struct kinfo_file *kf = (struct kinfo_file *)p;
            if (kf->kf_structsize <= 0 || p + kf->kf_structsize > buf + len)
                break;
            if (kf->kf_path[0] == '/' && disk_path_under(kf->kf_path, roots, nroots)) {
                if (count == 0)
                    snprintf(sample, ssize, "%s", kf->kf_path);
                count++;
            }
            p += kf->kf_structsize;
        }
    }
    free(buf);
    return count;
}
#endif

int ps5tm_disk_holders(const char *const *roots, int nroots, char *out, size_t outsize)
{
    int found = 0;
    size_t used = 0;
    out[0] = 0;
#if !defined(__linux__)
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t len = 0;
    char *buf;
    const size_t need = offsetof(struct kinfo_proc, ki_comm) + sizeof(((struct kinfo_proc *)0)->ki_comm);
    if (sysctl(mib, 4, NULL, &len, NULL, 0) != 0 || len == 0)
        return -1;
    len += len / 8 + 4096;
    buf = malloc(len);
    if (buf == NULL)
        return -1;
    if (sysctl(mib, 4, buf, &len, NULL, 0) != 0) {
        free(buf);
        return -1;
    }
    for (char *p = buf; p < buf + len;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)p;
        char sample[260] = "";
        int n;
        if (ki->ki_structsize <= 0 || (size_t)ki->ki_structsize < need)
            break;
        p += ki->ki_structsize;
        if (ki->ki_pid <= 1 || ki->ki_stat == 5 /* SZOMB */)
            continue;
        n = disk_proc_holds(ki->ki_pid, roots, nroots, sample, sizeof(sample));
        if (n > 0 && used + 400 < outsize) {
            used += (size_t)snprintf(out + used, outsize - used, "%d\t%s\t%d\t%s\n", (int)ki->ki_pid, ki->ki_comm, n, sample);
            found++;
        }
    }
    free(buf);
#else
    DIR *d = opendir("/proc");
    struct dirent *e;
    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL) {
        char dir[64], link[300], target[600], sample[260] = "", comm[64] = "?";
        int n = 0;
        if (e->d_name[0] < '1' || e->d_name[0] > '9')
            continue;
        snprintf(link, sizeof(link), "/proc/%s/cwd", e->d_name);
        {
            ssize_t r = readlink(link, target, sizeof(target) - 1);
            if (r > 0) {
                target[r] = 0;
                if (disk_path_under(target, roots, nroots)) {
                    snprintf(sample, sizeof(sample), "%s", target);
                    n++;
                }
            }
        }
        snprintf(dir, sizeof(dir), "/proc/%s/fd", e->d_name);
        {
            DIR *fd = opendir(dir);
            struct dirent *fe;
            while (fd != NULL && (fe = readdir(fd)) != NULL) {
                ssize_t r;
                snprintf(link, sizeof(link), "%s/%s", dir, fe->d_name);
                r = readlink(link, target, sizeof(target) - 1);
                if (r > 0) {
                    target[r] = 0;
                    if (disk_path_under(target, roots, nroots)) {
                        if (n == 0)
                            snprintf(sample, sizeof(sample), "%s", target);
                        n++;
                    }
                }
            }
            if (fd != NULL)
                closedir(fd);
        }
        if (n > 0) {
            FILE *cf;
            snprintf(link, sizeof(link), "/proc/%s/comm", e->d_name);
            cf = fopen(link, "r");
            if (cf != NULL) {
                if (fgets(comm, sizeof(comm), cf) != NULL)
                    comm[strcspn(comm, "\r\n")] = 0;
                fclose(cf);
            }
            if (used + 400 < outsize) {
                used += (size_t)snprintf(out + used, outsize - used, "%s\t%s\t%d\t%s\n", e->d_name, comm, n, sample);
                found++;
            }
        }
    }
    closedir(d);
#endif
    return found;
}

/* Resident memory of the process, KiB (-1 = failed). Like Payload Manager: ki_rssize from the process list
 * (a per-pid query on the PS5 returns a record of a different size than in the SDK headers). */
long ps5tm_process_rss_kib(int pid)
{
#if defined(__linux__)
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/statm", pid);
    long total = 0, resident = 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    int ok = fscanf(f, "%ld %ld", &total, &resident) == 2;
    fclose(f);
    return ok ? resident * (sysconf(_SC_PAGESIZE) / 1024) : -1;
#else
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t len = 0;
    if (sysctl(mib, 4, NULL, &len, NULL, 0) != 0 || len == 0)
        return -1;
    len += len / 8 + 4096;
    char *buf = malloc(len);
    if (!buf)
        return -1;
    long result = -1;
    if (sysctl(mib, 4, buf, &len, NULL, 0) == 0) {
#ifdef PAGE_SIZE
        const long page = PAGE_SIZE;
#else
        const long page = sysconf(_SC_PAGESIZE);
#endif
        for (char *p = buf; p < buf + len;) {
            struct kinfo_proc *ki = (struct kinfo_proc *)p;
            if (ki->ki_structsize <= 0)
                break;
            p += ki->ki_structsize;
            if (ki->ki_pid == pid) {
                result = (long)ki->ki_rssize * page / 1024;
                break;
            }
        }
    }
    free(buf);
    return result;
#endif
}

long self_rss_kib(void)
{
    return ps5tm_process_rss_kib((int)getpid());
}
