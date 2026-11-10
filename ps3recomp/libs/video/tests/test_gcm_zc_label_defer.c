/* Deferred back-end labels (perf item 1): a 0x1D70 label behind critical ZCULL
 * reports is queued instead of blocking on the GPU, written the moment those
 * reports retire, kept in order with later labels, and drained by anything
 * else the guest can see (SET_REFERENCE). RPCS3 write_gcm_label skips the sync
 * for a value memory already holds. */
#include "../cellGcmSys.c"
#include <stdio.h>
#include <stdlib.h>
uint8_t* vm_base;
uint32_t ppu_vm_size;
int g_resv_store_active;
uint32_t g_ww_lo, g_ww_hi;
void ppu_resv_break_store(uint64_t ea) { (void)ea; }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) { (void)a; (void)v; (void)w; }
int spu_coh_is_reserved(uint32_t a) { (void)a; return 0; }
void spu_coh_notify_write(uint32_t a) { (void)a; }
void spu_lockline_lock(void) {}
void spu_lockline_unlock(void) {}
ps3_guest_caller_fn g_ps3_guest_caller;
/* The rest of the walker's world: no graphics backend, no lifted program. */
PPU_THREAD_LOCAL ppu_context* g_active_ctx;
u32 g_rsx_last_reference;
void (*g_spu_put_hook)(uint32_t ea, uint32_t size);
void rsx_raise_user_cmd(uint32_t arg) { (void)arg; }
void ppu_dump_bctrl_ring(uint32_t n, const char* why) { (void)n; (void)why; }
void ppu_dump_guest_stack(ppu_context* c, const char* why) { (void)c; (void)why; }
void rsx_state_init(rsx_state* state) { (void)state; }
int  rsx_process_method(rsx_state* state, u32 method, u32 data) { (void)state; (void)method; (void)data; return 0; }
int  rsx_draw_engine_enabled(void) { return 1; }
void rsx_draw_engine_method(u32 method, u32 arg) { (void)method; (void)arg; }
void rsx_draw_engine_note_flip(u32 buffer_id) { (void)buffer_id; }
void rsx_draw_engine_guest_read(u32 l, u32 o, u32 p, u32 w, u32 h) { (void)l; (void)o; (void)p; (void)w; (void)h; }
void rsx_draw_engine_guest_wrote(u32 l, u32 o, u32 p, u32 w, u32 h) { (void)l; (void)o; (void)p; (void)w; (void)h; }
void rsx_draw_engine_range_read(u32 l, u32 o, u32 n) { (void)l; (void)o; (void)n; }
void rsx_draw_engine_range_wrote(u32 l, u32 o, u32 n) { (void)l; (void)o; (void)n; }
void rsx_draw_engine_range_will_write(u32 l, u32 o, u32 n) { (void)l; (void)o; (void)n; }
void rsx_draw_engine_range_cpu_wrote(u32 l, u32 o, u32 n) { (void)l; (void)o; (void)n; }
int rsx_draw_engine_blit(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g_, u32 h, u32 i, u32 j,
                         u32 k, u32 l, u32 m, u32 n, int lin, u32 iwh)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g_; (void)h; (void)i; (void)j;
  (void)k; (void)l; (void)m; (void)n; (void)lin; (void)iwh; return 0; }
int rsx_draw_engine_blit_swizzled(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g_, u32 h, u32 i,
                                  u32 j, u32 k, int lin)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g_; (void)h; (void)i; (void)j;
  (void)k; (void)lin; return 0; }
int  rsx_live_draw_enabled(void) { return 0; }
void rsx_live_draw_method(u32 method, u32 arg) { (void)method; (void)arg; }
int  rsx_live_draw_blit(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 w, u32 h)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)w; (void)h; return 0; }
void rsx_live_draw_note_resolve(u32 a, u32 b, u32 c, u32 d) { (void)a; (void)b; (void)c; (void)d; }
int  rsx_live_draw_resolve_blit(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g, u32 h,
                                u32 i, u32 j, u32 k, u32 l)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; (void)j; (void)k; (void)l; return 0; }
int rsx_draw_engine_blit_flip(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g_, u32 h, u32 i,
                              u32 j, u32 k, u32 l, u32 m, u32 n, int lin, u32 iwh, u32 bpp, u32 flip)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g_; (void)h; (void)i; (void)j;
  (void)k; (void)l; (void)m; (void)n; (void)lin; (void)iwh; (void)bpp; (void)flip; return 0; }

extern int g_stub_occl_host, g_stub_occl_ready, g_stub_occl_waits, g_stub_occl_kicks;
extern u64 g_stub_occl_value;
extern u32 g_stub_sem_ea;
extern int g_stub_cr_caps, g_stub_cr_calls;
extern u32 g_stub_cr_n, g_stub_cr_ids[16];

static int fails;
static void check(int ok, const char* what)
{
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    fails += !ok;
}

/* One ZPASS query with draws, its report claimed at `sink`. */
static void queue_report(u32 sink)
{
    const int qi = gcm_zc_alloc();
    s_zc.q[qi].host = 1;
    s_zc.q[qi].draws = 1;
    s_zc.q[qi].sync_tag = ++s_zc.timer;
    s_zc.q[qi].ts_us = gcm_zc_now_us();
    gcm_zc_write* w = gcm_zc_push();
    w->q = qi; w->sink = sink; w->type = 1u; w->tag = s_zc.stat_tag;
    s_zc.stat[s_zc.stat_tag].result = 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    _putenv_s("PS3RECOMP_ZCULL_CRITICAL", "1");
    const uint32_t vm_size = ppu_hle_inject_base + 0x00210000u;
    vm_base = (uint8_t*)calloc(1, vm_size);
    if (!vm_base) { puts("FAIL: guest VM alloc"); return 1; }
    ppu_vm_size = vm_size;
    gcm_zc_init();
    g_stub_occl_host = 1;
    g_stub_occl_value = 7;

    const u32 rep = GCM_REPORT_GUEST_BASE + 0x100u, lab = GCM_LABEL_GUEST_BASE + 0x40u;
    const u32 lab2 = GCM_LABEL_GUEST_BASE + 0x50u;

    /* 1. Deferred, not waited on. */
    queue_report(rep);
    g_stub_sem_ea = lab;
    const int k0 = g_stub_occl_kicks;
    check(gcm_zc_pre(0x1D70u, 0x00000001u) == 1, "0x1D70 behind a pending critical report is queued (dropped)");
    check(g_stub_occl_waits == 0 && vm_read32(lab) == 0u, "no GPU wait, label not visible yet");
    check(g_stub_occl_kicks == k0 + 1, "the report's query is submitted (sync hint)");
    check(cellGcm_rsx_work_pending() == 1, "work pending for the host loop");
    g_stub_sem_ea = lab2;
    check(gcm_zc_pre(0x1D74u, 0x22u) == 1 && vm_read32(lab2) == 0u, "a later 0x1D74 queues behind it (order)");

    /* 2. The query completes: report, then labels, without a wait. */
    g_stub_occl_ready = 1;
    s_zc.next_tick_us = 0;
    gcm_zc_tick();
    check(vm_read32(rep + 8) == 7u, "report retired with the query result");
    check(vm_read32(lab) == 0x00010000u && vm_read32(lab2) == 0x22u, "both labels written once it retired (0x1D70 byte swap)");
    check(g_stub_occl_waits == 0 && cellGcm_rsx_work_pending() == 0, "still no wait; nothing pending");

    /* 3. SET_REFERENCE (engine mode: immediate ref, RPCS3 set_reference)
     * queues behind the label rather than syncing; both land in order. */
    g_stub_occl_ready = 0;
    queue_report(rep);
    g_stub_sem_ea = lab;
    check(gcm_zc_pre(0x1D70u, 0x00000002u) == 1 && vm_read32(lab) == 0x00010000u, "deferred again");
    { extern u32 g_rsx_last_reference; g_rsx_last_reference = 0x1234u; }
    vm_write32(GCM_CONTROL_GUEST_ADDR + 8, 0u);
    gcm_method_post(0x50u, 0u);
    check(vm_read32(GCM_CONTROL_GUEST_ADDR + 8) == 0u && vm_read32(lab) == 0x00010000u &&
          g_stub_occl_waits == 0, "SET_REFERENCE behind a deferred label is queued, no wait");
    g_stub_occl_ready = 1;
    s_zc.next_tick_us = 0;
    gcm_zc_tick();
    check(vm_read32(lab) == 0x00020000u && vm_read32(GCM_CONTROL_GUEST_ADDR + 8) == 0x1234u &&
          g_stub_occl_waits == 0 && cellGcm_rsx_work_pending() == 0, "label then ref written on retire");
    { extern u32 g_rsx_last_reference; g_rsx_last_reference = 0x1235u; }
    gcm_method_post(0x50u, 0u);
    check(vm_read32(GCM_CONTROL_GUEST_ADDR + 8) == 0x1235u, "nothing pending: ref published at once (no pacing)");

    /* 3b. A guest-visible NOTIFY drains (forces the report). */
    g_stub_occl_ready = 0;
    queue_report(rep);
    g_stub_sem_ea = lab;
    check(gcm_zc_pre(0x1D70u, 0x00000003u) == 1, "deferred a third time");
    gcm_zc_pre(0x0104u, 0u);
    check(vm_read32(lab) == 0x00030000u && g_stub_occl_waits == 1 && s_zc.wn == 0,
          "NOTIFY forced the report and wrote the label");
    g_stub_sem_ea = lab;
    vm_write32(lab, 0x00020000u);

    /* 4. RPCS3 write_gcm_label: same value already in memory -> no sync. */
    g_stub_occl_waits = 0;
    queue_report(rep);
    check(gcm_zc_pre(0x1D70u, 0x00000002u) == 0 && g_stub_occl_waits == 0 && s_zc.wn == 1 &&
          cellGcm_rsx_work_pending() == 0, "a label write of the value already there neither syncs nor queues");
    /* ... and the flip semaphore never syncs. */
    g_stub_sem_ea = GCM_LABEL_GUEST_BASE + 0x10u;
    check(gcm_zc_pre(0x1D70u, 0x00000005u) == 0 && g_stub_occl_waits == 0 && s_zc.wn == 1,
          "flip semaphore label: no sync");

    /* 5. Engine mode SetWaitFlip: RPCS3's RSX-side wait (SEMAPHORE_OFFSET
     * 0x10 + ACQUIRE 0 in the command buffer), no PPU wait for the flip. */
    {
        const u32 ctx = GCM_LABEL_GUEST_BASE + 0x800u, cb = GCM_LABEL_GUEST_BASE + 0x900u;
        vm_write32(ctx + 0, cb); vm_write32(ctx + 4, cb + 0x100u);
        vm_write32(ctx + 8, cb); vm_write32(ctx + 0xC, 0u);
        s_gcm_context_ea = ctx;
        s_flip_status = CELL_GCM_FLIP_STATUS_WAITING;
        cellGcmSetWaitFlip();
        check(vm_read32(ctx + 8) == cb + 12u && vm_read32(cb) == 0x00080064u &&
              vm_read32(cb + 4) == 0x10u && vm_read32(cb + 8) == 0u,
              "SetWaitFlip emits the RSX-side acquire and returns at once");
        s_gcm_context_ea = 0;
    }

    /* 6. FP7 GPU conditional rendering: SET_RENDER_ENABLE on a report whose
     * query is still in flight hands that query to the engine (no wait); a
     * known zero or visible result decides on the CPU; without the backend
     * capability the old forced read runs. */
    {
        g_stub_occl_ready = 0;
        g_stub_occl_waits = 0;
        gcm_zc_sync_x(1);
        g_stub_occl_waits = 0;
        const u32 rep2 = GCM_REPORT_GUEST_BASE + 0x200u;
        queue_report(rep2);
        const int qi = s_zc.w[(s_zc.wh + s_zc.wn - 1u) % ZC_WRITES].q;
        g_stub_cr_caps = 1;
        s_zc.cr_enabled = 1; s_zc.cr_pending = 1; s_zc.cr_failed = 0; s_zc.cr_addr = rep2;
        const int c0 = g_stub_cr_calls;
        check(gcm_zc_render_disabled() == 0 && g_stub_occl_waits == 0 && s_zc.wn == 1,
              "cond render, query in flight: draws not skipped, no wait, report still queued");
        check(g_stub_cr_calls == c0 + 1 && g_stub_cr_n == 1u && g_stub_cr_ids[0] == (u32)qi && s_zc.cr_gpu,
              "the deciding query is handed to the engine predicate");
        gcm_zc_set_render_enable(1u << 24);
        check(g_stub_cr_calls == c0 + 2 && g_stub_cr_n == RSX_COND_RENDER_OFF && !s_zc.cr_gpu,
              "RENDER_ENABLE mode 1 ends the predicate");
        /* known zero: decided on the CPU, draws skipped, no predicate */
        s_zc.q[qi].have = 1; s_zc.q[qi].result = 0;
        s_zc.cr_enabled = 1; s_zc.cr_pending = 1; s_zc.cr_failed = 0;
        check(gcm_zc_render_disabled() == 1 && g_stub_cr_calls == c0 + 2 && g_stub_occl_waits == 0,
              "a known zero result skips the draws without the GPU");
        /* known nonzero: drawn */
        s_zc.q[qi].result = 5;
        s_zc.cr_pending = 1; s_zc.cr_failed = 0;
        check(gcm_zc_render_disabled() == 0 && g_stub_cr_calls == c0 + 2, "a known nonzero result draws");
        s_zc.q[qi].have = 0;
        /* no capability: the forced read (one wait) */
        g_stub_cr_caps = 0;
        s_zc.cr_pending = 1; s_zc.cr_failed = 0;
        check(gcm_zc_render_disabled() == 0 && g_stub_occl_waits == 1 && g_stub_cr_calls == c0 + 2,
              "without the backend capability the report is forced (CPU path)");
        s_zc.cr_enabled = 0;
    }

    printf("[gcm_zc_label_defer] %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    free(vm_base);
    return fails ? 1 : 0;
}