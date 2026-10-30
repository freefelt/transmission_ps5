/* PS5 + Transmission: event log of the daemon.
 *
 * License: GPL-3.0-or-later (like Transmission). */

#include "ps5tm_common.h"


/* libevent messages (for example, a poll error) get into our log: the cause is visible, not just the consequence */

void ps5tm_event_log(int severity, const char *msg)
{
    shim_log("libevent[%s]: %s", severity == 0 ? "debug" : severity == 1 ? "msg" : severity == 2 ? "warn" : "err", msg);
}
