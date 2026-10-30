/* PS5 + Transmission: power off and exit of the manager.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"


/* Powering off the console: reboot(RB_HALT | RB_POWEROFF), system call 55 (as in PS5-Power-Payloads-Project). A successful call does not return.
 * Host tests set the PS5TM_TEST_POWEROFF environment variable: then only a mark in a file is made instead of the call. */
int ps5tm_power_off_syscall(void)
{
    if (getenv("PS5TM_TEST_POWEROFF") != NULL) {
        FILE *f = fopen("/tmp/ps5tm_test_poweroff", "w");
        if (f) {
            fputs("poweroff\n", f);
            fclose(f);
        }
        return 0;
    }
    return (int)syscall(55, 0x4008);
}

void ps5tm_exit_now(void)
{
    shim_log("manager: exiting (full quit requested)");
    _exit(0);
}
