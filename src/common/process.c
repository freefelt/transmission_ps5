/* PS5 + Transmission: the process name and the search for other instances.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"


/* ---------- process name and restart ---------- */

#if !defined(__linux__)
/* "Active Processes" in Payload Manager shows ki_comm. The ELF loader gets
 * only the bytes of the file and names unnamed payloads "payload.elf", so
 * the payload sets its own name (pldmgr.elf, shadowmountplus.elf and others do this). */
void set_process_name(void)
{
    syscall(SYS_thr_set_name, -1, PROC_NAME);
}

/* PID of the process with the given name (not our own) or -1. As in Payload Manager, the loop over entries
 * stops only when ki_structsize == 0 (or if the entry is too short to
 * contain the name): the entry size in the PS5 kernel does not have to match the sizeof from the SDK headers. */
pid_t find_process_by_name(const char *name)
{
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t size = 0;
    if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0 || size == 0)
        return -1;
    size += size / 8 + 4096; /* processes may appear between the two calls */
    char *buf = malloc(size);
    if (!buf)
        return -1;

    const size_t need = offsetof(struct kinfo_proc, ki_comm) + sizeof(((struct kinfo_proc *)0)->ki_comm);
    pid_t found = 0;
    if (sysctl(mib, 4, buf, &size, NULL, 0) != 0) {
        free(buf);
        return -1;
    }
    {
        for (char *p = buf; p < buf + size;) {
            struct kinfo_proc *ki = (struct kinfo_proc *)p;
            if (ki->ki_structsize <= 0 || (size_t)ki->ki_structsize < need)
                break;
            p += ki->ki_structsize;
            if (ki->ki_pid > 1 && ki->ki_pid != getpid() && ki->ki_stat != 5 /* SZOMB */ &&
                strncmp(ki->ki_comm, name, sizeof(ki->ki_comm)) == 0) {
                found = ki->ki_pid;
                break;
            }
        }
    }
    free(buf);
    return found;
}
#else
/* Host tests only (Linux): the process name through prctl, the search through /proc.
 * comm in Linux is limited to 15 characters. */
void set_process_name(void)
{
    prctl(PR_SET_NAME, PROC_NAME, 0, 0, 0);
}

pid_t find_process_by_name(const char *name)
{
    DIR *d = opendir("/proc");
    if (!d)
        return -1;
    pid_t found = 0;
    struct dirent *e;
    while (found <= 0 && (e = readdir(d)) != NULL) {
        pid_t pid = (pid_t)atoi(e->d_name);
        if (pid <= 1 || pid == getpid())
            continue;
        char path[64], line[512];
        snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        if (fgets(line, sizeof(line), f)) {
            char *open_paren = strchr(line, '(');
            char *close_paren = strrchr(line, ')');
            /* comm in Linux is truncated to 15 characters: we compare in full what is between the parentheses */
            size_t name_len = strnlen(name, 15);
            if (open_paren && close_paren && close_paren > open_paren && close_paren[1] == ' ' && close_paren[2] != 'Z' &&
                (size_t)(close_paren - open_paren - 1) == name_len && strncmp(open_paren + 1, name, name_len) == 0)
                found = pid;
        }
        fclose(f);
    }
    closedir(d);
    return found;
}
#endif

/* Another instance of Transmission (with our process name) */
pid_t find_other_instance(void)
{
    return find_process_by_name(PROC_NAME);
}


/* ---------- daemon ---------- */

volatile int ps5tm_daemon_finished;
