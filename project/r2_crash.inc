/* Resistance port: crash and hang reports for play-testing.
 *
 * A crash (an exception nobody handles) writes logs\crash\R2_<time>_crash.dmp
 * and a one-line summary to logs\crash\crashes.txt, then lets the process die
 * as before. A hang (the game's frame loop, r2_game_frames, does not advance
 * for R2_HANG_S seconds, default 90 -- longer than any level load) writes
 * R2_<time>_hang.dmp once per stall and keeps running. The dumps hold thread
 * stacks and the memory they point at, not the 4 GB guest space, so they stay
 * small. R2_CRASH_DUMPS=0 turns both off; R2_CRASH_DUMPS=full writes full
 * memory dumps (several GB). */
#ifdef _WIN32
#include <dbghelp.h>
#include <time.h>

extern "C" volatile LONG r2_game_frames = 0;   /* stubs.cpp frame limiter */

static char s_crash_dir[MAX_PATH];
static int  s_crash_full = 0;
static volatile LONG s_crash_busy = 0;

static void r2_crash_path(char* out, size_t n, const char* kind)
{
    time_t t = time(NULL);
    struct tm tmv;
    localtime_s(&tmv, &t);
    snprintf(out, n, "%s\\R2_%04d%02d%02d_%02d%02d%02d_%s.dmp", s_crash_dir, tmv.tm_year + 1900,
             tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec, kind);
}

static int r2_write_dump(const char* path, EXCEPTION_POINTERS* ep)
{
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    MINIDUMP_EXCEPTION_INFORMATION mei = { GetCurrentThreadId(), ep, FALSE };
    const MINIDUMP_TYPE type = s_crash_full
        ? (MINIDUMP_TYPE)(MiniDumpWithFullMemory | MiniDumpWithThreadInfo | MiniDumpWithHandleData)
        : (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory |
                          MiniDumpScanMemory | MiniDumpWithUnloadedModules);
    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f, type,
                                      ep ? &mei : NULL, NULL, NULL);
    CloseHandle(f);
    return ok != FALSE;
}

static void r2_crash_note(const char* line)
{
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s\\crashes.txt", s_crash_dir);
    if (FILE* f = fopen(p, "a")) { fputs(line, f); fclose(f); }
    fputs(line, stderr);
}

static LONG WINAPI r2_crash_filter(EXCEPTION_POINTERS* ep)
{
    if (InterlockedExchange(&s_crash_busy, 1)) return EXCEPTION_CONTINUE_SEARCH;
    char path[MAX_PATH];
    r2_crash_path(path, sizeof path, "crash");
    const ULONG_PTR addr = (ULONG_PTR)ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod = NULL;
    char mname[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &mod) && mod) {
        GetModuleFileNameA(mod, mname, sizeof mname);
    }
    const char* base = strrchr(mname, '\\');
    const int ok = r2_write_dump(path, ep);
    char line[1024];
    snprintf(line, sizeof line, "[crash] code 0x%08lX at %s+0x%llX (thread %lu), frame %ld -> %s%s\n",
             ep->ExceptionRecord->ExceptionCode, base ? base + 1 : mname,
             (unsigned long long)(addr - (ULONG_PTR)mod), GetCurrentThreadId(), r2_game_frames,
             ok ? path : "dump FAILED", "");
    r2_crash_note(line);
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI r2_hang_watch(LPVOID)
{
    int limit_s = 90;
    if (const char* e = getenv("R2_HANG_S")) { const int v = atoi(e); if (v >= 10) limit_s = v; }
    LONG last = r2_game_frames;
    ULONGLONG since = GetTickCount64();
    int reported = 0;
    for (;;) {
        Sleep(1000);
        const LONG now = r2_game_frames;
        if (now != last) { last = now; since = GetTickCount64(); reported = 0; continue; }
        /* not counting until the game loop has started (boot, intro videos) */
        if (now == 0 || reported) continue;
        if (GetTickCount64() - since < (ULONGLONG)limit_s * 1000ull) continue;
        reported = 1;
        char path[MAX_PATH], line[1024];
        r2_crash_path(path, sizeof path, "hang");
        const int ok = r2_write_dump(path, NULL);
        snprintf(line, sizeof line, "[hang] no game frame for %d s (frame %ld) -> %s\n", limit_s, now,
                 ok ? path : "dump FAILED");
        r2_crash_note(line);
    }
    return 0;
}

static void r2_crash_init(void)
{
    const char* e = getenv("R2_CRASH_DUMPS");
    if (e && *e == '0') return;
    s_crash_full = e && !_stricmp(e, "full");
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof exe);
    if (char* s = strrchr(exe, '\\')) *s = 0;
    snprintf(s_crash_dir, sizeof s_crash_dir, "%s\\..\\logs", exe);
    CreateDirectoryA(s_crash_dir, NULL);
    strncat(s_crash_dir, "\\crash", sizeof s_crash_dir - strlen(s_crash_dir) - 1);
    CreateDirectoryA(s_crash_dir, NULL);
    SetUnhandledExceptionFilter(r2_crash_filter);
    CloseHandle(CreateThread(NULL, 64 * 1024, r2_hang_watch, NULL, 0, NULL));
}
#else
static void r2_crash_init(void) {}
#endif
