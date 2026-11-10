/* cellGcmSys.c with the two-thread engine (FP1): an IO unmap first lets the
 * queued draws run (they translate IO offsets when they execute), then drops
 * what the engine cached over the range; 2D / NV0039 methods, which write
 * guest memory from the walker, first drain the engine thread.
 */
#include "../cellGcmSys.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
/* The walker's other consumers: no graphics backend, no lifted program. */
PPU_THREAD_LOCAL ppu_context* g_active_ctx;
u32 g_rsx_last_reference;
void (*g_spu_put_hook)(uint32_t ea, uint32_t size);
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
void rsx_raise_user_cmd(uint32_t arg) { (void)arg; }

extern int g_stub_mt_drains, g_stub_inv_calls, g_stub_inv_order;
extern u32 g_stub_inv_loc, g_stub_inv_off, g_stub_inv_len;
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    CHECK(cellGcmMapEaIoAddress(0x30000000u, 0x00200000u, 0x00200000u) == CELL_OK);
    int d0 = g_stub_mt_drains;
    CHECK(cellGcmUnmapIoAddress(0x00200000u) == CELL_OK);
    CHECK(g_stub_mt_drains == d0 + 1);
    CHECK(g_stub_inv_calls == 1);
    CHECK(g_stub_inv_order == d0 + 1);                 /* drained first */
    CHECK(g_stub_inv_loc == RSX_LOCATION_MAIN && g_stub_inv_off == 0x00200000u &&
          g_stub_inv_len == 0x00200000u);

    CHECK(cellGcmMapEaIoAddress(0x30000000u, 0x00500000u, 0x00100000u) == CELL_OK);
    d0 = g_stub_mt_drains;
    CHECK(cellGcmUnmapEaIoAddress(0x30000000u) == CELL_OK);
    CHECK(g_stub_mt_drains == d0 + 1 && g_stub_inv_calls == 2);
    CHECK(g_stub_inv_off == 0x00500000u && g_stub_inv_len == 0x00100000u);
    CHECK(cellGcmUnmapIoAddress(0x00500000u) != CELL_OK);  /* no mapping: nothing */
    CHECK(g_stub_inv_calls == 2);

    d0 = g_stub_mt_drains;
    gcm_2d_method(3, 0x0180, 0xFEED0000u);
    CHECK(g_stub_mt_drains == d0 + 1);
    d0 = g_stub_mt_drains;
    gcm_sub1_nv0039(0x0180, 0xFEED0000u);
    CHECK(g_stub_mt_drains == d0 + 1);

    printf(fails ? "test_gcm_mt_sync: %d FAILED\n" : "test_gcm_mt_sync: all passed\n", fails);
    return fails ? 1 : 0;
}
