// Resistance 2: temporal AA on the native Vulkan renderer (R2_RSX=vulkan): RSX_FSR=2 DLAA (DLSS, FSR 3.1
// when DLSS can't run), RSX_FSR=3 AMD FSR 3.1 native AA, RSX_FSR=1 FSR1 output scaling only.
//
// The policy of the RPCS3 path's VKR2Upscaler (patches/r2-rpcs3.patch,
// r2aa_on_draw / r2aa_on_flip / r2aa_run), driven by ps3recomp's generic
// draw-engine hooks (rsx_draw_engine_set_aa_hooks) and the Vulkan backend's
// temporal-upscaler slot, filled by ps3recomp's rsx_vk_upscale (DLSS through
// project/r2_dlss.cpp = r2_dlss.dll's port_dlss_vk_get_api, else rsx_vk_fsr3 with
// amd_fidelityfx_vk.dll, FidelityFX SDK 1.1.4):
//  * scene  = the depth buffer with the most depth-tested draws last frame and
//             its busiest, latest colour target; R2 draws it in several runs
//             (pre-pass, material pass), the AA runs after the last one, before
//             transparents/post/HUD (first other target, a texture read of the
//             scene, or the flip);
//  * jitter = rsx_vk_upscale_jitter: Halton(2,3) over 8 frames (RSX_AA_PHASES), added to the
//             viewport of scene draws only, once the upscaler ran on an unchanged scene;
//             FSR 3.1 uses its own sequence and phase count (ffxQuery; RSX_AA_PHASES
//             overrides the count only when set), in render pixels, +x right,
//             +y down, the same value for the projection and for the upscaler
//             (DLSS's times RSX_DLSS_JITTER_SIGN / RSX_AA_JITTER_SIGN);
//  * motion = camera reprojection: each scene draw's object->clip matrix is
//             recovered by evaluating its vertex program on the CPU, matched
//             to last frame's by (program, vertex array, index array,
//             occurrence), and the majority M_prev * M_cur^-1 is the camera.
// Motion vectors: current -> previous, the backend scales them to render pixels.
// RSX_SHARPEN (0..1, or a percentage) sets FSR 3.1's RCAS (default off) and FSR1's.
// If no temporal upscaler can load or start, FSR1 output scaling is used instead (logged once).
// RSX_UPSCALE < 1 (e.g. 0.5 = DLSS Performance): scene draws go into the top-left RSX_UPSCALE share of
// the target (rsx_aa_hooks.draw_scale, like the D3D12 backend's corner rendering) and the pass upscales
// them over the whole target; with RSX_RES_SCALE=3 at R2's 1280x720 that is 1920x1080 -> 3840x2160.
// (RSX_FSR=1 means FSR 3.1 on the RPCS3 path; the launcher writes 3 for the native renderer.)
#if defined(_WIN32) && defined(PS3RECOMP_RSX_VULKAN)

#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "../ps3recomp/libs/video/rsx_draw_engine.h"
#include "../ps3recomp/libs/video/rsx_vp_eval.h"
#include "../ps3recomp/libs/video/rsx_vulkan_backend.h"
#include "../ps3recomp/libs/video/rsx_vk_upscale.h"

namespace {

void r2log(const char* fmt, ...)
{
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "[r2aa] %s\n", b);
}

void dlss_log(const char* s) { r2log("%s", s); }

// ---- settings (the launcher's / the RPCS3 path's environment) -----------
// The upscaler choice, DLSS preset, jitter sign / phases and RSX_SHARPEN are rsx_vk_upscale's
// (rsx_vk_upscale_config_from_env).
struct Cfg {
    bool debug = false;    // RSX_AA_DEBUG
    unsigned view = 0;     // RSX_AA_DEBUG_VIEW: 1 |AA - input| x8, 2 split screen
    bool mask = true;      // RSX_DLSS_MASK=0: no moving-object mask
} g_cfg;

void read_cfg()
{
    if (const char* e = std::getenv("RSX_AA_DEBUG")) g_cfg.debug = std::atoi(e) != 0;
    if (const char* e = std::getenv("RSX_AA_DEBUG_VIEW")) g_cfg.view = (unsigned)std::atoi(e);
    if (const char* e = std::getenv("RSX_DLSS_MASK")) g_cfg.mask = std::atoi(e) != 0;
}

// ---- 4x4 (row-major, m[r][c]) --------------------------------------------
struct m4 { double m[4][4]; };

m4 m4_identity()
{
    m4 r{};
    for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0;
    return r;
}

m4 m4_mul(const m4& a, const m4& b)
{
    m4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
    return r;
}

bool m4_inverse(const m4& a, m4& out)
{
    double t[4][8];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 8; ++c) t[r][c] = c < 4 ? a.m[r][c] : (c - 4 == r ? 1.0 : 0.0);
    for (int c = 0; c < 4; ++c) {
        int p = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(t[r][c]) > std::fabs(t[p][c])) p = r;
        if (std::fabs(t[p][c]) < 1e-12) return false;
        if (p != c)
            for (int k = 0; k < 8; ++k) std::swap(t[p][k], t[c][k]);
        const double inv = 1.0 / t[c][c];
        for (int k = 0; k < 8; ++k) t[c][k] *= inv;
        for (int r = 0; r < 4; ++r) {
            if (r == c) continue;
            const double f = t[r][c];
            if (f != 0.0)
                for (int k = 0; k < 8; ++k) t[r][k] -= f * t[c][k];
        }
    }
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out.m[r][c] = t[r][c + 4];
    return true;
}

double m4_maxabs(const m4& a)
{
    double v = 0;
    for (const auto& r : a.m)
        for (double x : r) v = std::fmax(v, std::fabs(x));
    return v;
}

double m4_maxdiff(const m4& a, const m4& b)
{
    double v = 0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) v = std::fmax(v, std::fabs(a.m[i][j] - b.m[i][j]));
    return v;
}

// The engine's clip -> NDC epilogue (unjittered), as Vulkan NDC (y down).
struct Screen { float xf[8]; bool valid = false; };

m4 m4_screen(const Screen& s)
{
    m4 r{};
    r.m[0][0] = s.xf[0]; r.m[0][3] = s.xf[4];
    r.m[1][1] = -s.xf[1]; r.m[1][3] = -s.xf[5];
    r.m[2][2] = s.xf[2]; r.m[2][3] = s.xf[6];
    r.m[3][3] = 1.0;
    return r;
}

u64 mix64(u64 h, u64 v) { return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)); }

// ---- per-frame state -------------------------------------------------------
struct Tally { unsigned count = 0, first_draw = 0, segments = 0; };

struct State {
    bool disabled = false;
    std::unordered_map<u64, Tally> tally;
    u64 last_pair = ~0ull;
    unsigned draw_index = 0;
    bool scene_valid = false, scene_changed = true;
    u32 scene_rt = 0, scene_ds = 0;
    unsigned seg_count = 0, seg_target = 1;
    unsigned scene_draws = 0;
    bool aa_done = false, cur_scene = false;
    // upscaling: share of the target the scene is drawn into (< 1), and whether this frame does
    float up_f = 1.f;
    bool up_frame = false;
    // the scene as last seen this frame
    u32 color = 0, depth = 0, w = 0, h = 0;
    u32 rt_loc = 0, rt_off = 0;
    float zA = 1.f, zB = 0.f;
    bool inverted = false, msaa = false;
    // camera
    std::unordered_map<u64, m4> cur_M, prev_M;
    std::unordered_map<u64, unsigned> occurrences;
    unsigned evals = 0, eval_fail = 0;
    Screen frame_S, prev_S;
    bool prev_valid = false, ran_ok = false, need_reset = true;
    m4 last_T = m4_identity();
    unsigned frame = 0, jitter_cooldown = 0;
    float jx = 0.f, jy = 0.f;
    std::chrono::steady_clock::time_point last_run{};
    unsigned runs = 0, vote_hits = 0, vote_total = 0;
    std::string last_skip;
} g_st;

void skip_once(const char* why)
{
    if (g_st.last_skip != why) { g_st.last_skip = why; r2log("not applied: %s", why); }
}

u32 addr_key(u32 present, u32 loc, u32 off) { return present ? (0x80000000u | (loc << 30) | (off & 0x3FFFFFFFu)) : 0u; }

bool vote_camera(m4& R, unsigned& matched, unsigned& members)
{
    std::vector<m4> rs;
    rs.reserve(g_st.cur_M.size());
    for (const auto& kv : g_st.cur_M) {
        const auto it = g_st.prev_M.find(kv.first);
        if (it == g_st.prev_M.end()) continue;
        m4 inv;
        if (!m4_inverse(kv.second, inv)) continue;
        const m4 r = m4_mul(it->second, inv);
        if (!std::isfinite(m4_maxabs(r))) continue;
        rs.push_back(r);
    }
    matched = (unsigned)rs.size();
    members = 0;
    if (rs.empty()) return false;
    size_t best = 0;
    for (size_t i = 0; i < rs.size(); ++i) {
        const double tol = 1e-3 * (1.0 + m4_maxabs(rs[i]));
        unsigned n = 0;
        for (size_t j = 0; j < rs.size(); ++j)
            if (m4_maxdiff(rs[i], rs[j]) <= tol) ++n;
        if (n > members) { members = n; best = i; }
    }
    R = rs[best];
    return members >= 4 && members * 4 >= matched;
}

void run()
{
    auto& st = g_st;
    st.aa_done = true;
    if (st.disabled) return;
    if (!rsx_vulkan_backend_temporal_upscaler_ready()) { skip_once("the upscaler is not ready"); return; }
    if (!st.scene_draws || !st.color || !st.depth || !st.frame_S.valid || st.w < 16 || st.h < 16) {
        skip_once("no scene this frame");
        return;
    }
    if (st.msaa) { skip_once("the scene is multisampled"); return; }

    m4 T = m4_identity();
    bool reset = st.need_reset || !st.prev_valid;
    if (st.prev_valid) {
        m4 R, sci;
        unsigned matched = 0, members = 0;
        st.vote_total++;
        if (vote_camera(R, matched, members) && m4_inverse(m4_screen(st.frame_S), sci)) {
            T = m4_mul(m4_screen(st.prev_S), m4_mul(R, sci));
            st.vote_hits++;
            // A camera cut still votes consistently, just with a huge motion: measure it on distant geometry.
            const double zfar = (st.inverted ? 0.002 : 0.998) * st.zA + st.zB;
            double sum = 0;
            int pts = 0;
            for (const double px : { -0.8, 0.0, 0.8 })
                for (const double py : { -0.8, 0.0, 0.8 }) {
                    const double v[4] = { px, py, zfar, 1.0 };
                    double q[4];
                    for (int r = 0; r < 4; ++r)
                        q[r] = T.m[r][0] * v[0] + T.m[r][1] * v[1] + T.m[r][2] * v[2] + T.m[r][3] * v[3];
                    if (std::fabs(q[3]) < 1e-9) continue;
                    sum += std::hypot(q[0] / q[3] - px, q[1] / q[3] - py) * 0.5;
                    ++pts;
                }
            const double motion = pts ? sum / pts : 1e9;
            if (!(motion < 0.2)) {
                reset = true;
                T = m4_identity();
                if (g_cfg.debug) r2log("camera cut (motion %.2f of the screen): history reset", motion);
            }
            st.last_T = T;
        } else if (!matched) {
            reset = true;
        } else {
            T = st.last_T;   // no clear majority (lots of animation): assume the camera kept moving
        }
        if (g_cfg.debug && (st.frame % 120) == 0) r2log("motion vote: %u of %u draws agree", members, matched);
    }
    if (reset) st.last_T = m4_identity();
    st.need_reset = false;

    const auto now = std::chrono::steady_clock::now();
    float ms = st.runs ? std::chrono::duration<float, std::milli>(now - st.last_run).count() : 16.6f;
    ms = std::fmin(std::fmax(ms, 1.f), 100.f);
    st.last_run = now;

    rsx_be_scene_aa q;
    std::memset(&q, 0, sizeof q);
    q.color = st.color; q.depth = st.depth; q.w = st.w; q.h = st.h;
    if (st.up_frame) {
        // The scene was drawn into the up_f corner (draw_scale): render size in, the whole target out.
        q.w = (u32)std::lround(st.w * st.up_f);
        q.h = (u32)std::lround(st.h * st.up_f);
        q.out_w = st.w;
        q.out_h = st.h;
    }
    q.jitter_px[0] = st.jx; q.jitter_px[1] = st.jy;
    const float js = rsx_vk_upscale_eval_jitter_sign();
    q.eval_jitter[0] = st.jx * js; q.eval_jitter[1] = st.jy * js;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) q.reproject[r * 4 + c] = (float)T.m[r][c];
    q.zA = st.zA; q.zB = st.zB;
    q.inverted = st.inverted ? 1u : 0u;
    q.reset = reset ? 1u : 0u;
    q.frame_ms = ms;
    q.debug_view = g_cfg.view;
    q.use_mask = g_cfg.mask ? 1u : 0u;
    if (!rsx_draw_engine_scene_aa(&q)) {
        skip_once("the upscaler pass failed");
        return;
    }
    if (!st.runs++) r2log("first frame processed (%u scene draws, camera motion %s)", st.scene_draws,
                          st.vote_hits ? "found" : "not yet");
    st.last_skip.clear();
    st.ran_ok = true;
}

// ---- engine hooks ------------------------------------------------------------
void on_draw(void*, const rsx_aa_draw_info* in)
{
    auto& st = g_st;
    st.cur_scene = false;
    if (st.disabled) return;
    if (!rsx_vulkan_backend_temporal_upscaler_ready()) {
        st.disabled = true;
        r2log("no temporal upscaler (DLSS / FSR 3.1) on this device; FSR1 output scaling instead");
        rsx_vulkan_backend_force_output_fsr1();
        return;
    }
    const u32 rt = addr_key(in->color, in->color_location, in->color_offset);
    const u32 ds = in->depth_offset != ~0u ? addr_key(in->depth, in->depth_location, in->depth_offset) : 0u;
    const bool dtest = in->depth_test != 0, dwrite = in->depth_write != 0;
    const u64 pair = ((u64)rt << 32) | ds;
    const bool new_run = pair != st.last_pair;
    st.last_pair = pair;
    if (ds && (dtest || dwrite)) {
        auto& t = st.tally[pair];
        if (!t.count) t.first_draw = st.draw_index;
        t.count++;
        if (new_run) t.segments++;
    }
    const bool scene_pair = st.scene_valid && rt == st.scene_rt && ds == st.scene_ds;
    if (scene_pair && new_run) st.seg_count++;
    st.draw_index++;

    if (st.scene_valid && !st.aa_done && st.scene_draws && st.seg_count >= st.seg_target && !scene_pair) {
        // The last run of draws into the scene pair is over (R2 draws it twice: geometry pre-pass, then the
        // material pass after the light passes). Anything else now (transparents, post, HUD, a pass that
        // samples the scene): AA first.
        run();
    }

    if (st.scene_valid && !st.aa_done && ds == st.scene_ds && (dtest || dwrite)) {
        st.cur_scene = true;
        st.scene_draws++;
        st.depth = in->depth;
        if (rt == st.scene_rt) {
            st.color = in->color;
            st.rt_loc = in->color_location;
            st.rt_off = in->color_offset;
            st.w = in->w; st.h = in->h;
            st.msaa = in->aa_sx * in->aa_sy > 1;
            st.zA = in->zA; st.zB = in->zB;
            st.inverted = in->depth_func == 0x0204 || in->depth_func == 0x0206;   // GREATER / GEQUAL
        }
        if (st.evals < 384) {
            st.evals++;
            u64 key = mix64(rsx_dsp_vp_start(in->rsx), rsx_dsp_reg(in->rsx, 0x1680));
            key = mix64(key, rsx_dsp_reg(in->rsx, 0x181C));
            const unsigned occ = st.occurrences[key]++;
            key = mix64(key, occ);
            double M[4][4];
            if (rsx_vp_extract_affine(in->rsx, M)) {
                m4 m;
                std::memcpy(m.m, M, sizeof m.m);
                st.cur_M[key] = m;
            } else {
                st.eval_fail++;
            }
        }
    }
}

int on_transform(void*, const float xf[8], float jitter_px[2])
{
    auto& st = g_st;
    if (st.disabled || !st.cur_scene) return 0;
    std::memcpy(st.frame_S.xf, xf, sizeof st.frame_S.xf);
    st.frame_S.valid = true;
    if (st.jx == 0.f && st.jy == 0.f) return 0;
    jitter_px[0] = st.jx;
    jitter_px[1] = st.jy;
    return 1;
}

float draw_scale(void*)
{
    const auto& st = g_st;
    return st.up_frame && st.cur_scene && !st.disabled ? st.up_f : 1.f;
}

void on_flip(void*)
{
    auto& st = g_st;
    if (st.disabled) return;
    if (st.scene_draws && !st.aa_done) run();

    // Next frame's scene: the depth buffer with the most depth-tested draws, then its busiest colour target.
    u32 best_ds = 0;
    {
        std::unordered_map<u32, unsigned> per_ds;
        unsigned best = 0;
        for (const auto& kv : st.tally) {
            if (!(kv.first >> 32)) continue;   // depth-only (shadow maps) can't be the scene
            const u32 ds = (u32)kv.first;
            const unsigned n = (per_ds[ds] += kv.second.count);
            if (n > best) { best = n; best_ds = ds; }
        }
    }
    unsigned max_count = 0;
    for (const auto& kv : st.tally)
        if ((u32)kv.first == best_ds && (kv.first >> 32)) max_count = max_count > kv.second.count ? max_count : kv.second.count;
    bool found = false;
    u32 new_rt = 0;
    unsigned latest_first = 0;
    const unsigned floor_n = max_count / 4 > 30u ? max_count / 4 : 30u;
    for (const auto& kv : st.tally) {
        if ((u32)kv.first != best_ds || !(kv.first >> 32)) continue;
        if (kv.second.count < floor_n) continue;
        if (!found || kv.second.first_draw > latest_first) {
            found = true;
            latest_first = kv.second.first_draw;
            new_rt = (u32)(kv.first >> 32);
        }
    }
    const bool was_valid = st.scene_valid;
    const u32 old_rt = st.scene_rt, old_ds = st.scene_ds;
    st.scene_valid = found;
    if (found) { st.scene_rt = new_rt; st.scene_ds = best_ds; }
    st.scene_changed = !was_valid || !found || old_rt != st.scene_rt || old_ds != st.scene_ds;
    if (found) {
        const auto it = st.tally.find(((u64)st.scene_rt << 32) | st.scene_ds);
        st.seg_target = it != st.tally.end() && it->second.segments ? it->second.segments : 1u;
    }
    if (st.scene_changed && found) {
        r2log("scene: colour 0x%08x, depth 0x%08x (%u draws, %u passes)", st.scene_rt, st.scene_ds, max_count,
              st.seg_target);
        st.need_reset = true;
    }
    if (g_cfg.debug && (st.frame % 300) == 0)
        r2log("frame %u: %u scene draws, %u matrix evals (%u failed), vote %u/%u", st.frame, st.scene_draws,
              st.evals, st.eval_fail, st.vote_hits, st.vote_total);

    st.prev_M.swap(st.cur_M);
    st.cur_M.clear();
    st.occurrences.clear();
    st.tally.clear();
    st.last_pair = ~0ull;
    st.seg_count = 0;
    st.prev_S = st.frame_S;
    st.prev_valid = st.frame_S.valid && st.ran_ok;
    // Jitter only while the upscaler is known to run on this scene: it ran last frame and the scene is unchanged.
    if (st.jitter_cooldown) st.jitter_cooldown--;
    const bool jitter_on = st.ran_ok && st.scene_valid && !st.scene_changed && !st.jitter_cooldown;
    // Upscale only a scene the pass ran on last frame (at full size the first time), like the jitter: a frame
    // drawn into the corner that the pass then misses is shown as is.
    st.up_frame = st.up_f < 1.f && st.ran_ok && st.scene_valid && !st.scene_changed &&
                  rsx_vulkan_backend_temporal_upscaler_ready();
    st.ran_ok = false;
    st.scene_draws = 0;
    st.draw_index = 0;
    st.evals = st.eval_fail = 0;
    st.aa_done = false;
    st.cur_scene = false;
    st.color = st.depth = 0;
    st.frame_S.valid = false;
    st.frame++;
    st.jx = st.jy = 0.f;
    unsigned rw = 0, rh = 0, ow = 0, oh = 0;
    if (jitter_on && st.w && rsx_vk_upscale_created_size(&rw, &rh, &ow, &oh) && ow) {
        // The selector's jitter (FSR 3.1's own sequence, else Halton(2,3)) is in render pixels; the engine's
        // jitter_px is in guest pixels of the scene target (the backend scales eval_jitter back by the same
        // render scale), so the upscaler gets exactly the selector's value.
        float ox = 0.f, oy = 0.f;
        rsx_vk_upscale_jitter(st.frame, rw, ow, &ox, &oy);
        const float ars = (float)ow / (float)st.w;
        st.jx = ox / ars;
        st.jy = oy / ars;
    }
}

const rsx_aa_hooks g_hooks = { nullptr, on_draw, on_transform, on_flip, draw_scale };

} // namespace

// Before rsx_vulkan_backend_init: RSX_FSR=2 (the launcher's DLAA choice) registers DLSS with FSR 3.1 as the
// fallback, RSX_FSR=3 FSR 3.1 (rsx_vk_upscale); without either FSR1 output scaling. RSX_FSR=1 selects FSR1
// output scaling. 1 when an upscaler is registered.
extern "C" int r2_native_aa_register(void)
{
    rsx_vk_upscale_config cfg;
    const int mode = rsx_vk_upscale_config_from_env(&cfg);
    if (mode == 1) {
        r2log("FSR1 (RSX_FSR=1): spatial output scaling on the native renderer");
        rsx_vulkan_backend_force_output_fsr1();
        return 0;
    }
    if (mode != 2 && mode != 3) return 0;
    read_cfg();
    cfg.log = dlss_log;
    const rsx_vk_upscaler_ops* ops = rsx_vk_upscale_setup(&cfg);
    if (!ops) {
        r2log("neither DLSS nor FSR 3.1 can load; FSR1 output scaling instead");
        rsx_vulkan_backend_force_output_fsr1();
        return 0;
    }
    g_st.up_f = rsx_vk_upscale_fraction();
    rsx_vulkan_backend_set_temporal_upscaler(ops);
    rsx_draw_engine_set_aa_hooks(&g_hooks);
    if (g_st.up_f < 1.f)
        r2log("%s requested for the native renderer: the scene at %.0f%% of the target, upscaled "
              "(DLSS preset %u, FSR 3.1 sharpening %.2f)", mode == 2 ? "DLSS (FSR 3.1 fallback)" : "FSR 3.1",
              (double)g_st.up_f * 100.0, cfg.dlss_preset, (double)cfg.sharpness);
    else
        r2log("%s requested for the native renderer (DLSS preset %u, FSR 3.1 sharpening %.2f)",
              mode == 2 ? "DLAA (FSR 3.1 fallback)" : "FSR 3.1 native AA", cfg.dlss_preset, (double)cfg.sharpness);
    return 1;
}

#else
extern "C" int r2_native_aa_register(void) { return 0; }
#endif
