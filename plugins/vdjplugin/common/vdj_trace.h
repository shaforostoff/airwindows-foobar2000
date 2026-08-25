/* ========================================
 *  vdjplugin - optional load trace
 *
 *  Off unless the build defines VDJ_TRACE, in which case every plug-in appends
 *  a line to a log file as it is asked for, loaded, interrogated, started and
 *  first handed audio.
 *
 *  This exists for one question, which is the hardest one in plug-in work and
 *  cannot be answered from outside: when a host does not list your plug-in, did
 *  it look at the file and reject it, or did it never look at all? Nothing
 *  observable distinguishes those - the folder contents, the exports, the
 *  architecture and the IIDs can all be verified correct while the host still
 *  shows nothing, and vdj_host_verify proves only that *a* host can load it.
 *  So the plug-in has to be the witness.
 *
 *  Reading the log:
 *
 *    empty                          the host never opened the file. The problem
 *                                   is discovery: wrong folder, wrong
 *                                   architecture, or the scan did not run.
 *    "DllGetClassObject" only       it was loaded and asked for an interface it
 *                                   does not implement - the line says which -
 *                                   so the host thinks it is a different kind
 *                                   of plug-in.
 *    "...OnGetPluginInfo"           the host has the plug-in and its name. It
 *                                   is registered, and anything still missing
 *                                   is in the host's own UI.
 *    "...OnStart" / "first buffer"  it is running and being given audio.
 *
 *  Cost when off: nothing at all - the macro expands to a void expression, so
 *  the format strings are not even in the binary. Cost when on: an fopen and an
 *  fclose per event, and the events on the audio thread happen once each. Not
 *  something to ship, which is why the CMake option defaults to off.
 * ======================================== */

#ifndef VDJ_TRACE_H
#define VDJ_TRACE_H

#if defined(VDJ_TRACE)

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#include <process.h>
#endif

namespace vdj {

//! Where the log goes. One file for all four plug-ins, appended to, so the
//! order of the lines shows the order the host did things in.
inline const char * tracePath() {
    static char path[1024];
    if (path[0] != 0) return path;
#if defined(_WIN32)
    const char * env = getenv("VDJ_TRACE_LOG");
    if (env != NULL && env[0] != 0) {
        snprintf(path, sizeof(path), "%s", env);
        return path;
    }
    const char * tmp = getenv("TEMP");
    if (tmp == NULL || tmp[0] == 0) tmp = getenv("TMP");
    if (tmp == NULL || tmp[0] == 0) tmp = "C:";
    snprintf(path, sizeof(path), "%s\\shellacfilters_vdj_trace.log", tmp);
#else
    const char * env = getenv("VDJ_TRACE_LOG");
    snprintf(path, sizeof(path), "%s",
             (env != NULL && env[0] != 0) ? env : "/tmp/shellacfilters_vdj_trace.log");
#endif
    return path;
}

//! Opened and closed per line rather than held open, so that a host which
//! crashes, or is killed, still leaves everything up to that point on disk -
//! which is exactly the case this is wanted for.
inline void trace(const char * fmt, ...) {
    FILE * f = fopen(tracePath(), "a");
    if (f == NULL) return;

    char stamp[64];
    stamp[0] = 0;
    const time_t now = time(NULL);
    struct tm parts;
#if defined(_WIN32)
    if (localtime_s(&parts, &now) == 0) {
        strftime(stamp, sizeof(stamp), "%H:%M:%S", &parts);
    }
    fprintf(f, "%s pid %-6u ", stamp, (unsigned)_getpid());
#else
    if (localtime_r(&now, &parts) != NULL) {
        strftime(stamp, sizeof(stamp), "%H:%M:%S", &parts);
    }
    fprintf(f, "%s ", stamp);
#endif

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);

    fputc('\n', f);
    fclose(f);
}

//! GUIDs are what the whole question turns on, so they get printed rather than
//! summarised as matched/not.
inline const char * traceGuid(const GUID & g, char * out, size_t size) {
    snprintf(out, size,
             "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
             (unsigned long)g.Data1, (unsigned)g.Data2, (unsigned)g.Data3,
             g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
             g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return out;
}

} // namespace vdj

#define VDJ_TRACEF(...) ::vdj::trace(__VA_ARGS__)
#define VDJ_TRACE_GUID(g, buf) ::vdj::traceGuid((g), (buf), sizeof(buf))

#else   // !VDJ_TRACE

#define VDJ_TRACEF(...) ((void)0)

#endif

#endif // VDJ_TRACE_H
