/* ps3_log.h -- gate for high-volume diagnostic logging.
 *
 * Chatty per-wait / per-round logs stay FULL when stderr is redirected
 * (debug runs capture to a file) or PS3_VERBOSE is set, and go quiet on a
 * live console: conhost flushes stall the emitting threads, and the ~20k
 * [WAIT] lines/minute of an LBP intro run showed up as harsh ~1 Hz hitches
 * in user-visible runs while file-redirected runs played smoothly.
 */
#ifndef PS3_LOG_H
#define PS3_LOG_H

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <io.h>
#define PS3_ISATTY_STDERR() _isatty(_fileno(stderr))
#else
#include <unistd.h>
#define PS3_ISATTY_STDERR() isatty(fileno(stderr))
#endif

#ifdef __cplusplus
static inline int ps3_diag_enabled(void)
#else
static __inline int ps3_diag_enabled(void)
#endif
{
    /* Resistance port: R2_DIAG=1 turns on the debug diagnostics (periodic
     * audio/SPURS stats, logs\*.txt side files, per-wait tracing). Off by
     * default so release runs stay quiet. */
    static int d = -1;
    if (d < 0) {
        const char* e = getenv("R2_DIAG");
        d = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return d;
}

#ifdef __cplusplus
static inline int ps3_log_verbose(void)
#else
static __inline int ps3_log_verbose(void)
#endif
{
    /* PS3_VERBOSE, when set, decides -- INCLUDING "0" to force quiet. Without it
     * (Resistance port) R2_DIAG decides: run.bat always pipes stderr into
     * logs\run.log, so the old "redirected stderr = verbose" default made every
     * release run trace each event-queue wait (~100 lines/s).
     *
     * That default has a sharp edge worth knowing about. The per-event lines are
     * emitted from every guest thread through one FILE lock, and Windows locks
     * are not fair, so at the ~6k lines/s a SPURS audio loop produces a thread
     * can starve on the lock for seconds. Tokyo Jungle presents four frames in
     * 40 s with the log discarded and one with it redirected to a file -- the
     * logging changes what the title does. Measure timing-sensitive behaviour
     * with PS3_VERBOSE=0. */
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("PS3_VERBOSE");
        if (e) v = (e[0] && e[0] != '0') ? 1 : 0;
        else   v = ps3_diag_enabled();
    }
    return v;
}

#endif /* PS3_LOG_H */
