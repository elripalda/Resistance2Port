/*
 * rsx_stage_prof.h -- RSX_PROF_STAGES=1: exclusive per-stage timers for the
 * draw path (mt_record_design.md section 7). A stage is the time between two
 * checkpoints on one thread, so the stages of a draw add up to its total and
 * never overlap. Read with rdtsc (calibrated against the wall clock at report
 * time); sum, count, max and a log2-microsecond histogram per stage.
 *
 * The engine stages (RSP_E_*) run on the engine thread, the backend ones
 * (RSP_V_*) inside its draw call, the worker ones (RSP_W_*) on the parallel
 * recorders (their counters are atomic). Off (the default) costs one load
 * and a branch per checkpoint.
 */
#ifndef PS3RECOMP_RSX_STAGE_PROF_H
#define PS3RECOMP_RSX_STAGE_PROF_H

#include <stdint.h>
#if defined(_MSC_VER)
#include <intrin.h>
#define RSP_TSC() ((uint64_t)__rdtsc())
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#define RSP_TSC() ((uint64_t)__rdtsc())
#else
#include <time.h>
static inline uint64_t rsp_tsc_(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#define RSP_TSC() rsp_tsc_()
#endif

enum {
    /* engine (sink_end_impl), letters as in the design's stage table */
    RSP_E_ENTRY, RSP_E_VERTEX, RSP_E_RSTATE, RSP_E_FB, RSP_E_PIPE, RSP_E_AA, RSP_E_TEX,
    RSP_E_VTEXIDX, RSP_E_XFORM, RSP_E_CONST, RSP_E_NOTE, RSP_E_BECALLS, RSP_E_DRAW, RSP_E_POST,
    /* backend (vk_eng_draw) */
    RSP_V_PROLOGUE, RSP_V_RING, RSP_V_CONST, RSP_V_DESC, RSP_V_PASS, RSP_V_RECORD, RSP_V_PACKET,
    /* parallel recording */
    RSP_W_CHUNK, RSP_W_WAIT, RSP_A_ASSEMBLE, RSP_A_WAIT,
    /* C.vertex split: key + lookup, references + remap, plan, decode (the
     * rest -- topology, VCACHE store -- stays C.vertex) */
    RSP_E_VKEY, RSP_E_VREFS, RSP_E_VPLAN, RSP_E_VDECODE,
    RSP_N
};

extern int rsx_stage_prof_on_;           /* -1 unknown, 0 off, 1 on */
int  rsx_stage_prof_init_(void);
void rsx_stage_prof_add_(int id, uint64_t ticks);
void rsx_stage_prof_add_atomic_(int id, uint64_t ticks);
/* Print the stages since the last report (and reset them) to stderr. */
void rsx_stage_prof_report(const char* tag, uint64_t draws);

static inline int rsp_on(void)
{
    const int v = rsx_stage_prof_on_;
    return v >= 0 ? v : rsx_stage_prof_init_();
}
/* A checkpoint chain on one thread: begin, then mark(stage) after each stage. */
#define RSP_BEGIN(last) do { if (rsp_on()) (last) = RSP_TSC(); } while (0)
#define RSP_MARK(last, id) do { if (rsx_stage_prof_on_ > 0) { const uint64_t t_ = RSP_TSC(); \
    rsx_stage_prof_add_((id), t_ - (last)); (last) = t_; } } while (0)

#endif /* PS3RECOMP_RSX_STAGE_PROF_H */
