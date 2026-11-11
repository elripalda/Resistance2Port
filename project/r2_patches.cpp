// Resistance 2: community RPCS3 patches (patch.yml), ported as guest-memory
// data patches applied once at boot, after the EBOOT is loaded and before the
// game's entry runs -- the moment RPCS3 applies them.
//
// Only the EU/NA disc EBOOT 1.00 (BCES00226 / BCUS98120, PPU hash
// 08da3b601b335c5b09e6425b27fe00eacdad6e78) is supported. Every write checks
// the original value first and is skipped (logged) on a mismatch, so another
// EBOOT never gets patched at the wrong place.
//
// Code patches (instruction nops) cannot be byte-patched: the PPU code is
// recompiled. Those live as hooks in tools/hooks/r2.json (see README,
// "RPCS3 patches").
//
//   R2_PATCH_FPS_FLOORS  "Unlock FPS" (Whatcookie) frame-time floors. Always
//                        on when R2_FPS != 30 (the limiter hook depends on
//                        them); =1 also applies them at 30 fps.
//   R2_PATCH_FOV=1       "Extended FOV" (illusion), off by default.
//                        R2_PATCH_FOV_VALUE=<float> overrides the 0.7 (and,
//                        set alone, turns the patch on: the launcher's and the
//                        in-game PC menu's Field of view).
//   R2_PATCH_TIMING_FRAMECOUNT  30 Hz frame-count constants scaled to the
//                        frame rate (hooks in r2.json); on by default.
//
// Graphics quality (all off by default, see README "Graphics enhancements"):
//   R2_PATCH_FULLRES=1      no dynamic resolution: the scene stays at the
//                           game's native 1280x704 (never drops to 1152x640).
//   R2_PATCH_NO_LOADSHED=1  no GPU-time load shedding at all (resolution,
//                           LOD distance x0.75, effect budgets).
//   R2_PATCH_LOD_SCALE=<x>  LOD distance multiplier 1.0..4.0 (hook in r2.json).
//   R2_FOV_SAFE=0           old FOV patch (constant poke, misaligned shadows);
//                           default: FOV widened on the scene camera only.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ps3emu/ps3types.h"
#include "ppu_recomp.h"

extern "C" double r2_fps_target(void);   /* stubs.cpp: 30 = the game's own limiter */
extern "C" double r2_lod_scale(void);
static bool r2_fov_boot(float k);   /* R2_FOV_SAFE, below */

namespace {

/* -1 unset, 0 off, 1 on */
int env_flag(const char* name)
{
    const char* e = getenv(name);
    if (!e || !*e) return -1;
    if (*e == '0' || !_stricmp(e, "off") || !_stricmp(e, "false") || !_stricmp(e, "no")) return 0;
    return 1;
}

uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

/* Write `val` at `ea` if it still holds `orig`. */
bool poke32(const char* patch, uint32_t ea, uint32_t orig, uint32_t val)
{
    const uint32_t cur = vm_read32(ea);
    if (cur == val) return true;
    if (cur != orig) {
        fprintf(stderr, "[patch] %s: 0x%08X holds %08X, expected %08X -- skipped (not EBOOT 1.00?)\n",
                patch, ea, cur, orig);
        return false;
    }
    vm_write32(ea, val);
    return true;
}

} // namespace

extern "C" void r2_patches_apply(void)
{
    /* Unlock FPS (Whatcookie), PPU-08da3b60...:
     *   bef32 0x00D529D8 0.0001   1/30: the clock's initial minimum frame time
     *                             (func_002C8268 copies it to clock+0x4)
     *   bef32 0x00D69EB8 0.0001   1/75: lower clamp of that minimum
     *                             (func_006BFB08 / func_006BFCA0)
     *   be32  0x00635D7C/88 nop   the limiter's wait: the r2_frame_period hook
     *                             in func_00635D28 (tools/hooks/r2.json).
     * The floors used to be written on the first limiter call; writing them
     * here guarantees the clock's minimum is never initialised from 1/30. */
    {
        if (r2_fps_target() != 30.0 || env_flag("R2_PATCH_FPS_FLOORS") == 1) {
            const uint32_t w = f2u(0.0001f);
            const bool a = poke32("fps_floors", 0x00D529D8u, 0x3D088889u, w);
            const bool b = poke32("fps_floors", 0x00D69EB8u, 0x3C5A740Eu, w);
            fprintf(stderr, "[patch] Unlock FPS frame-time floors: %s\n", a && b ? "on" : "partial");
        }
    }

    /* Extended FOV (illusion), PPU-08da3b60...: bef32 0x00DE67E4 0.7.
     * The constant term (1.0) of the tangent approximation in func_00788068,
     * shared by the camera and shadow projections; RPCS3's note: "Breaks
     * shadows, causing them to be misaligned as well as making them jump
     * around when the player moves." */
    const char* fov_value = getenv("R2_PATCH_FOV_VALUE");
    if (env_flag("R2_PATCH_FOV") == 1 || (fov_value && *fov_value && env_flag("R2_PATCH_FOV") != 0)) {
        float v = 0.7f;
        if (const char* e = fov_value) {
            const float x = (float)atof(e);
            if (x >= 0.3f && x <= 1.0f) v = x;
            else fprintf(stderr, "[patch] R2_PATCH_FOV_VALUE=%s out of range 0.3..1.0, using 0.7\n", e);
        }
        if (r2_fov_boot(v)) {}
        else if (poke32("fov", 0x00DE67E4u, 0x3F800000u, f2u(v)))
            fprintf(stderr, "[patch] Extended FOV: on (%.3f)\n", u2f(vm_read32(0x00DE67E4u)));
    }

    /* Dynamic resolution. The scene render targets are 1280x704 (the
     * display config built in func_002C76D8). Each frame, the GPU-time
     * governor func_002C3780 measures the previous frame's RSX time (report
     * timestamps, func_0064BAB0 -> 0x012CF9B0+0xF10) and, when over budget
     * (or when the byte at 0x00DFA000 is > 1, a mode setting), renders that frame at
     * 1152x640 inside the same targets (0x012CEA18 = {w, h}, then restores
     * it); the final pass stretches the 1152x640 viewport to the display.
     * The word at 0x00DF7FDC gates the resolution drop; the game's own debug
     * switch "-no_dynares" (argument parser func_002EB404) clears it, and
     * nothing else reads it. Clearing it keeps every frame at 1280x704; no
     * buffer is resized, so nothing can be cut off. */
    const int noshed = env_flag("R2_PATCH_NO_LOADSHED");
    const int fullres = env_flag("R2_PATCH_FULLRES");
    if (fullres == 1 || noshed == 1) {
        if (poke32("fullres", 0x00DF7FDCu, 1u, 0u))
            fprintf(stderr, "[patch] native resolution (no dynamic resolution, 1280x704): on\n");
    } else {
        fprintf(stderr, "[patch] native resolution (R2_PATCH_FULLRES): off\n");
    }

    /* GPU-time load shedding, same governor: budget = (0x012CEA18+0x20) *
     * 2000.0; above 1.125x / 1.33x / 2x budget the frame also gets LOD
     * distances x0.75 (0x00FC3954+0x48, see r2_lod_scale), effect budget
     * scales x0.5/x0.25/x0.05 and the resolution drop. The 2000.0 constant
     * (pool 0x00D5A510-0x7F0C) is read only by func_002C3780; making it huge
     * means no frame ever counts as over budget. The CPU-time path
     * (0.0366 s) is left alone. Implies R2_PATCH_FULLRES. */
    if (noshed == 1) {
        if (poke32("no_loadshed", 0x00D52604u, f2u(2000.0f), f2u(1.0e20f)))
            fprintf(stderr, "[patch] no GPU load shedding: on\n");
    } else {
        fprintf(stderr, "[patch] no GPU load shedding (R2_PATCH_NO_LOADSHED): off\n");
    }

    const double lod = r2_lod_scale();
    if (lod != 1.0) fprintf(stderr, "[patch] LOD distance scale: %.2f\n", lod);
    else            fprintf(stderr, "[patch] LOD distance scale (R2_PATCH_LOD_SCALE): off\n");
}

/* R2_PATCH_LOD_SCALE=<1.0..4.0>, default 1.0 (off).
 *
 * func_001721C8 (the frame renderer) computes the LOD projection scale as
 * func_00652A60(view width, fov) * (0x00FC3954+0x48) and hands it to
 * func_00652F40 (its only caller), which rebuilds every LOD class's
 * transition/fade distances as authored distance * scale. The game itself
 * trims the multiplier to 0.75 on over-budget frames, so it is a designed
 * tuning knob; values above 1 push LOD switches and fade-outs further out.
 * The hook in tools/hooks/r2.json multiplies that product here. */
extern "C" double r2_lod_scale(void)
{
    static double s = -1.0;
    if (s < 0.0) {
        s = 1.0;
        if (const char* e = getenv("R2_PATCH_LOD_SCALE")) {
            const double x = atof(e);
            if (x >= 1.0 && x <= 4.0) s = x;
            else if (*e && x != 0.0)
                fprintf(stderr, "[patch] R2_PATCH_LOD_SCALE=%s out of range 1.0..4.0, using 1.0\n", e);
        }
    }
    return s;
}

/* R2_PATCH_TIMING_FRAMECOUNT (default on): frame counts authored for 30 Hz.
 *
 * The game clock (0x012F7308) counts frames at +0xD0 (func_006BFD50, +1 per
 * unpaused frame). A few systems use it as a 30 Hz tick, so above 30 fps
 * they run early or more often. Hooks in tools/hooks/r2.json pass those
 * constants through here, scaled by fps/30:
 *   func_0017F8EC  deadline = frame + 150         (+0x40, 5 s at 30 fps)
 *   func_005140A8  frame % 30 == phase            (once a second)
 *   func_0052BE60  frame % 60 == phase            (every 2 s, radius query)
 *   func_0057CDEC  (frame + id) % 60 == 0         (every 2 s)
 *   func_00599620  frame % 60 == phase            (every 2 s, radius query)
 * A larger modulus still hits each phase once per period, so the stagger
 * holds. At 30 fps the constants are unchanged. With R2_FPS=uncapped the
 * scale follows the real frame time (clock timer 0, +0x14), smoothed and
 * quantised to 1/4 so the modulus doesn't change every frame. */
extern "C" uint32_t r2_timing_frames(uint32_t n, uint32_t maxv)
{
    static int on = -1;
    static double fixed = 0.0;
    if (on < 0) {
        on = env_flag("R2_PATCH_TIMING_FRAMECOUNT") != 0;
        const double t = r2_fps_target();
        fixed = t > 0.0 ? t / 30.0 : 0.0;
        fprintf(stderr, "[patch] timing frame counts: %s (%s)\n", on ? "on" : "off",
                fixed > 0.0 ? "fixed fps" : "measured fps");
    }
    if (!on) return n;

    double scale = fixed;
    if (scale <= 0.0) {
        static uint32_t last_frame = 0xFFFFFFFFu;
        static double ema = 1.0 / 30.0, q = 1.0;
        const uint32_t clock = 0x012F7308u;
        const uint32_t frame = vm_read32(clock + 0xD0);
        if (frame != last_frame) {
            last_frame = frame;
            const float dt = u2f(vm_read32(clock + 0x14));
            if (dt > 0.0001f && dt < 0.5f) ema += (dt - ema) * 0.05;
            double s = (1.0 / 30.0) / ema;
            s = s < 1.0 ? 1.0 : s > 32.0 ? 32.0 : s;
            s = (double)(int)(s * 4.0 + 0.5) / 4.0;
            if (s > q * 1.1 || s < q / 1.1) q = s;
        }
        scale = q;
    }
    if (scale <= 1.0) return n;
    const double v = n * scale + 0.5;
    return v >= (double)maxv ? maxv : (uint32_t)v;
}

/* R2_ASPECT: ultrawide / arbitrary aspect, Hor+ (hooks at the entry of the
 * game's two frustum setters in r2.json: func_0067E8B0 takes the horizontal
 * FOV in degrees, func_0067E6F4 the vertical one; f4 is the aspect w/h).
 * Only 16:9 frustums (the scene cameras) are touched; shadow and cube-map
 * frustums (aspect ~1) are not. Wider than 16:9 keeps the vertical FOV and
 * widens the horizontal one; narrower (16:10, 4:3) keeps the horizontal FOV
 * and grows the vertical one. Culling uses the same frustum object, so it
 * follows. The renderer stretches the 16:9 render target over the window
 * (r2rsx_bridge.cpp, stretch_to_display_area), which undoes the anamorphic
 * squeeze; the 2D HUD is stretched with it (R2_HUD_MODE=stretch).
 *   R2_ASPECT=off|16:9 (default: 16:9 picture, pillarboxed by the renderer)
 *            =auto       follow the game window's client area (live)
 *            =21:9 | 32:9 | <w>:<h> | <w>x<h> | <float>   fixed */
#include <cmath>
extern "C" void* g_kbm_hwnd;   /* the game window (rsx backends) */
extern "C" __declspec(dllimport) int __stdcall GetClientRect(void* hwnd, long* rect);
extern "C" __declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);

extern "C" double r2_target_aspect(void)
{
    static int mode = -1;   /* 0 off, 1 window, 2 fixed */
    static double fixed = 0.0, live = 16.0 / 9.0;
    static unsigned long long next = 0;
    if (mode < 0) {
        mode = 0;
        const char* e = getenv("R2_ASPECT");
        if (e && *e && _stricmp(e, "off") && strcmp(e, "0") && _stricmp(e, "16:9")) {
            if (!_stricmp(e, "auto") || !_stricmp(e, "window") || !_stricmp(e, "on") || !strcmp(e, "1")) mode = 1;
            else {
                double a = 0.0, b = 0.0;
                if (!_stricmp(e, "21:9")) a = 64.0 / 27.0, b = 1.0;   /* 2560x1080; 3440x1440 is 43:18 */
                else if (sscanf(e, "%lf:%lf", &a, &b) == 2 || sscanf(e, "%lfx%lf", &a, &b) == 2) {}
                else { a = atof(e); b = 1.0; }
                if (a > 0.0 && b > 0.0 && a / b >= 1.0 && a / b <= 5.0) { mode = 2; fixed = a / b; }
            }
        }
        fprintf(stderr, "[aspect] R2_ASPECT=%s: %s\n", e ? e : "(unset)",
                mode == 0 ? "off (16:9)" : mode == 1 ? "Hor+, follows the window" : "Hor+, fixed");
        if (mode == 2) fprintf(stderr, "[aspect] target %.4f\n", fixed);
    }
    if (mode == 0) return 0.0;
    if (mode == 2) return fixed;
    const unsigned long long now = GetTickCount64();
    if (now >= next && g_kbm_hwnd) {
        next = now + 250;
        long r[4] = {0, 0, 0, 0};
        if (GetClientRect(g_kbm_hwnd, r) && r[2] > 64 && r[3] > 64) {
            const double a = (double)r[2] / (double)r[3];
            if (fabs(a - live) > 0.002) {
                live = a;
                fprintf(stderr, "[aspect] window %ldx%ld: aspect %.4f\n", r[2], r[3], a);
            }
        }
    }
    return live;
}

/* R2_FOV_SAFE (default on whenever a FOV is chosen): wider FOV without the
 * misaligned shadows. The "Extended FOV" patch lowers the constant term K of
 * the game's tan() approximation (func_00788068). Every tan() call in the game
 * shares that constant, not only the camera's: the shadow cascade fitting, the
 * light (spot / cube) frustums and other projection maths get the
 * distorted tangent too, so shadow maps no longer cover what the widened
 * camera sees and jump as the view moves. Safe mode leaves K at 1.0 and
 * applies the very same widening only to the 16:9 scene camera frustums, at
 * the two frustum setters (the hooks below). The culling frustum is that
 * frustum, so it follows, and everything else keeps the original maths.
 *
 * K is still the interface: at boot the chosen value is remembered and the
 * constant is set to a sentinel (just below 1.0); the PC menu's live FOV
 * (r2_pcport.cpp) keeps poking the constant, and each poke is picked up here
 * and replaced by the sentinel again (1.0 = Original). R2_FOV_SAFE=0 restores
 * the plain constant patch. */
static const uint32_t kFovConst = 0x00DE67E4u, kFovSentinel = 0x3F7FFFFFu;
static float g_fov_k = 1.0f;     /* K the player chose */
static int g_fov_safe = -1;

static int fov_safe_mode(void)
{
    if (g_fov_safe < 0) {
        g_fov_safe = env_flag("R2_FOV_SAFE") != 0;
        fprintf(stderr, "[patch] FOV safe mode (R2_FOV_SAFE): %s\n", g_fov_safe ? "on" : "off");
    }
    return g_fov_safe;
}

/* Boot: in safe mode the chosen K is remembered and the constant gets the
 * sentinel instead; false = plain constant patch. */
static bool r2_fov_boot(float k)
{
    if (!fov_safe_mode()) return false;
    if (!poke32("fov", kFovConst, 0x3F800000u, kFovSentinel)) return true;
    g_fov_k = k;
    fprintf(stderr, "[patch] Extended FOV: on (%.3f, safe: scene camera only)\n", k);
    return true;
}

/* func_00788068 with constant term K (float maths as the game; the range
 * reduction is irrelevant for the half-angles of a camera, < 85 deg). */
static double game_tan(double x, double k)
{
    const double q = floor(x * (2.0 / 3.14159265358979323846) + 0.5);
    const double r = x - q * (3.14159265358979323846 / 2.0);
    const double r2 = r * r;
    const double num = r + r * r2 * -0.0957823;
    const double den = k + r2 * (-0.42911619 + r2 * 0.0097099366);
    if (((long long)q) & 1) return num != 0.0 ? -den / num : 0.0;
    return den != 0.0 ? num / den : 0.0;
}

static void fov_track(void)
{
    const uint32_t cur = vm_read32(kFovConst);
    if (cur == kFovSentinel) return;
    if (cur == 0x3F800000u) {                /* original constant */
        if (g_fov_k != 1.0f) {               /* menu: back to Original */
            g_fov_k = 1.0f;
            fprintf(stderr, "[patch] FOV (safe): original\n");
        }
        return;
    }
    const float v = u2f(cur);
    if (v >= 0.3f && v < 1.0f) {             /* a live poke from the PC menu */
        g_fov_k = v;
        vm_write32(kFovConst, kFovSentinel);
        fprintf(stderr, "[patch] FOV (safe): %.3f\n", v);
    }
}

/* R2_FOV_VIEWMODEL (default 0): the first-person arms/weapon keep the game's
 * own FOV. The player camera builds two 16:9 frustums back to back
 * (func_00712F84 at spawn, func_007129A0 / func_00712A50 per frame): the scene
 * one at +0xD00 with the FOV at +0x2140 and a second one at +0x1100 with its
 * own FOV (+0x2144) and a nearer near plane -- the viewmodel projection.
 * Widened like the scene, the arm shrinks away from the screen edge and its
 * cut-off end (the shoulder) comes into view. =1 widens it as well (old). */
static bool viewmodel_frustum(const ppu_context* ctx)
{
    static int widen = -1;
    if (widen < 0) {
        widen = env_flag("R2_FOV_VIEWMODEL") == 1;
        fprintf(stderr, "[patch] FOV viewmodel (R2_FOV_VIEWMODEL): %s\n", widen ? "widened" : "original FOV");
    }
    if (widen) return false;
    const uint32_t lr = (uint32_t)ctx->lr;
    return lr == 0x00712A28u || lr == 0x00712B4Cu || lr == 0x007132B8u;
}

extern "C" void r2_frustum_aspect(ppu_context* ctx, int hfov)
{
    const double a = ctx->fpr[4];
    if (!(a > 1.70 && a < 1.86)) return;   /* only the 16:9 scene frustums */
    if (fov_safe_mode() == 1) {
        fov_track();
        const double fov = ctx->fpr[3];
        if (g_fov_k != 1.0f && fov > 1.0 && fov < 170.0 && !viewmodel_frustum(ctx)) {
            const double h = fov * (3.14159265358979323846 / 360.0);
            const double ratio = game_tan(h, g_fov_k) / game_tan(h, 1.0);
            if (ratio > 1.0 && ratio < 20.0)
                ctx->fpr[3] = (double)(float)(atan(tan(h) * ratio) * (360.0 / 3.14159265358979323846));
        }
    }
    const double t = r2_target_aspect();
    if (t <= 0.0) return;
    const double k = t / a;
    if (fabs(k - 1.0) < 0.005) return;
    const double fov = ctx->fpr[3];
    if (!(fov > 1.0 && fov < 170.0)) return;
    const double h = fov * (3.14159265358979323846 / 360.0);
    if (hfov && k > 1.0)       ctx->fpr[3] = (double)(float)(atan(tan(h) * k) * (360.0 / 3.14159265358979323846));
    else if (!hfov && k < 1.0) ctx->fpr[3] = (double)(float)(atan(tan(h) / k) * (360.0 / 3.14159265358979323846));
    ctx->fpr[4] = (double)(float)(a * k);
}
