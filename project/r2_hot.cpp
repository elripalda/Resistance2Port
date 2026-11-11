// Resistance 2: native replacements for the game's generic libc routines.
//
// The lifted versions run every byte/word through the guest-memory accessors
// (bounds check, coherence-bitmap probe, byte swap each); these do the same
// work on the host copy of guest memory directly. Guest memory is raw
// big-endian bytes at vm_base + ea, so a byte copy, fill or compare needs no
// swapping at all.
//
// Each lifted function starts with `if (r2_hot_xxx(ctx)) return;` (hooks in
// tools/hooks/r2.json). The native routine handles the call only in the
// "plain" domain and returns 0 otherwise, so the original lifted body runs:
//   - every byte touched, widened to the aligned 8-byte blocks the lifted
//     word-at-a-time loops may over-read, lies in [0x10000, 0xDFFFFFF8) (the
//     lifted accessors' inline range) and outside the gcm poll-word block;
//   - no destination line is reserved by an SPU (g_spu_coh_bitmap_p): such a
//     store must take the out-of-line path that raises the lost-reservation
//     event;
//   - source and destination do not overlap (memcpy/strcpy: the lifted copy
//     picks its direction from the low 12 address bits, not from overlap);
//   - lengths below 2 GiB; the word-at-a-time TOC constant is 0x7F..7F.
// Equivalence is at the ABI level: memory, r3 and every non-volatile register
// (r1, r2, r13-r31, lr, f14-f31, v20-v31, cr2-cr4) match the lifted code.
// The scratch registers the lifted body would also have changed (r0, r4-r12,
// ctr, xer, cr0/1/5-7) are left alone -- no caller may depend on them.
// scratch/hot/difftest proves it on random and edge-case inputs.
//
//   R2_HOT_NATIVE=0           all off (the lifted code runs, as before)
//   R2_HOT_MEMCPY=0  R2_HOT_MEMSET=0  R2_HOT_STRCMP=0  R2_HOT_STRLEN=0
//   R2_HOT_STRCPY=0  R2_HOT_MEMCMP=0  R2_HOT_GSTRCMP=0   one routine off
//   R2_HOT_LOG=0              no [hot] lines (default: one block every ~10 s)
//   R2_HOT_SAMPLE=<n>         time 1 call in n per thread (default 4096): the
//                             lifted body, then the native one on the same
//                             inputs (every replacement is idempotent), and
//                             check r3 agrees -- [hot] reports the estimated
//                             time saved and any mismatch. 0 = no sampling.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <atomic>
#include "ppu_recomp.h"

/* lifted functions come from ppu_recomp.h (C++ linkage) */
void func_00A56E30(ppu_context* ctx);   /* memcpy */
void func_00A63258(ppu_context* ctx);   /* memset */
void func_00A59DB0(ppu_context* ctx);   /* strcmp */
void func_00A5A310(ppu_context* ctx);   /* strlen */
void func_00A5A180(ppu_context* ctx);   /* strcpy */
void func_00A63330(ppu_context* ctx);   /* memcmp */
void func_006BB908(ppu_context* ctx);   /* game strcmp: signed chars, returns the difference */

namespace {

enum { H_MEMCPY, H_MEMSET, H_STRCMP, H_STRLEN, H_STRCPY, H_MEMCMP, H_GSTRCMP, H_N };
const char* const k_name[H_N] = { "memcpy", "memset", "strcmp", "strlen", "strcpy", "memcmp", "gstrcmp" };
const char* const k_env[H_N] = { "R2_HOT_MEMCPY", "R2_HOT_MEMSET", "R2_HOT_STRCMP", "R2_HOT_STRLEN",
                                 "R2_HOT_STRCPY", "R2_HOT_MEMCMP", "R2_HOT_GSTRCMP" };

/* ---- the domain ---- */
constexpr uint64_t kLo = 0x10000u, kHi = 0xDFFFFFF8u;
constexpr uint64_t kPollLo = 0x20002000u, kPollHi = 0x20002010u;

/* bytes [ea, ea+n), widened to whole 8-byte blocks, all in the inline range */
inline bool span_ok(uint32_t ea, uint64_t n)
{
    const uint64_t a = ea & ~7ull, b = ((uint64_t)ea + n + 7) & ~7ull;
    return a >= kLo && b <= kHi && !(a < kPollHi && b > kPollLo);
}
inline bool blk_ok(uint32_t ea) { return span_ok(ea, 1); }

/* no SPU-reserved 128-byte line in [ea, ea+n) */
inline bool coh_ok(uint32_t ea, uint64_t n)
{
    if (!g_spu_coh_armed || !n) return true;
    const unsigned char* bm = g_spu_coh_bitmap_p;
    const uint32_t l1 = (uint32_t)(((uint64_t)ea + n - 1) >> 7);
    for (uint32_t l = ea >> 7; l <= l1; l++) {
        if (!(l & 7) && l + 7 <= l1) { if (bm[l >> 3]) return false; l += 7; continue; }
        if ((bm[l >> 3] >> (l & 7)) & 1u) return false;
    }
    return true;
}

/* widened ranges intersect */
inline bool overlap(uint32_t a, uint64_t na, uint32_t b, uint64_t nb)
{
    const uint64_t a0 = a & ~7ull, a1 = ((uint64_t)a + na + 7) & ~7ull;
    const uint64_t b0 = b & ~7ull, b1 = ((uint64_t)b + nb + 7) & ~7ull;
    return a0 < b1 && b0 < a1;
}

inline uint64_t ld_be64(uint32_t ea) { uint64_t v; memcpy(&v, vm_base + ea, 8); return _byteswap_uint64(v); }

/* the word-at-a-time zero-byte constant the lifted strlen/strcmp/strcpy load
 * from the TOC: if it is not 0x7F7F...7F they are not the routines we know */
inline bool toc_ok(uint64_t r2, uint32_t off)
{
    const uint32_t ea = (uint32_t)(r2 - off);
    return span_ok(ea, 8) && !(ea & 7) && ld_be64(ea) == 0x7F7F7F7F7F7F7F7Full;
}

inline bool has_zero(uint64_t w) { return ((w - 0x0101010101010101ull) & ~w & 0x8080808080808080ull) != 0; }

/* length of the NUL-terminated string at s, staying in the inline range;
 * -1 if it runs out of it */
inline int64_t bounded_strlen(uint32_t s)
{
    if (s < kLo || s >= kHi) return -1;
    const uint64_t lim = kHi - s;
    const size_t len = strnlen((const char*)vm_base + s, (size_t)lim);
    if (len >= lim) return -1;
    if (!span_ok(s, (uint64_t)len + 1)) return -1;
    return (int64_t)len;
}

/* ---- the routines: 1 = done (ctx updated), 0 = run the lifted body ---- */
int nat_memcpy(ppu_context* c, uint64_t* bytes)
{
    const uint64_t n = c->gpr[5];
    if (n >= 0x80000000ull) return 0;
    const uint32_t d = (uint32_t)c->gpr[3], s = (uint32_t)c->gpr[4];
    if (n) {
        if (!span_ok(s, n) || !span_ok(d, n) || overlap(d, n, s, n) || !coh_ok(d, n)) return 0;
        memcpy(vm_base + d, vm_base + s, (size_t)n);
    }
    c->gpr[3] = d;
    *bytes = n;
    return 1;
}

int nat_memset(ppu_context* c, uint64_t* bytes)
{
    const uint64_t n = c->gpr[5], r3 = c->gpr[3];
    if (n >= 0x80000000ull || (r3 >> 32)) return 0;
    const uint32_t d = (uint32_t)r3;
    if (n) {
        if (!span_ok(d, n) || !coh_ok(d, n)) return 0;
        memset(vm_base + d, (int)(uint8_t)c->gpr[4], (size_t)n);
    }
    *bytes = n;
    return 1;
}

int nat_strlen(ppu_context* c, uint64_t* bytes)
{
    const uint64_t r3 = c->gpr[3];
    if ((r3 >> 32) || !toc_ok(c->gpr[2], 0x1250)) return 0;
    const int64_t len = bounded_strlen((uint32_t)r3);
    if (len < 0) return 0;
    c->gpr[3] = (uint64_t)len;
    *bytes = (uint64_t)len;
    return 1;
}

int nat_strcpy(ppu_context* c, uint64_t* bytes)
{
    if (!toc_ok(c->gpr[2], 0x1258)) return 0;
    const uint32_t d = (uint32_t)c->gpr[3], s = (uint32_t)c->gpr[4];
    const int64_t len = bounded_strlen(s);
    if (len < 0) return 0;
    const uint64_t n = (uint64_t)len + 1;
    if (!span_ok(d, n) || overlap(d, n, s, n) || !coh_ok(d, n)) return 0;
    memcpy(vm_base + d, vm_base + s, (size_t)n);
    c->gpr[3] = d;
    *bytes = n;
    return 1;
}

int nat_memcmp(ppu_context* c, uint64_t* bytes)
{
    const uint32_t n = (uint32_t)c->gpr[5];
    if (n >= 0x80000000u) return 0;
    const uint32_t a = (uint32_t)c->gpr[3], b = (uint32_t)c->gpr[4];
    int r = 0;
    if (n) {
        if (!span_ok(a, n) || !span_ok(b, n)) return 0;
        r = memcmp(vm_base + a, vm_base + b, n);
    }
    c->gpr[3] = (uint64_t)(int64_t)(r < 0 ? -1 : r > 0 ? 1 : 0);
    *bytes = n;
    return 1;
}

/* unsigned-char strcmp returning -1/0/1 */
int nat_strcmp(ppu_context* c, uint64_t* bytes)
{
    if (!toc_ok(c->gpr[2], 0x1268)) return 0;
    const uint32_t a = (uint32_t)c->gpr[3], b = (uint32_t)c->gpr[4];
    const uint8_t* m = vm_base;
    int r = 0;
    for (uint32_t i = 0;; ) {
        const uint32_t pa = a + i, pb = b + i;
        if ((i == 0 || !(pa & 7)) && !blk_ok(pa)) return 0;
        if ((i == 0 || !(pb & 7)) && !blk_ok(pb)) return 0;
        if (!((pa | pb) & 7)) {
            uint64_t wa, wb; memcpy(&wa, m + pa, 8); memcpy(&wb, m + pb, 8);
            if (wa == wb && !has_zero(wa)) { i += 8; continue; }
        }
        const uint8_t x = m[pa], y = m[pb];
        if (x != y) { r = x < y ? -1 : 1; *bytes = i; break; }
        if (!x) { *bytes = i; break; }
        i++;
    }
    c->gpr[3] = (uint64_t)(int64_t)r;
    return 1;
}

/* func_006BB908: signed chars; returns -s2[i] at the end of s1, s1[i] at the
 * end of s2, else s1[i] - s2[i] at the first difference (sign-extended) */
int nat_gstrcmp(ppu_context* c, uint64_t* bytes)
{
    const uint32_t a = (uint32_t)c->gpr[3], b = (uint32_t)c->gpr[4];
    const uint8_t* m = vm_base;
    int32_t r;
    for (uint32_t i = 0;; i++) {
        const uint32_t pa = a + i, pb = b + i;
        if ((i == 0 || !(pa & 7)) && !blk_ok(pa)) return 0;
        if ((i == 0 || !(pb & 7)) && !blk_ok(pb)) return 0;
        const int32_t x = (int8_t)m[pa], y = (int8_t)m[pb];
        if (x == 0) { r = -y; *bytes = i; break; }
        if (y == 0) { r = x; *bytes = i; break; }
        if (x != y) { r = x - y; *bytes = i; break; }
    }
    c->gpr[3] = (uint64_t)(int64_t)r;
    return 1;
}

/* ---- switches and stats ---- */
int env_int(const char* n, int def)
{
    const char* e = getenv(n);
    if (!e || !*e) return def;
    if (!_stricmp(e, "off") || !_stricmp(e, "false") || !_stricmp(e, "no")) return 0;
    if (!_stricmp(e, "on") || !_stricmp(e, "true") || !_stricmp(e, "yes")) return 1;
    return atoi(e);
}

std::atomic<int> g_init{0};
unsigned g_on = 0;           /* bit per routine */
unsigned g_sample = 4096;
int g_log = 1;

struct Stat {
    std::atomic<long long> calls{0}, native{0}, bytes{0}, samples{0}, cyc_orig{0}, cyc_nat{0}, mismatch{0};
};
Stat g_st[H_N];
std::atomic<unsigned long long> g_next_log{0};
long long g_qpc0, g_qpf; unsigned long long g_tsc0;

struct Tls {
    unsigned calls[H_N], native[H_N], samp[H_N];
    unsigned long long bytes[H_N];
    unsigned tick;
    int bypass;
};
thread_local Tls t_tls;

void init_once()
{
    int z = 0;
    if (!g_init.compare_exchange_strong(z, 1)) { while (g_init.load() != 2) _mm_pause(); return; }
    unsigned on = 0;
    if (env_int("R2_HOT_NATIVE", 0))
        for (int i = 0; i < H_N; i++) if (env_int(k_env[i], 1)) on |= 1u << i;
    const int s = env_int("R2_HOT_SAMPLE", 4096);
    g_sample = s <= 0 ? 0 : (unsigned)s;
    g_log = env_int("R2_HOT_LOG", 1);
    LARGE_INTEGER f, q; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&q);
    g_qpf = f.QuadPart; g_qpc0 = q.QuadPart; g_tsc0 = __rdtsc();
    g_next_log.store(GetTickCount64() + 10000);
    g_on = on;
    fprintf(stderr, "[hot] native routines:");
    for (int i = 0; i < H_N; i++) fprintf(stderr, " %s=%u", k_name[i], (on >> i) & 1u);
    fprintf(stderr, " (R2_HOT_NATIVE=0: all lifted) sample 1/%u\n", g_sample);
    g_init.store(2, std::memory_order_release);
}

inline bool enabled(int h)
{
    if (__builtin_expect(g_init.load(std::memory_order_acquire) != 2, 0)) init_once();
    return (g_on >> h) & 1u;
}

void report()
{
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    const double secs = (double)(q.QuadPart - g_qpc0) / (double)g_qpf;
    const double tsc_per_ms = secs > 0 ? (double)(__rdtsc() - g_tsc0) / (secs * 1000.0) : 3.0e6;
    static long long l_calls[H_N], l_nat[H_N], l_bytes[H_N];
    double saved_total = 0;
    for (int h = 0; h < H_N; h++) {
        Stat& s = g_st[h];
        const long long c = s.calls.load(), n = s.native.load(), b = s.bytes.load();
        const long long dc = c - l_calls[h], dn = n - l_nat[h], db = b - l_bytes[h];
        l_calls[h] = c; l_nat[h] = n; l_bytes[h] = b;
        if (!dc) continue;
        const long long ns = s.samples.load();
        const double co = ns ? (double)s.cyc_orig.load() / ns : 0, cn = ns ? (double)s.cyc_nat.load() / ns : 0;
        const double saved_ms = ns ? (double)dn * (co - cn) / tsc_per_ms : 0;
        saved_total += saved_ms;
        fprintf(stderr, "[hot] %-7s calls=%lld native=%.1f%% KB=%lld | cyc/call lifted=%.0f native=%.0f (n=%lld) "
                        "saved~%.1f ms/10s mismatch=%lld\n",
                k_name[h], dc, 100.0 * (double)dn / (double)dc, db >> 10, co, cn, ns, saved_ms, s.mismatch.load());
    }
    if (saved_total > 0) fprintf(stderr, "[hot] total saved ~%.1f ms per 10 s across all PPU threads\n", saved_total);
}

void flush(Tls& t)
{
    for (int h = 0; h < H_N; h++) {
        if (!t.calls[h]) continue;
        g_st[h].calls.fetch_add(t.calls[h], std::memory_order_relaxed);
        g_st[h].native.fetch_add(t.native[h], std::memory_order_relaxed);
        g_st[h].bytes.fetch_add((long long)t.bytes[h], std::memory_order_relaxed);
        t.calls[h] = t.native[h] = 0; t.bytes[h] = 0;
    }
    if (!g_log) return;
    const unsigned long long now = GetTickCount64();
    unsigned long long nx = g_next_log.load(std::memory_order_relaxed);
    if (now >= nx && g_next_log.compare_exchange_strong(nx, now + 10000)) report();
}

typedef int (*nat_fn)(ppu_context*, uint64_t*);

int run(int h, ppu_context* c, nat_fn nat, void (*lifted)(ppu_context*))
{
    Tls& t = t_tls;
    if (t.bypass) return 0;
    t.calls[h]++;
    if ((++t.tick & 1023u) == 0) flush(t);
    uint64_t bytes = 0;
    if (g_sample && ++t.samp[h] >= g_sample) {
        t.samp[h] = 0;
        ppu_context in = *c;
        ppu_context tmp = in;
        if (!nat(&tmp, &bytes)) return 0;          /* outside the domain: the lifted body runs */
        /* time the lifted body on the real ctx, then the native one again on
         * a copy of the inputs (memcpy/memset/strcpy do not overlap here, so
         * redoing them is a no-op) */
        const unsigned long long t0 = __rdtsc();
        t.bypass = 1; lifted(c); t.bypass = 0;
        const unsigned long long t1 = __rdtsc();
        ppu_context n2 = in;
        nat(&n2, &bytes);
        const unsigned long long t2 = __rdtsc();
        Stat& s = g_st[h];
        s.samples.fetch_add(1, std::memory_order_relaxed);
        s.cyc_orig.fetch_add((long long)(t1 - t0), std::memory_order_relaxed);
        s.cyc_nat.fetch_add((long long)(t2 - t1), std::memory_order_relaxed);
        if (n2.gpr[3] != c->gpr[3]) {
            const long long k = s.mismatch.fetch_add(1, std::memory_order_relaxed);
            if (k < 8)
                fprintf(stderr, "[hot] MISMATCH %s r3 lifted=0x%llX native=0x%llX (in r3=0x%llX r4=0x%llX r5=0x%llX)\n",
                        k_name[h], (unsigned long long)c->gpr[3], (unsigned long long)n2.gpr[3],
                        (unsigned long long)in.gpr[3], (unsigned long long)in.gpr[4], (unsigned long long)in.gpr[5]);
        }
        t.native[h]++; t.bytes[h] += bytes;
        return 1;                                   /* the lifted body already ran */
    }
    if (!nat(c, &bytes)) return 0;
    t.native[h]++; t.bytes[h] += bytes;
    return 1;
}

}  // namespace

#define R2_HOT_ENTRY(fn, h, nat, lifted) \
    extern "C" int fn(ppu_context* ctx) { return enabled(h) ? run(h, ctx, nat, lifted) : 0; }
R2_HOT_ENTRY(r2_hot_memcpy,  H_MEMCPY,  nat_memcpy,  func_00A56E30)
R2_HOT_ENTRY(r2_hot_memset,  H_MEMSET,  nat_memset,  func_00A63258)
R2_HOT_ENTRY(r2_hot_strcmp,  H_STRCMP,  nat_strcmp,  func_00A59DB0)
R2_HOT_ENTRY(r2_hot_strlen,  H_STRLEN,  nat_strlen,  func_00A5A310)
R2_HOT_ENTRY(r2_hot_strcpy,  H_STRCPY,  nat_strcpy,  func_00A5A180)
R2_HOT_ENTRY(r2_hot_memcmp,  H_MEMCMP,  nat_memcmp,  func_00A63330)
R2_HOT_ENTRY(r2_hot_gstrcmp, H_GSTRCMP, nat_gstrcmp, func_006BB908)

/* difftest entry: the native routine alone (no switches, stats or sampling) */
extern "C" int r2_hot_native_only(int h, ppu_context* ctx)
{
    static const nat_fn k[H_N] = { nat_memcpy, nat_memset, nat_strcmp, nat_strlen, nat_strcpy, nat_memcmp, nat_gstrcmp };
    uint64_t b = 0;
    return h >= 0 && h < H_N ? k[h](ctx, &b) : 0;
}


/* ---- occlusion-query report wait (func_006483A8) ----
 * At the start of frame N the main thread reads the visibility results of the
 * occlusion queries it issued in frame N-1: it polls the 16-byte RSX report of
 * the LAST query in the set until its timestamp is non-zero (usleep(30) per
 * poll, 1 s timeout), then turns every report into a visible flag. On timeout
 * it keeps the previous flags and does not mark the set as read -- the
 * function's own "no result" path. In the port rsx::thread finishes frame N-1
 * only ~20 ms after the PPU kicked it in heavy scenes (5k draws at ~4 us), so
 * the main thread sleeps here and PPU and RSX run one after the other.
 *   R2_OCC_SKIP=<n>          when the result is not in yet, take the timeout
 *                            path at once, at most n frames in a row per set
 *                            (the n+1-th waits as before): PPU frame N+1 then
 *                            overlaps RSX frame N, and visibility is at most n
 *                            frames older. Default 0 = the original wait every
 *                            frame. 1 = heavy airfield scene 30 -> 45 fps, but
 *                            nearby objects visibly pop in and out. Skipping every
 *                            frame (R2_OCC_WAIT_US=0) leaves the flags stuck:
 *                            objects vanish -- do not.
 *   R2_OCC_WAIT_US=<us>      wait natively at most <us>, then the timeout path
 *                            (unset: the lifted wait, 1 s)
 * [occ] lines (every ~10 s): sets read, ready at once, skipped, waited, timeouts. */
namespace {
int g_occ_init = 0;
long long g_occ_us = -1;
int g_occ_skip = 0;
std::atomic<long long> g_occ_calls{0}, g_occ_ready{0}, g_occ_skipped{0}, g_occ_waited{0}, g_occ_timeouts{0}, g_occ_wait_ns{0};
std::atomic<unsigned long long> g_occ_next{0};
struct OccSet { uint32_t obj; int32_t b; int run; };
OccSet g_occ_sets[16];   /* main thread only */

inline uint32_t ld_be32(uint32_t ea) { uint32_t v; memcpy(&v, vm_base + ea, 4); return _byteswap_ulong(v); }
inline uint16_t ld_be16(uint32_t ea) { uint16_t v; memcpy(&v, vm_base + ea, 2); return _byteswap_ushort(v); }

void occ_report()
{
    static long long lc, lr, ls, lw, lt, lns;
    const long long c = g_occ_calls.load(), r = g_occ_ready.load(), s = g_occ_skipped.load(), w = g_occ_waited.load(),
                    t = g_occ_timeouts.load(), ns = g_occ_wait_ns.load();
    fprintf(stderr, "[occ] report sets %lld: ready %lld, skipped %lld (R2_OCC_SKIP=%d), waited %lld", c - lc, r - lr, s - ls,
            g_occ_skip, w - lw);
    if (g_occ_us >= 0)
        fprintf(stderr, " (%.2f ms/s, %.0f us each, timeouts %lld, R2_OCC_WAIT_US=%lld)", (double)(ns - lns) / 1e7,
                w - lw ? (double)(ns - lns) / 1e3 / (double)(w - lw) : 0.0, t - lt, g_occ_us);
    fprintf(stderr, "\n");
    lc = c; lr = r; ls = s; lw = w; lt = t; lns = ns;
}

OccSet* occ_set(uint32_t obj, int32_t b)
{
    OccSet* lru = &g_occ_sets[0];
    for (OccSet& e : g_occ_sets) {
        if (e.obj == obj && e.b == b) return &e;
        if (!e.obj) { e.obj = obj; e.b = b; e.run = 0; return &e; }
    }
    lru->obj = obj; lru->b = b; lru->run = 0;
    return lru;
}
}  // namespace

extern "C" int r2_occ_wait(ppu_context* ctx)
{
    if (__builtin_expect(!g_occ_init, 0)) {
        const char* e = getenv("R2_OCC_WAIT_US");
        g_occ_us = (e && *e) ? atoll(e) : -1;
        g_occ_skip = env_int("R2_OCC_SKIP", 0);
        if (g_occ_skip < 0) g_occ_skip = 0;
        g_occ_init = 1;
        fprintf(stderr, "[occ] occlusion report wait: skip %d frame(s) in a row, native timeout %lld us (-1 = lifted)\n",
                g_occ_skip, g_occ_us);
        g_occ_next.store(GetTickCount64() + 10000);
    }
    if (g_occ_us < 0 && !g_occ_skip) return 0;

    /* same address arithmetic as the lifted entry; anything unexpected runs the lifted body */
    const uint32_t obj = (uint32_t)ctx->gpr[3];
    const int32_t b = (int32_t)ctx->gpr[4];
    if (b < 0 || b > 64) return 0;
    const uint32_t g = (uint32_t)(ctx->gpr[2] - 0x6EC4);
    if (!span_ok(g, 4) || !span_ok(obj, 0x80 + 2 * (uint64_t)b)) return 0;
    const uint32_t r30 = ld_be32(g);
    if (!span_ok(r30 - 0x8000, 8)) return 0;
    const uint32_t gs = ld_be32(r30 - 0x8000);
    if (!span_ok(gs + 0x6C, 4)) return 0;
    if ((int32_t)ld_be32(obj + 8) != (int32_t)(ld_be32(gs + 0x6C) - 1)) return 0;   /* not last frame's set: no wait */
    const uint32_t count = ld_be16(obj + 0x78 + 2 * b);
    if (!count) return 0;
    const uint32_t list = ld_be32(obj + 0x68 + 4 * b);
    if (!span_ok(list + 2 * (count - 1), 2)) return 0;
    const uint32_t last = ld_be16(list + 2 * (count - 1));
    const uint32_t pbase = ld_be32(r30 - 0x7FFC);
    if (!span_ok(pbase, 4)) return 0;
    const uint32_t rep = ld_be32(pbase) + (last << 4);
    if (!span_ok(rep, 8)) return 0;

    g_occ_calls.fetch_add(1, std::memory_order_relaxed);
    const unsigned long long now = GetTickCount64();
    unsigned long long nx = g_occ_next.load(std::memory_order_relaxed);
    if (now >= nx && g_occ_next.compare_exchange_strong(nx, now + 10000)) occ_report();

    const volatile uint64_t* ts = (const volatile uint64_t*)(vm_base + rep);
    OccSet* st = occ_set(obj, b);
    if (*ts) { st->run = 0; g_occ_ready.fetch_add(1, std::memory_order_relaxed); return 0; }
    if (st->run < g_occ_skip) { st->run++; g_occ_skipped.fetch_add(1, std::memory_order_relaxed); return 1; }
    st->run = 0;
    g_occ_waited.fetch_add(1, std::memory_order_relaxed);
    if (g_occ_us < 0) return 0;   /* the lifted wait */

    LARGE_INTEGER f, q0, q; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&q0);
    const long long lim = g_occ_us * f.QuadPart / 1000000;
    int timed_out = 0;
    for (unsigned i = 0;; i++) {
        if (*ts) break;
        QueryPerformanceCounter(&q);
        if (q.QuadPart - q0.QuadPart >= lim) { timed_out = 1; break; }
        if (i < 64) _mm_pause(); else SwitchToThread();
    }
    QueryPerformanceCounter(&q);
    g_occ_wait_ns.fetch_add((q.QuadPart - q0.QuadPart) * 1000000000 / f.QuadPart, std::memory_order_relaxed);
    if (timed_out) g_occ_timeouts.fetch_add(1, std::memory_order_relaxed);
    /* ready: the lifted body finds the timestamp set and reads the results without sleeping;
       timed out: return like its timeout path (no flags written, set not marked) */
    return timed_out;
}
