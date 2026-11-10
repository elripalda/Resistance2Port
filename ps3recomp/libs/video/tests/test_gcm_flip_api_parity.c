/* The flip API in FIFO order, the queue handler and the vblank thread's
 * schedule (cellGcmSys.c; RPCS3 cellGcmSys.cpp gcmSetPrepareFlip /
 * _cellGcmSetFlipCommandWithWaitLabel, rsx_methods.cpp flip_command,
 * RSXDisplayEngine.cpp vblank thread + handle_emu_flip).
 *
 *   1. cellGcmSetPrepareFlip puts the flip into the default context and moves
 *      put past it; it returns the buffer id and the flip is WAITING until
 *      the drain executes it.
 *   2. cellGcmSetFlipCommandWithWaitLabel (no context) writes OFFSET, ACQUIRE
 *      and the flip into the current context; the PPU leaves the label alone.
 *   3. Executing a flip calls the queue handler with 1, once.
 *   4. A vblank late by several periods is one vblank, not a burst.
 *   5. With a frame limit a flip completes no sooner than one limit after the
 *      previous one; with none, as soon as it is executed; never before.
 */
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
PPU_THREAD_LOCAL ppu_context* g_active_ctx;
u32 g_rsx_last_reference;
void (*g_spu_put_hook)(uint32_t ea, uint32_t size);
void rsx_raise_user_cmd(uint32_t arg) { (void)arg; }
void ppu_dump_bctrl_ring(uint32_t n, const char* why) { (void)n; (void)why; }
void ppu_dump_guest_stack(ppu_context* c, const char* why) { (void)c; (void)why; }
void rsx_state_init(rsx_state* state) { (void)state; }
int  rsx_process_method(rsx_state* state, u32 method, u32 data) { (void)state; (void)method; (void)data; return 0; }
int  rsx_draw_engine_enabled(void) { return 0; }
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

#define IO_ADDR   0x00100000u
#define IO_SIZE   0x00400000u
#define CMD_SIZE  0x00080000u
#define QUEUE_OPD 0x00DC5000u
#define LABEL     0x40u
#define VALUE     9u

static uint32_t g_heap = 0x00800000u;
static u32 host_alloc(u32 size, u32 align)
{
    if (!align) align = 4;
    g_heap = (g_heap + align - 1u) & ~(align - 1u);
    u32 r = g_heap;
    g_heap += size;
    return r;
}
static void host_w32(u32 ea, u32 v) { vm_write32(ea, v); }

static int g_queue_calls; static uint64_t g_queue_arg;
static void caller(uint32_t opd, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                   uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
    if (opd == QUEUE_OPD) { g_queue_calls++; g_queue_arg = a0; }
}

static int fails;
static void check(int ok, const char* what)
{
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    fails += !ok;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const uint32_t vm_size = ppu_hle_inject_base + 0x10000u;
    vm_base = (uint8_t*)calloc(1, vm_size);
    if (!vm_base) { puts("FAIL: guest VM alloc"); return 1; }
    ppu_vm_size = vm_size;
    g_ps3_guest_caller = caller;

    const u32 ctx_out = host_alloc(4, 4);
    const u32 ctx = cellGcmSetupContext(ctx_out, CMD_SIZE, IO_SIZE, IO_ADDR, host_alloc, host_w32);
    s_display_buffer_set[1] = 1;
    s_display_buffer_set[2] = 1;
    const u32 begin = vm_read32(ctx + 0);
    s_fifo_getoff = gcm_ea2io(begin);
    vm_write32(GCM_CONTROL_GUEST_ADDR + 0, gcm_ea2io(begin));   /* idle: put == get */
    (void)GCM_PENDING_TAKE();

    /* 1 */
    const s32 rc = cellGcmSetPrepareFlip((void*)(uintptr_t)ctx, 2);
    check(rc == 2, "PrepareFlip returns the buffer id");
    check(vm_read32(begin) == (GCM_FLIP_MARKER | 2u), "the flip is in the default context");
    check(vm_read32(ctx + 8) == begin + 4, "current moved past it");
    check(vm_read32(GCM_CONTROL_GUEST_ADDR + 0) == gcm_ea2io(begin) + 4, "and put with it");
    check(s_flip_status == CELL_GCM_FLIP_STATUS_WAITING, "the flip waits for the RSX");
    check(!gcm_flip_executed_peek(), "not executed before the drain");
    cellGcm_rsx_process_fifo();
    check(s_fifo_getoff == gcm_ea2io(begin) + 4 && gcm_flip_executed_peek(), "the drain executes it");
    check(cellGcmGetCurrentDisplayBufferId() == 2, "and it names buffer 2");
    (void)gcm_flip_executed_take();
    s_flip_status = CELL_GCM_FLIP_STATUS_DONE;

    /* 2 */
    const u32 cur = vm_read32(ctx + 8), put = vm_read32(GCM_CONTROL_GUEST_ADDR + 0);
    const u32 label_ea = GCM_LABEL_GUEST_BASE + LABEL * GCM_LABEL_STRIDE;
    vm_write32(label_ea, 0);
    check(cellGcmSetFlipCommandWithWaitLabel(1, LABEL, VALUE) == CELL_OK, "WithWaitLabel (no ctx) accepted");
    check(vm_read32(cur + 0) == 0x00040064u && vm_read32(cur + 4) == LABEL * GCM_LABEL_STRIDE,
          "SEMAPHORE_OFFSET in the current context");
    check(vm_read32(cur + 8) == 0x00040068u && vm_read32(cur + 12) == VALUE, "then SEMAPHORE_ACQUIRE");
    check(vm_read32(cur + 16) == (GCM_FLIP_MARKER | 1u), "then the flip");
    check(vm_read32(ctx + 8) == cur + 20, "current is past it");
    check(vm_read32(GCM_CONTROL_GUEST_ADDR + 0) == put, "put is the title's to move");
    check(vm_read32(label_ea) == 0, "the PPU did not write the label");

    /* 3: the RSX reaches the flip (the label already holds the value). */
    cellGcmSetQueueHandler((CellGcmQueueHandler)(uintptr_t)QUEUE_OPD);
    vm_write32(label_ea, VALUE);
    vm_write32(GCM_CONTROL_GUEST_ADDR + 0, gcm_ea2io(cur) + 20);
    (void)GCM_PENDING_TAKE();
    cellGcm_rsx_process_fifo();
    check(s_fifo_getoff == gcm_ea2io(cur) + 20, "the drain passed the wait and the flip");
    ppu_gcm_pump();
    check(g_queue_calls == 1 && g_queue_arg == 1, "the queue handler ran with 1");
    ppu_gcm_pump();
    check(g_queue_calls == 1, "once");

    /* 4 */
    const long long P = 16667;
    long long next_vb = 1000000;
    u32 vc = s_vblank_count;
    (void)GCM_PENDING_TAKE();
    int bits = gcm_vblank_step(next_vb - 10, &next_vb, P, 0);
    check(!(bits & 1) && s_vblank_count == vc, "no vblank before its time");
    bits = gcm_vblank_step(1000000 + 5 * P + 100, &next_vb, P, 0);
    check((bits & 1) && s_vblank_count == vc + 1, "a vblank 5 periods late is one vblank");
    check(next_vb == 1000000 + 6 * P + 100, "and the schedule restarts from now");
    bits = gcm_vblank_step(next_vb - 1, &next_vb, P, 0);
    check(!(bits & 1), "no burst after it");

    /* 5 */
    const long long L = 8333;
    s_flip_pace_target_us = 0;
    s_flip_status = CELL_GCM_FLIP_STATUS_WAITING;
    check(!gcm_vblank_flip_try(2000000, L), "a flip not executed does not complete");
    s_flip_direct_pending = 1;
    check(gcm_vblank_flip_try(2000000, L), "an executed flip completes at once when the limit allows");
    check(s_flip_pace_target_us == 2000000 + L, "the next one is due one limit later");
    s_flip_status = CELL_GCM_FLIP_STATUS_WAITING; s_flip_direct_pending = 1;
    check(!gcm_vblank_flip_try(2000000 + L - 100, L), "not before then");
    check(gcm_vblank_deadline(9000000, L) == 2000000 + L, "the thread wakes for it");
    check(gcm_vblank_flip_try(2000000 + L, L), "and completes on time");
    s_flip_status = CELL_GCM_FLIP_STATUS_WAITING; s_flip_direct_pending = 1;
    check(gcm_vblank_flip_try(2000000 + L + 1, 0), "with no limit it completes as soon as executed");

    printf("[gcm_flip_api_parity] %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    free(vm_base);
    return fails ? 1 : 0;
}
