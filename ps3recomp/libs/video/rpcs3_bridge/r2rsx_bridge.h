/* r2rsx_bridge: drives RPCS3's RSX core (third_party/rpcs3, GPLv2) from the
 * recompiled runtime. RPCS3 runs in its HLE mode (rsx::thread::isHLE): our
 * cellGcmSys HLE plays the part of RPCS3's cellGcmSys module, and the RSX thread
 * reads the title's FIFO, put/get/ref and memory straight out of guest RAM.
 *
 * C API so cellGcmSys.c and the host can call it. Every function is a no-op
 * when the bridge is not enabled. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default on; R2_RSX=d3d12 (or 0) selects the old D3D12 backend. Fixed at the first call. */
int  r2rsx_enabled(void);

/* The window RPCS3 renders into (host thread owning it pumps its messages). */
void r2rsx_set_window(void* hwnd, uint32_t width, uint32_t height);

/* Bring up the RSX thread and allocate the GCM context, as RPCS3's
 * _cellGcmInitBody + sys_rsx_context_allocate do. dma_control_ea is the
 * RsxDmaControl, i.e. the CellGcmControl the title polls minus 0x40.
 * label_ea is the RsxReports block (semaphores +0, notify +0x1000,
 * reports +0x1400; 0x9400 bytes). Blocks until the renderer is up. */
int  r2rsx_context_allocate(uint32_t dma_control_ea, uint32_t label_ea,
                            uint32_t driver_info_ea, uint32_t device_ea,
                            uint32_t local_size);

/* sys_rsx_context_iomap / iounmap: 1 MB granularity. */
void r2rsx_iomap(uint32_t ea, uint32_t io, uint32_t size);
void r2rsx_iounmap(uint32_t io, uint32_t size);

void r2rsx_set_display_buffer(uint32_t id, uint32_t offset, uint32_t pitch,
                              uint32_t width, uint32_t height);
void r2rsx_set_flip_mode(uint32_t mode);            /* CELL_GCM_DISPLAY_VSYNC = 1 */
uint32_t r2rsx_get_flip_status(void);               /* 0 = DONE */
void r2rsx_set_flip_status(uint32_t status);
uint64_t r2rsx_last_flip_time(void);
uint64_t r2rsx_vblank_count(void);
uint32_t r2rsx_current_display_buffer(void);

/* Guest OPDs, 0 to clear. Called on the bridge's interrupt thread, as RPCS3
 * calls them on _gcm_intr_thread. */
void r2rsx_set_flip_handler(uint32_t opd);
void r2rsx_set_vblank_handler(uint32_t opd);
void r2rsx_set_user_handler(uint32_t opd);
void r2rsx_set_queue_handler(uint32_t opd);

void r2rsx_set_tile(uint32_t index, uint32_t location, uint32_t offset, uint32_t size,
                    uint32_t pitch, uint32_t comp, uint32_t base, uint32_t bank, int bound);
void r2rsx_set_zcull(uint32_t index, uint32_t offset, uint32_t width, uint32_t height,
                     uint32_t cullStart, uint32_t zFormat, uint32_t aaFormat,
                     uint32_t zcullDir, uint32_t zcullFormat, uint32_t sFunc,
                     uint32_t sRef, uint32_t sMask, int bound);

/* Wait (bounded) until the RSX has consumed the FIFO up to `put`. */
void r2rsx_wait_get(uint32_t put, unsigned max_ms);

#ifdef __cplusplus
}
#endif
