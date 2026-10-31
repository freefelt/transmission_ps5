/* PS5 + Transmission: the internal interface of the wrapper modules (src/common, src/daemon, src/manager).
 * Everything here is shared between modules; each module keeps its private helpers static.
 *
 * License: GPL-3.0-or-later (like Transmission). */
#pragma once
#if !defined(PS5TM_DAEMON) && !defined(PS5TM_MANAGER)
#error "define PS5TM_DAEMON or PS5TM_MANAGER"
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#ifndef __linux__
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#endif
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#if defined(__linux__)
#include <dirent.h>
#include <sys/prctl.h>
#endif
#if !defined(__linux__)
#include <sys/param.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#endif
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

/* Application folder layout:
 *   /data/Transmission/
 *     config.cfg, daemon.desired, paused_by_stop.txt  manager state
 *     logs/                                           all logs (shim.log, daemon.log, console.log, transmission.log)
 *     transmission/                                   everything related to the daemon: transmission_d.elf, config/ (settings
 *                                                     and torrents of Transmission), web/, tmp/, cacert.pem */
#define BASE_DIR "/data/Transmission"
#define LOGS_DIR BASE_DIR "/logs"
#define DAEMON_DIR BASE_DIR "/transmission"
#define CONFIG_DIR DAEMON_DIR "/config"
#define TMP_DIR DAEMON_DIR "/tmp"
/* Download folder on first start: SSD (extended storage), otherwise internal storage */
#define SSD_ROOT "/mnt/ext1"
#define SSD_DOWNLOAD_DIR SSD_ROOT "/torrents"
#define INTERNAL_DOWNLOAD_DIR "/data/torrents"
#define CA_FILE DAEMON_DIR "/cacert.pem"
#define WEB_DIR DAEMON_DIR "/web"
#define LOG_FILE LOGS_DIR "/transmission.log"
#if defined(PS5TM_DAEMON)
#define SHIM_LOG LOGS_DIR "/daemon.log"
#define PROC_NAME "transmission_d.elf"
#else
#define SHIM_LOG LOGS_DIR "/shim.log"
#define PROC_NAME "Transmission.elf"
#define DAEMON_PROC_NAME "transmission_d.elf"
#define DAEMON_PATH DAEMON_DIR "/transmission_d.elf"
#define ICON_DIR BASE_DIR "/icon"
#define ICON_HELPER_PATH ICON_DIR "/transmission_i.elf"
#endif
#define CONSOLE_LOG LOGS_DIR "/console.log"
#define SETTINGS_FILE CONFIG_DIR "/settings.json"
#define CONFIG_CFG BASE_DIR "/config.cfg" /* wrapper settings: delays, notifications */

#define RPC_PORT 9091
/* The version comes from the VERSION file in the repository root: the build scripts pass -DSHIM_VERSION="x.y.z". */
#ifndef SHIM_VERSION
#error "SHIM_VERSION is not defined: build with ./build.sh or pass -DSHIM_VERSION=\"x.y.z\""
#endif

/* The version of the SDK the payload is built with: the build scripts pass -DPS5TM_SDK_VERSION="v0.45" (it is shown in the diagnostics). */
#ifndef PS5TM_SDK_VERSION
#define PS5TM_SDK_VERSION "unknown"
#endif

#define DAEMON_STACK (8u * 1024u * 1024u)
#define WORKER_STACK (2u * 1024u * 1024u)

#if defined(PS5TM_DAEMON)
extern int tr_daemon_main(int argc, char **argv);
#else
extern void *ps5tm_monitor_thread(void *arg);
extern int ps5tm_config_init(const char *path);
extern void *ps5tm_ui_thread(void *arg);
extern int ps5tm_cfg_notify_startup(void);
extern void ps5tm_journal_add(const char *text, int shown); /* notification log (bell on the page) */
extern const unsigned char ps5tm_daemon_elf[];     /* embedded daemon (daemon_blob.S) */
extern const unsigned char ps5tm_daemon_elf_end[];
extern const unsigned char ps5tm_icon_elf[];       /* icon install helper (icon_blob.S) */
extern const unsigned char ps5tm_icon_elf_end[];
#endif
extern int sceKernelSendNotificationRequest(int device, void *request, size_t size, int flags);
extern int klog_printf(const char *fmt, ...);


/* ---------- shared between modules ---------- */
#define GUARD_MAX 64
#define GUARD_PATH 512
#define GUARD_NO_FOLDER "/dev/null/ps5tm-no-download-folder"
int main();
#if defined(PS5TM_MANAGER)
#define NOTIFY_STARTUP(...) notify_startup(__VA_ARGS__)
#endif
extern int shim_fd;
extern struct timespec t0;
void shim_log(const char *fmt, ...);

/* libevent messages (for example, a poll error) get into our log: the cause is visible, not just the consequence */
typedef void (*ps5tm_event_log_cb)(int severity, const char *msg);

void ps5tm_event_log(int severity, const char *msg);
void notify(const char *fmt, ...);
void notify_system(const char *fmt, ...); /* the same, but the bell marks the entry "system" */
void notify_startup(const char *fmt, ...);
void on_sigcont(int sig);
void install_diagnostics(void);
void set_process_name(void);
pid_t find_process_by_name(const char *name);
pid_t find_other_instance(void);
size_t ps5tm_icon_helper_size(void);
int ps5tm_resolve_alias(const char *in, char *out, size_t size);
int ps5tm_volume_info(const char *root, char *fstype, size_t fssize, char *device, size_t devsize);
int ps5tm_disk_kind_of_root(const char *root);
int ps5tm_fs_needs_fix(const char *fstype);
int ensure_dir(const char *path);
int write_file_atomic(const char *path, const void *data, size_t length);
#if defined(PS5TM_DAEMON)
void ps5tm_guard_remember_default(const char *path, int only_if_missing); /* daemon/guard.c: the default folder chosen by the user */
#endif
int file_has_content(const char *path, const void *data, size_t length);
int ensure_web_files(void);
void apply_web_theme(void);
void apply_web_lang(void);
void apply_web_icons(void);
void apply_web_config(void);
int write_default_settings(void);
int guard_normalize(const char *in, char *out, size_t size);
int guard_load(char (*list)[GUARD_PATH], int max);
int guard_find(char (*list)[GUARD_PATH], int n, const char *norm);
void guard_enforce_settings(void);
void detect_ip(void);
extern volatile int ps5tm_daemon_finished;
void iow_start(void);
int prepare_daemon_file(void);
long self_rss_kib(void);
void release_embedded_daemon(void);
extern void event_set_log_callback(ps5tm_event_log_cb cb);
int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start)(void *), void *arg);
int __real_open(const char *path, int flags, ...);
int __real_close(int fd);
ssize_t __real_pwrite(int fd, const void *buf, size_t n, off_t off);
ssize_t __real_pread(int fd, void *buf, size_t n, off_t off);
int __real_ftruncate(int fd, off_t length);
int __real_rename(const char *from, const char *to);
int __real_mkdir(const char *path, mode_t mode);
int __real_unlink(const char *path);
int __real_fsync(int fd);
int __real_stat(const char *path, struct stat *st);
int __real_lstat(const char *path, struct stat *st);
int __real_statvfs(const char *path, struct statvfs *sv);
void ensure_ca_file(void);
unsigned web_file_count(void);
