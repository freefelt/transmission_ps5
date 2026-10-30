/* PS5 + Transmission: crash diagnostics and the thread / fork / exec wrappers.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"



/* ---------- threads ---------- */


/* On the console the default thread stack is small for OpenSSL/curl: we give 2 MiB
 * if the thread creator did not specify its own size. */
int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start)(void *), void *arg)
{
    pthread_attr_t local;
    int rc;
    if (attr == NULL && pthread_attr_init(&local) == 0) {
        pthread_attr_setstacksize(&local, WORKER_STACK);
        rc = __real_pthread_create(thread, &local, start, arg);
        pthread_attr_destroy(&local);
    } else {
        rc = __real_pthread_create(thread, attr, start, arg);
    }
    return rc;
}

/* Transmission runs scripts (script-torrent-done and others) through fork+execvp.
 * The web interface is open without a password, and scripts can be enabled through RPC, so
 * starting processes is disabled entirely: the calls just fail. */
pid_t __wrap_fork(void)
{
    errno = EPERM;
    return -1;
}

int __wrap_execvp(const char *file, char *const argv[])
{
    (void)file;
    (void)argv;
    errno = EPERM;
    return -1;
}

/* ---------- crash interception and the "dump" of hung threads ---------- */

static void put_str(char **p, const char *s)
{
    while (*s)
        *(*p)++ = *s++;
}

static void put_hex(char **p, uintptr_t v)
{
    static const char digits[] = "0123456789abcdef";
    int started = 0;
    *(*p)++ = '0';
    *(*p)++ = 'x';
    for (int shift = (int)sizeof(v) * 8 - 4; shift >= 0; shift -= 4) {
        int d = (int)((v >> shift) & 0xf);
        if (d || started || shift == 0) {
            *(*p)++ = digits[d];
            started = 1;
        }
    }
}

#if defined(__linux__)
#define CTX_REG(uc, name_linux, name_bsd) ((uintptr_t)(uc)->uc_mcontext.gregs[name_linux])
#define CTX_RIP(uc) CTX_REG(uc, REG_RIP, mc_rip)
#define CTX_RSP(uc) CTX_REG(uc, REG_RSP, mc_rsp)
#define CTX_RBP(uc) CTX_REG(uc, REG_RBP, mc_rbp)
#define CTX_RAX(uc) CTX_REG(uc, REG_RAX, mc_rax)
#define CTX_RBX(uc) CTX_REG(uc, REG_RBX, mc_rbx)
#define CTX_RCX(uc) CTX_REG(uc, REG_RCX, mc_rcx)
#define CTX_RDX(uc) CTX_REG(uc, REG_RDX, mc_rdx)
#define CTX_RSI(uc) CTX_REG(uc, REG_RSI, mc_rsi)
#define CTX_RDI(uc) CTX_REG(uc, REG_RDI, mc_rdi)
#define CTX_R8(uc) CTX_REG(uc, REG_R8, mc_r8)
#define CTX_R9(uc) CTX_REG(uc, REG_R9, mc_r9)
#define CTX_R12(uc) CTX_REG(uc, REG_R12, mc_r12)
#define CTX_R13(uc) CTX_REG(uc, REG_R13, mc_r13)
#define CTX_R14(uc) CTX_REG(uc, REG_R14, mc_r14)
#define CTX_R15(uc) CTX_REG(uc, REG_R15, mc_r15)
#else
#define CTX_RIP(uc) ((uintptr_t)(uc)->uc_mcontext.mc_rip)
#define CTX_RSP(uc) ((uintptr_t)(uc)->uc_mcontext.mc_rsp)
#define CTX_RBP(uc) ((uintptr_t)(uc)->uc_mcontext.mc_rbp)
#define CTX_RAX(uc) ((uintptr_t)(uc)->uc_mcontext.mc_rax)
#define CTX_RBX(uc) ((uintptr_t)(uc)->uc_mcontext.mc_rbx)
#define CTX_RCX(uc) ((uintptr_t)(uc)->uc_mcontext.mc_rcx)
#define CTX_RDX(uc) ((uintptr_t)(uc)->uc_mcontext.mc_rdx)
#define CTX_RSI(uc) ((uintptr_t)(uc)->uc_mcontext.mc_rsi)
#define CTX_RDI(uc) ((uintptr_t)(uc)->uc_mcontext.mc_rdi)
#define CTX_R8(uc) ((uintptr_t)(uc)->uc_mcontext.mc_r8)
#define CTX_R9(uc) ((uintptr_t)(uc)->uc_mcontext.mc_r9)
#define CTX_R12(uc) ((uintptr_t)(uc)->uc_mcontext.mc_r12)
#define CTX_R13(uc) ((uintptr_t)(uc)->uc_mcontext.mc_r13)
#define CTX_R14(uc) ((uintptr_t)(uc)->uc_mcontext.mc_r14)
#define CTX_R15(uc) ((uintptr_t)(uc)->uc_mcontext.mc_r15)
#endif


/* Safe reading of foreign memory: write() into a pipe returns EFAULT instead of crashing. */
static int probe_pipe[2] = {-1, -1};

static int probe_read(const void *addr, void *out, size_t n)
{
    if (probe_pipe[1] < 0 || addr == NULL)
        return -1;
    if (write(probe_pipe[1], addr, n) != (ssize_t)n)
        return -1;
    return read(probe_pipe[0], out, n) == (ssize_t)n ? 0 : -1;
}

static void put_kv(char **p, const char *key, uintptr_t value)
{
    put_str(p, " ");
    put_str(p, key);
    put_str(p, "=");
    put_hex(p, value);
}

static void log_raw(const char *buf, size_t len)
{
    if (shim_fd >= 0) {
        (void)!write(shim_fd, buf, len);
        fsync(shim_fd);
    }
}

static void diag_handler(int sig, siginfo_t *si, void *context)
{
    char buf[700];
    char *p = buf;
    ucontext_t *uc = (ucontext_t *)context;

    put_str(&p, "FATAL");
    put_kv(&p, "sig", (uintptr_t)sig);
    put_kv(&p, "tid", (uintptr_t)pthread_self());
    put_kv(&p, "rip", uc ? CTX_RIP(uc) : 0);
    put_kv(&p, "rsp", uc ? CTX_RSP(uc) : 0);
    put_kv(&p, "addr", si ? (uintptr_t)si->si_addr : 0);
    put_kv(&p, "main", (uintptr_t)&main);
    *p++ = '\n';
    log_raw(buf, (size_t)(p - buf));

    if (uc) {
        /* registers */
        p = buf;
        put_str(&p, "REGS");
        put_kv(&p, "rax", CTX_RAX(uc));
        put_kv(&p, "rbx", CTX_RBX(uc));
        put_kv(&p, "rcx", CTX_RCX(uc));
        put_kv(&p, "rdx", CTX_RDX(uc));
        put_kv(&p, "rsi", CTX_RSI(uc));
        put_kv(&p, "rdi", CTX_RDI(uc));
        put_kv(&p, "rbp", CTX_RBP(uc));
        put_kv(&p, "r8", CTX_R8(uc));
        put_kv(&p, "r9", CTX_R9(uc));
        put_kv(&p, "r12", CTX_R12(uc));
        put_kv(&p, "r13", CTX_R13(uc));
        put_kv(&p, "r14", CTX_R14(uc));
        put_kv(&p, "r15", CTX_R15(uc));
        *p++ = '\n';
        log_raw(buf, (size_t)(p - buf));

        /* 16 bytes of code at rip */
        unsigned char code[16];
        p = buf;
        put_str(&p, "CODE@rip:");
        if (probe_read((void *)CTX_RIP(uc), code, sizeof(code)) == 0) {
            static const char hexd[] = "0123456789abcdef";
            for (size_t i = 0; i < sizeof(code); i++) {
                *p++ = ' ';
                *p++ = hexd[code[i] >> 4];
                *p++ = hexd[code[i] & 15];
            }
        } else {
            put_str(&p, " <unreadable>");
        }
        *p++ = '\n';
        log_raw(buf, (size_t)(p - buf));

        /* 8 words at the top of the stack, then the frame chain via rbp */
        uintptr_t words[8];
        p = buf;
        put_str(&p, "STACK@rsp:");
        if (probe_read((void *)CTX_RSP(uc), words, sizeof(words)) == 0) {
            for (size_t i = 0; i < 8; i++) {
                *p++ = ' ';
                put_hex(&p, words[i]);
            }
        } else {
            put_str(&p, " <unreadable>");
        }
        *p++ = '\n';
        log_raw(buf, (size_t)(p - buf));

        p = buf;
        put_str(&p, "FRAMES(rbp):");
        uintptr_t frame = CTX_RBP(uc);
        for (int depth = 0; depth < 12; depth++) {
            uintptr_t pair[2];
            if (probe_read((void *)frame, pair, sizeof(pair)) != 0)
                break;
            *p++ = ' ';
            put_hex(&p, pair[1]);
            if (pair[0] <= frame)
                break;
            frame = pair[0];
        }
        *p++ = '\n';
        log_raw(buf, (size_t)(p - buf));
    }

    signal(sig, SIG_DFL); /* we return from the handler: the fault repeats and kills the process */
}

void install_diagnostics(void)
{
    if (pipe(probe_pipe) == 0) {
        fcntl(probe_pipe[0], F_SETFL, fcntl(probe_pipe[0], F_GETFL, 0) | O_NONBLOCK);
        fcntl(probe_pipe[1], F_SETFL, fcntl(probe_pipe[1], F_GETFL, 0) | O_NONBLOCK);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = diag_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    const int fatal[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
    for (size_t i = 0; i < sizeof(fatal) / sizeof(fatal[0]); i++)
        sigaction(fatal[i], &sa, NULL);
}
