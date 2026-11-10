/*
 * Temporal upscaler selection for the Vulkan backend's upscaler slot
 * (rsx_vulkan_backend_set_temporal_upscaler), shared by every ps3recomp port:
 *
 *   NVIDIA DLSS / DLAA  through a port's NGX bridge DLL (rsx_vk_dlss_api.h,
 *                       e.g. Resistance 2's r2_dlss.dll) + nvngx_dlss.dll
 *   AMD FSR 3.1         through rsx_vk_fsr3 (amd_fidelityfx_vk.dll)
 *   FSR1 EASU + RCAS    output scaling (rsx_vulkan_backend_force_output_fsr1),
 *                       when neither temporal upscaler can run
 *
 * The D3D12 backend's policy (rsx_fsr.inc), mirrored:
 *  - RSX_FSR=2 asks for DLSS; a missing bridge DLL, nvngx_dlss.dll, NVIDIA
 *    GPU or driver, or a failed feature creation, falls back to FSR 3.1 on
 *    the same device (the device is created with both upscalers' extensions);
 *    RSX_FSR=3 asks for FSR 3.1 alone. Without either: FSR1 (caller's job,
 *    rsx_vk_upscale_setup returns NULL or rsx_vulkan_backend_temporal_upscaler_ready
 *    stays 0).
 *  - RSX_UPSCALE=<0.33..1> (default 1 = native AA / DLAA): the share of the
 *    scene target the scene renders to. The scene draws go into the top-left
 *    corner (rsx_vk_upscale_corner_xf), the upscaler reads that corner and
 *    writes the whole target. With RSX_RES_SCALE making the target 4K, 0.5 is
 *    1080p rendering upscaled to 4K.
 *  - jitter: Halton(2,3), index (frame % phases) + 1, minus 0.5; phases 8 at
 *    1x and 8 x (output / render)^2 when upscaling, clamped to 8..72 (NVIDIA's
 *    guidance); RSX_AA_PHASES overrides. With FSR 3.1 active its own sequence
 *    (ffxQuery). Render pixels, +x right, +y down; the same value goes to the
 *    projection and to the upscaler. DLSS gets it times
 *    RSX_DLSS_JITTER_SIGN (or RSX_AA_JITTER_SIGN), default +1.
 *  - motion: the backend's camera-reprojection vectors (no per-object
 *    motion on the PS3), render pixels, current -> previous, plus depth.
 *
 * Settings: RSX_FSR (2 DLSS, 3 FSR 3.1), RSX_UPSCALE, RSX_DLSS_PRESET (NGX
 * render preset, 0 = NVIDIA's default), RSX_DLSS_DLL (bridge path),
 * RSX_SHARPEN (FSR 3.1 RCAS, 0..1 or a percentage), RSX_AA_PHASES,
 * RSX_DLSS_JITTER_SIGN / RSX_AA_JITTER_SIGN.
 *
 * Windows only for DLSS (the bridge is a DLL); everything else is portable C.
 */
#ifndef RSX_VK_UPSCALE_H
#define RSX_VK_UPSCALE_H

#include "rsx_vulkan_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rsx_vk_upscaler_kind {
    RSX_VK_UPSCALER_NONE = 0,
    RSX_VK_UPSCALER_DLSS = 1,
    RSX_VK_UPSCALER_FSR3 = 2,
} rsx_vk_upscaler_kind;

typedef void (*rsx_vk_upscale_log_fn)(const char* msg);

typedef struct rsx_vk_upscale_config {
    int         mode;           /* 2 DLSS (FSR 3.1 fallback), 3 FSR 3.1; else none */
    const char* dlss_dll;       /* bridge DLL; NULL: RSX_DLSS_DLL, then port_dlss.dll */
    const char* project_id;     /* NGX project id; NULL keeps the bridge's */
    const char* engine_version; /* NULL keeps the bridge's                 */
    unsigned    dlss_preset;    /* NVSDK_NGX_DLSS_Hint_Render_Preset        */
    float       sharpness;      /* FSR 3.1 RCAS, 0..1                       */
    float       fraction;       /* render share of the output, 0.33..1      */
    unsigned    phases;         /* jitter phases, 0 = automatic             */
    float       dlss_jitter_sign;
    rsx_vk_upscale_log_fn log;  /* NULL: stderr                             */
} rsx_vk_upscale_config;

/* Fills c from the environment (see above). Returns c->mode. */
int  rsx_vk_upscale_config_from_env(rsx_vk_upscale_config* c);

/* Loads what c->mode asks for and returns the ops to register with
 * rsx_vulkan_backend_set_temporal_upscaler (static storage), or NULL when no
 * temporal upscaler can be loaded (then use FSR1). Call before
 * rsx_vulkan_backend_init; once. */
const rsx_vk_upscaler_ops* rsx_vk_upscale_setup(const rsx_vk_upscale_config* c);

/* The upscaler running on the device (after the backend's init; it can turn
 * from DLSS to FSR 3.1 when the DLSS feature cannot be created). */
rsx_vk_upscaler_kind rsx_vk_upscale_active(void);
const char* rsx_vk_upscale_name(rsx_vk_upscaler_kind k);

/* The configured render share (1 = native AA). */
float rsx_vk_upscale_fraction(void);

/* Render size for an output at fraction f (rounded, at least 1). */
void rsx_vk_upscale_render_size(unsigned out_w, unsigned out_h, float f,
                                unsigned* w, unsigned* h);

/* The render and output size of the last successful create (image pixels:
 * the guest size times RSX_RES_SCALE). 0 before. */
int  rsx_vk_upscale_created_size(unsigned* w, unsigned* h, unsigned* ow, unsigned* oh);

/* Jitter phase count for fraction f (the configured RSX_AA_PHASES when set). */
unsigned rsx_vk_upscale_phases(float f);

/* Frame `frame`'s jitter in render pixels (+x right, +y down) for an
 * upscale from render_w to out_w (0 = native). With FSR 3.1 active and its
 * context made, FSR's sequence; otherwise Halton(2,3). */
void rsx_vk_upscale_jitter(unsigned frame, unsigned render_w, unsigned out_w,
                           float* x, float* y);

/* The factor the jitter handed to the upscaler is multiplied by: the DLSS
 * jitter sign while DLSS is active, else 1. */
float rsx_vk_upscale_eval_jitter_sign(void);

/* Maps a draw's clip -> NDC epilogue (the draw engine's xf: vp_posscale.xyz,
 * _, vp_posoffset.xyz, _; D3D NDC, +y up) into the top-left f x f corner of
 * the target. f outside (0, 1) leaves xf alone. Apply before the jitter. */
void rsx_vk_upscale_corner_xf(float xf[8], float f);

/* Scales a scissor rectangle (target pixels or samples) into the corner:
 * the start rounds down, the end up. */
void rsx_vk_upscale_corner_rect(unsigned* x, unsigned* y, unsigned* w, unsigned* h, float f);

#ifdef __cplusplus
}
#endif

#endif /* RSX_VK_UPSCALE_H */
