/* ---- FP1: the two-thread RSX (RSX_PERF_MT) -------------------------------
 * Included by rsx_draw_engine.c right after ENG_SCOPE.
 *
 * RPCS3 splits FIFO fetch/decode from the renderer (rsx::thread with
 * multithreaded_rsx and the dma_manager offload). Here the FIFO walker keeps
 * decoding (cellGcmSys: semaphores, labels, zcull reports, flips, `get`) and
 * the engine -- register file, draw clause, surface store, texture cache,
 * pipelines, backend recording -- runs on its own thread, fed through a
 * single-producer / single-consumer ring of compact records:
 *   M     one method (method in the header, the argument after it)
 *   RUN   a method run, its words copied out of the FIFO
 *   CALL  an ordered asynchronous API call, arguments copied in
 *   SYNC  an API call whose result the walker needs: it waits until the
 *         engine has run everything before it and the call itself
 * Every point at which the guest may observe the RSX's progress drains the
 * ring before the walker goes on (rsx_draw_engine_mt_drain): label and
 * semaphore writes and SET_REFERENCE (rsx_backend_flush_reads, which already
 * precedes each of them), the flip, blits, read-backs to guest memory and
 * blocking report reads. So by the time a guest can learn that a draw is
 * done, the engine has read that draw's vertex, index and texture bytes,
 * exactly as before -- the ordering RPCS3 relies on too. Guest pages the
 * engine guards (WCB) are serviced on the engine's thread. A call from any
 * other thread runs on the engine's thread once it has caught up with what
 * the walker had queued.
 *
 * The engine thread starts at the first method the walker sends after init,
 * only for a backend that declared itself safe to drive off-thread
 * (rsx_draw_engine_set_mt_capable; the Vulkan backend does). RSX_PERF_MT=0
 * (or RSX_PERF_ALL=0) keeps everything on the walker's thread exactly as
 * before. RSX_PERF_MT_FLIP_DRAIN=0 does not drain at the flip. */
static void eng_mt_run_method(u32 method, u32 arg);
static void eng_mt_run_methods(u32 method, int incr, const u8* be, u32 count);
#ifdef _WIN32
#include <intrin.h>
#define ENG_MT_WORDS (1u << 22)          /* 16 MB of records */
#define ENG_MT_MASK  (ENG_MT_WORDS - 1u)
#define ENG_MT_LEN   0x0FFFFFFFu
enum { ENG_MT_PAD = 0u, ENG_MT_M = 1u, ENG_MT_RUN = 2u, ENG_MT_CALL = 3u, ENG_MT_SYNC = 4u,
       ENG_MT_QUIT = 5u };
static int  eng_mt_wcb_pending(void);
static void eng_mt_engine_thread(unsigned long tid);

static u32* s_mt_ring;
static HANDLE s_mt_th, s_mt_ev;
static volatile LONG s_mt_run;                  /* the engine thread is up */
static volatile DWORD s_mt_ptid, s_mt_ctid;     /* walker (producer), engine (consumer) */
static int s_mt_capable, s_mt_tried, s_mt_lockstep;
static int s_mt_perf = -1, s_mt_flip_perf = -1;
static __declspec(align(64)) volatile LONG64 s_mt_head;   /* records published */
static __declspec(align(64)) volatile LONG64 s_mt_tail;   /* records completed */
static __declspec(align(64)) volatile LONG s_mt_sleeping;
static __declspec(align(64)) u64 s_mt_ph;       /* producer-private head */
static u64 s_mt_ctail;                          /* producer's copy of the tail */
/* A call from a third thread: one slot, run once the engine has completed
 * everything the walker had published when it was posted. */
static SRWLOCK s_mt_flock = SRWLOCK_INIT;
static struct { eng_mt_fn fn; eng_mt_args* x; u64 after; } s_mt_f;
static volatile LONG s_mt_freq;
/* Counters (rsx_draw_engine_mt_stats). */
static __declspec(align(64)) u64 s_mt_n_rec, s_mt_n_sync, s_mt_n_drain, s_mt_n_drain_wait,
                                  s_mt_wait_ticks;
static __declspec(align(64)) volatile u64 s_mt_n_sleep;   /* the engine thread's */
/* RSX_PERF_MT_BATCH: plain method records are published to the engine
 * thread in batches (every ENG_MT_BATCH words, at each draw END and at every
 * other record), not one store to the shared head per method. Anything that
 * waits on the engine publishes first. */
#define ENG_MT_BATCH 256u
static int s_mt_batch_perf = -1, s_mt_batch;
static u64 s_mt_pub;                            /* producer: last published head */

static int eng_mt_on(void) { return eng_perf_on("RSX_PERF_MT", &s_mt_perf); }

static void eng_mt_pause(u32* spins)
{
    if (++*spins < 256u) _mm_pause();
    else if (*spins < 200000u) SwitchToThread();
    else Sleep(0);
}

static void eng_mt_wake(void)
{
    MemoryBarrier();
    if (s_mt_sleeping) SetEvent(s_mt_ev);
}

static void eng_mt_publish(void)
{
    if (s_mt_pub == s_mt_ph) return;
    _ReadWriteBarrier();
    s_mt_head = (LONG64)s_mt_ph;     /* x86: stores stay in order */
    s_mt_pub = s_mt_ph;
    if (s_mt_sleeping) SetEvent(s_mt_ev);
}

static void eng_mt_wait_room(u64 end)
{
    if (end - s_mt_ctail <= ENG_MT_WORDS) return;
    eng_mt_publish();
    u32 spins = 0;
    for (;;) {
        s_mt_ctail = (u64)s_mt_tail;
        if (end - s_mt_ctail <= ENG_MT_WORDS) return;
        eng_mt_wake();
        eng_mt_pause(&spins);
    }
}

/* Room for an n-word record, contiguous: a record never wraps, the rest of
 * the ring is padded instead. */
static u32* eng_mt_reserve(u32 n)
{
    u32 idx = (u32)(s_mt_ph & ENG_MT_MASK);
    if (idx + n > ENG_MT_WORDS) {
        const u32 pad = ENG_MT_WORDS - idx;
        eng_mt_wait_room(s_mt_ph + pad);
        s_mt_ring[idx] = (ENG_MT_PAD << 28) | pad;
        s_mt_ph += pad;
        idx = 0;
    }
    eng_mt_wait_room(s_mt_ph + n);
    return s_mt_ring + idx;
}

static void eng_mt_commit(u32 n)
{
    s_mt_ph += n;
    s_mt_n_rec++;
    eng_mt_publish();
}

/* A plain method record: published once a batch has built up. */
static void eng_mt_commit_lazy(u32 n)
{
    s_mt_ph += n;
    s_mt_n_rec++;
    if (s_mt_ph - s_mt_pub >= ENG_MT_BATCH) eng_mt_publish();
}

static void eng_mt_wait_done(u64 pos)
{
    eng_mt_publish();
    if ((u64)s_mt_tail >= pos) return;
    const u64 t0 = eng_prof_now();
    eng_mt_wake();
    u32 spins = 0;
    while ((u64)s_mt_tail < pos) eng_mt_pause(&spins);
    _ReadWriteBarrier();
    s_mt_n_drain_wait++;
    s_mt_wait_ticks += eng_prof_now() - t0;
}

static void eng_mt_foreign_run(u64 t)
{
    if (!s_mt_freq || t < s_mt_f.after) return;
    s_mt_f.fn(s_mt_f.x);
    InterlockedExchange(&s_mt_freq, 0);
}

static void eng_mt_foreign_call(eng_mt_fn fn, eng_mt_args* x)
{
    AcquireSRWLockExclusive(&s_mt_flock);
    if (!s_mt_run) { ReleaseSRWLockExclusive(&s_mt_flock); fn(x); return; }
    s_mt_f.fn = fn;
    s_mt_f.x = x;
    s_mt_f.after = (u64)s_mt_head;
    InterlockedExchange(&s_mt_freq, 1);
    eng_mt_wake();
    u32 spins = 0;
    while (s_mt_freq) eng_mt_pause(&spins);
    ReleaseSRWLockExclusive(&s_mt_flock);
}

static DWORD WINAPI eng_mt_main(LPVOID unused)
{
    (void)unused;
    eng_mt_engine_thread(GetCurrentThreadId());
    s_mt_ctid = GetCurrentThreadId();
    u64 t = (u64)s_mt_tail;
    for (;;) {
        const u64 h = (u64)s_mt_head;
        _ReadWriteBarrier();
        if (t == h) {
            if (eng_mt_wcb_pending()) rsx_draw_engine_wcb_service();
            eng_mt_foreign_run(t);
            u32 spins = 0;
            while ((u64)s_mt_head == t && !s_mt_freq && !eng_mt_wcb_pending() && spins < 4000u) {
                _mm_pause();
                spins++;
            }
            if ((u64)s_mt_head != t || s_mt_freq || eng_mt_wcb_pending()) continue;
            InterlockedExchange(&s_mt_sleeping, 1);
            if ((u64)s_mt_head == t && !s_mt_freq && !eng_mt_wcb_pending()) {
                s_mt_n_sleep++;
                WaitForSingleObject(s_mt_ev, 1);
            }
            InterlockedExchange(&s_mt_sleeping, 0);
            continue;
        }
        s_eng_depth++;
        while (t != h) {
            const u32* r = s_mt_ring + (t & ENG_MT_MASK);
            const u32 hdr = r[0];
            u32 len = hdr & ENG_MT_LEN;
            switch (hdr >> 28) {
            case ENG_MT_M:
                len = 2;
                eng_mt_run_method(hdr & ENG_MT_LEN, r[1]);
                break;
            case ENG_MT_RUN:
                eng_mt_run_methods(r[1], (int)r[2], (const u8*)(r + 3), len - 3u);
                break;
            case ENG_MT_CALL: {
                eng_mt_fn fn;
                eng_mt_args x;
                memcpy(&fn, r + 1, sizeof fn);
                memcpy(&x, r + 3, sizeof x);
                fn(&x);
                break;
            }
            case ENG_MT_SYNC: {
                eng_mt_fn fn;
                eng_mt_args* x;
                memcpy(&fn, r + 1, sizeof fn);
                memcpy(&x, r + 3, sizeof x);
                fn(x);
                break;
            }
            case ENG_MT_QUIT:
                s_eng_depth--;
                eng_mt_foreign_run(~0ull);
                _ReadWriteBarrier();
                s_mt_tail = (LONG64)(t + 1u);
                return 0;
            default:
                break;   /* padding */
            }
            t += len;
            _ReadWriteBarrier();
            s_mt_tail = (LONG64)t;
            if (s_mt_freq || eng_mt_wcb_pending()) {
                s_eng_depth--;
                if (eng_mt_wcb_pending()) rsx_draw_engine_wcb_service();
                eng_mt_foreign_run(t);
                s_eng_depth++;
            }
        }
        s_eng_depth--;
    }
}

/* PortFrontEnd.exe's menus, and the in-game pause menu, reuse their vertex
 * ring without a fence (cellGcm_wait_fifo_caught_up covers them by making the
 * guest wait for the walker): there the walker waits for each draw's END. */
extern volatile int g_port_pause_ui;
volatile int eng_mt_pause_ui_none = 0;
#if defined(_M_IX86)
#pragma comment(linker, "/alternatename:_g_port_pause_ui=_eng_mt_pause_ui_none")
#else
#pragma comment(linker, "/alternatename:g_port_pause_ui=eng_mt_pause_ui_none")
#endif
static int eng_mt_frontend(void)
{
    char mp[MAX_PATH] = "";
    GetModuleFileNameA(NULL, mp, MAX_PATH);
    const char* b = strrchr(mp, '\\');
    b = b ? b + 1 : mp;
    return _stricmp(b, "PortFrontEnd.exe") == 0;
}

static void eng_mt_start(void)
{
    s_mt_tried = 1;
    if (!s_mt_capable || !eng_mt_on()) return;
    s_mt_lockstep = eng_mt_frontend();
    if (!s_mt_ring) {
        s_mt_ring = (u32*)VirtualAlloc(NULL, (SIZE_T)ENG_MT_WORDS * 4u, MEM_RESERVE | MEM_COMMIT,
                                       PAGE_READWRITE);
        if (!s_mt_ring) return;
    }
    if (!s_mt_ev) s_mt_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!s_mt_ev) return;
    s_mt_head = s_mt_tail = 0;
    s_mt_ph = s_mt_ctail = s_mt_pub = 0;
    s_mt_batch = eng_perf_on("RSX_PERF_MT_BATCH", &s_mt_batch_perf);
    s_mt_sleeping = 0;
    s_mt_ptid = GetCurrentThreadId();
    s_mt_ctid = 0;
    s_mt_th = CreateThread(NULL, 1u << 20, eng_mt_main, NULL, 0, NULL);
    if (!s_mt_th) return;
    SetThreadPriority(s_mt_th, GetThreadPriority(GetCurrentThread()));
    while (!s_mt_ctid) SwitchToThread();
    s_mt_run = 1;
    s_eng_mt_live = 1;
    fprintf(stderr, "[rsx engine] FP1: engine thread up (walker %lu -> engine %lu)%s\n",
            (unsigned long)s_mt_ptid, (unsigned long)s_mt_ctid,
            s_mt_lockstep ? ", front end: per-draw lockstep" : "");
}

void rsx_draw_engine_mt_stop(void)
{
    if (!s_mt_run) { s_mt_tried = 0; return; }
    if (GetCurrentThreadId() == s_mt_ctid) return;
    u32* w = eng_mt_reserve(1);
    w[0] = (ENG_MT_QUIT << 28) | 1u;
    eng_mt_commit(1);
    eng_mt_wake();
    WaitForSingleObject(s_mt_th, INFINITE);
    CloseHandle(s_mt_th);
    s_mt_th = NULL;
    AcquireSRWLockExclusive(&s_mt_flock);
    s_mt_run = 0;
    s_eng_mt_live = 0;
    ReleaseSRWLockExclusive(&s_mt_flock);
    s_mt_ptid = s_mt_ctid = 0;
    s_mt_tried = 0;
    eng_mt_engine_thread(0);
}

/* 0: run here (MT off, or this is the engine's thread), 1: the walker,
 * queue it, 2: another thread, hand it over. */
static int eng_mt_route(void)
{
    if (!s_mt_run) return 0;
    const DWORD me = GetCurrentThreadId();
    if (me == s_mt_ctid) return 0;
    return me == s_mt_ptid ? 1 : 2;
}

static void eng_mt_sync_call(eng_mt_fn fn, eng_mt_args* x)
{
    u32* w = eng_mt_reserve(5);
    w[0] = (ENG_MT_SYNC << 28) | 5u;
    memcpy(w + 1, &fn, sizeof fn);
    memcpy(w + 3, &x, sizeof x);
    eng_mt_commit(5);
    s_mt_n_sync++;
    eng_mt_wait_done(s_mt_ph);
}

static int eng_mt_fwd(int sync, eng_mt_fn fn, eng_mt_args* x)
{
    const int r = eng_mt_route();
    if (!r) return 0;
    if (r == 2) { eng_mt_foreign_call(fn, x); return 1; }
    if (sync) { eng_mt_sync_call(fn, x); return 1; }
    const u32 n = 3u + (u32)((sizeof *x + 3u) / 4u);
    u32* w = eng_mt_reserve(n);
    w[0] = (ENG_MT_CALL << 28) | n;
    memcpy(w + 1, &fn, sizeof fn);
    memcpy(w + 3, x, sizeof *x);
    eng_mt_commit(n);
    return 1;
}

static void eng_mt_nop(eng_mt_args* x) { (void)x; }

void rsx_draw_engine_mt_drain(void)
{
    const int r = eng_mt_route();
    if (!r) return;
    s_mt_n_drain++;
    if (r == 2) { eng_mt_args x; eng_mt_foreign_call(eng_mt_nop, &x); return; }
    eng_mt_wait_done(s_mt_ph);
}

static void eng_mt_method_thunk(eng_mt_args* x) { eng_mt_run_method(x->a[0], x->a[1]); }

/* The walker's method: queue it (starting the engine thread on the first
 * one). Returns 0 when the caller runs it in place. */
static int eng_mt_method(u32 method, u32 arg)
{
    if (!s_mt_run) {
        if (s_mt_tried || !g.ready) return 0;
        eng_mt_start();
        if (!s_mt_run) return 0;
    }
    const DWORD me = GetCurrentThreadId();
    if (me != s_mt_ptid) {
        if (me == s_mt_ctid) return 0;
        eng_mt_args x;
        x.a[0] = method; x.a[1] = arg;
        eng_mt_foreign_call(eng_mt_method_thunk, &x);
        return 1;
    }
    u32* w = eng_mt_reserve(2);
    w[0] = (ENG_MT_M << 28) | (method & ENG_MT_LEN);
    w[1] = arg;
    const u32 m = method & 0xFFFCu;
    if (s_mt_batch && (method & 0x1FFCu) != 0x1808u && (method & 0x1FFCu) != 0x1D94u &&
        m < 0xE000u) eng_mt_commit_lazy(2);
    else eng_mt_commit(2);
    if (m == 0xFEACu || m == 0xE944u || m == 0xE920u || m == 0xE924u) {
        if (eng_perf_on("RSX_PERF_MT_FLIP_DRAIN", &s_mt_flip_perf)) rsx_draw_engine_mt_drain();
    } else if ((method & 0x1FFCu) == 0x1808u && !(arg & 0xFFu) &&
               (s_mt_lockstep || g_port_pause_ui)) {
        rsx_draw_engine_mt_drain();
    }
    return 1;
}

static void eng_mt_methods_thunk(eng_mt_args* x)
{
    const u8* be;
    memcpy(&be, &x->p, sizeof be);
    eng_mt_run_methods(x->a[0], (int)x->a[1], be, x->a[2]);
}

static int eng_mt_methods(u32 method, int incr, const u8* be, u32 count)
{
    if (!s_mt_run) return 0;
    const DWORD me = GetCurrentThreadId();
    if (me != s_mt_ptid) {
        if (me == s_mt_ctid) return 0;
        eng_mt_args x;
        x.a[0] = method; x.a[1] = (u32)incr; x.a[2] = count;
        x.p = (void*)be;
        eng_mt_foreign_call(eng_mt_methods_thunk, &x);
        return 1;
    }
    if (count > ENG_MT_WORDS / 4u) {
        /* A run too long for one record: in pieces. */
        for (u32 i = 0; i < count;) {
            const u32 k = count - i < ENG_MT_WORDS / 4u ? count - i : ENG_MT_WORDS / 4u;
            eng_mt_methods(incr ? method + i * 4u : method, incr, be + (size_t)i * 4u, k);
            i += k;
        }
        return 1;
    }
    const u32 n = 3u + count;
    u32* w = eng_mt_reserve(n);
    w[0] = (ENG_MT_RUN << 28) | n;
    w[1] = method;
    w[2] = (u32)incr;
    memcpy(w + 3, be, (size_t)count * 4u);
    if (s_mt_batch && method < 0xE000u) eng_mt_commit_lazy(n);
    else eng_mt_commit(n);
    return 1;
}
#else
static int eng_mt_fwd(int sync, eng_mt_fn fn, eng_mt_args* x) { (void)sync; (void)fn; (void)x; return 0; }
void rsx_draw_engine_mt_stop(void) {}
void rsx_draw_engine_mt_drain(void) {}
#endif

void rsx_draw_engine_set_mt_capable(int on)
{
#ifdef _WIN32
    s_mt_capable = on != 0;
#else
    (void)on;
#endif
}

int rsx_draw_engine_mt_active(void)
{
#ifdef _WIN32
    return s_mt_run != 0;
#else
    return 0;
#endif
}

void rsx_draw_engine_mt_stats(u64 out[6])
{
#ifdef _WIN32
    out[0] = s_mt_n_rec; out[1] = s_mt_n_sync; out[2] = s_mt_n_drain;
    out[3] = s_mt_n_drain_wait; out[4] = s_mt_wait_ticks; out[5] = s_mt_n_sleep;
#else
    memset(out, 0, 6 * sizeof out[0]);
#endif
}

/* Hand a host call (the Vulkan backend's present) to the engine's thread, in
 * order with the walker's methods; wait: until it has run. Returns 0 when
 * the caller must run it itself (MT off, or already on the engine's thread). */
static void eng_mt_host_thunk(eng_mt_args* x)
{
    void (*fn)(void*);
    memcpy(&fn, x->a, sizeof fn);
    fn(x->p);
}
int rsx_draw_engine_mt_offload(void (*fn)(void*), void* user, int wait)
{
    eng_mt_args x;
    memset(&x, 0, sizeof x);
    memcpy(x.a, &fn, sizeof fn);
    x.p = user;
    return eng_mt_fwd(wait, eng_mt_host_thunk, &x);
}
