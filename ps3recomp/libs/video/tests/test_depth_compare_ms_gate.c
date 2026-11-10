/* Depth-compare emulation on hardware-MSAA passes (R3 purple/green patches):
 * enabled there only with per-sample snapshot addressing on both sides. */
#define main engine_retention_main
#include "test_rsx_draw_engine.c"
#undef main
#include "rsx_draw_engine.c"
static rsx_draw_backend gate_backend;
static unsigned checks, failures;
static int layout_on;
#define T(c,msg) do{checks++;if(!(c)){failures++;fprintf(stderr,"FAIL %s\n",msg);}}while(0)
static u32 aa_caps(void* u,u32 h,u32 sx,u32 sy){(void)u;(void)h;(void)sx;(void)sy;return RSX_BE_AA_HW_MSAA;}
static int sample_layout(void* u){(void)u;return layout_on;}
int main(void){vm_base=calloc(1,GUEST_BYTES);ppu_vm_size=GUEST_BYTES;
 engine_up();gate_backend=g_stub_backend;gate_backend.target_set_aa=aa_caps;gate_backend.fp_sample_layout=sample_layout;rsx_draw_engine_set_backend(&gate_backend);
 rsx_fp_set_res_scale_dynamic(1);layout_on=1;
 T(eng_depth_compare_ms_ok(1),"single-sample pass always emulates");
 T(eng_depth_compare_ms_ok(2),"MSAA pass emulates with per-sample addressing");
 layout_on=0;
 T(!eng_depth_compare_ms_ok(2),"MSAA pass without backend sample layout stays off");
 T(eng_depth_compare_ms_ok(1),"single-sample pass unaffected by missing layout");
 layout_on=1;rsx_fp_set_res_scale_dynamic(0);
 T(!eng_depth_compare_ms_ok(2),"MSAA pass without fp_rscale stays off");
 gate_backend.fp_sample_layout=NULL;rsx_fp_set_res_scale_dynamic(1);
 T(!eng_depth_compare_ms_ok(4),"NULL hook keeps MSAA emulation off");
 rsx_fp_set_res_scale_dynamic(0);
 engine_down();
 printf("%u checks, %u failures\n",checks,failures);return failures?1:0;}
