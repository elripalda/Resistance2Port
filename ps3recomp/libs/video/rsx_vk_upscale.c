/*
 * Temporal upscaler selection for the Vulkan backend: DLSS (a port's NGX
 * bridge DLL, rsx_vk_dlss_api.h) with FSR 3.1 (rsx_vk_fsr3) as the fallback.
 * See rsx_vk_upscale.h.
 */
#include "rsx_vk_upscale.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rsx_vk_dlss_api.h"
#include "rsx_vk_fsr3.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define VK_MAX_EXTENSION_NAME_SIZE_COPY 256

static struct {
    rsx_vk_upscale_config cfg;
    int cfg_set;
    const port_dlss_vk_api* dlss;   /* bridge table, when loaded */
    int fsr3_loaded;
    const rsx_vk_upscaler_ops* fsr3;
    rsx_vk_upscaler_kind active;
    /* init's arguments, for a late FSR 3.1 start when DLSS creation fails */
    void *inst, *phys, *dev, *gipa, *gdpa;
    int fsr3_ready;
    unsigned cw, ch, cow, coh;      /* last created render / output size */
    const char* inst_ext[48];
    const char* dev_ext[64];
    char names[112][VK_MAX_EXTENSION_NAME_SIZE_COPY];   /* copies: providers reuse their storage */
    unsigned ni, nd, nn;
} s_up;

static void up_log(const char* fmt, ...)
{
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (s_up.cfg.log) s_up.cfg.log(b);
    else fprintf(stderr, "[upscale] %s\n", b);
}

static void up_log_raw(const char* s) { up_log("%s", s); }

static float env_f(const char* name, float def)
{
    const char* e = getenv(name);
    return (e && *e) ? (float)atof(e) : def;
}

int rsx_vk_upscale_config_from_env(rsx_vk_upscale_config* c)
{
    memset(c, 0, sizeof *c);
    const char* e = getenv("RSX_FSR");
    c->mode = e ? atoi(e) : 0;
    c->dlss_dll = getenv("RSX_DLSS_DLL");
    c->dlss_preset = (unsigned)env_f("RSX_DLSS_PRESET", 0.0f);
    float s = env_f("RSX_SHARPEN", 0.0f);
    if (s > 1.0f) s *= 0.01f;   /* a percentage */
    c->sharpness = s < 0.0f ? 0.0f : s > 1.0f ? 1.0f : s;
    float f = env_f("RSX_UPSCALE", 1.0f);
    if (!(f > 0.0f) || f >= 0.999f) f = 1.0f;
    if (f < 0.33f) f = 0.33f;
    c->fraction = f;
    const int p = (int)env_f("RSX_AA_PHASES", 0.0f);
    c->phases = p <= 0 ? 0u : p > 128 ? 128u : (unsigned)p;
    c->dlss_jitter_sign = 1.0f;
    if (getenv("RSX_AA_JITTER_SIGN")) c->dlss_jitter_sign = env_f("RSX_AA_JITTER_SIGN", 1.0f) < 0 ? -1.0f : 1.0f;
    if (getenv("RSX_DLSS_JITTER_SIGN")) c->dlss_jitter_sign = env_f("RSX_DLSS_JITTER_SIGN", 1.0f) < 0 ? -1.0f : 1.0f;
    return c->mode;
}

/* ---- the DLSS bridge -------------------------------------------------------- */
#ifdef _WIN32
static const port_dlss_vk_api* dlss_try(const char* path)
{
    HMODULE m = LoadLibraryA(path);
    if (!m) return NULL;
    port_dlss_vk_get_api_fn get = (port_dlss_vk_get_api_fn)(void*)GetProcAddress(m, PORT_DLSS_VK_GET_API);
    const port_dlss_vk_api* api = get ? get(PORT_DLSS_VK_API_VERSION) : NULL;
    if (!api || api->version < 1u || api->size < sizeof(port_dlss_vk_api) || !api->init || !api->create ||
        !api->evaluate || !api->required_extensions) {
        up_log("%s has no usable %s; DLSS unavailable", path, PORT_DLSS_VK_GET_API);
        FreeLibrary(m);
        return NULL;
    }
    return api;
}

static void exe_dir_a(char* out, size_t n)
{
    DWORD k = GetModuleFileNameA(NULL, out, (DWORD)n);
    while (k && out[k - 1] != '\\' && out[k - 1] != '/') k--;
    out[k] = 0;
}

static const port_dlss_vk_api* dlss_load(const char* path)
{
    char dir[MAX_PATH], p[MAX_PATH + 32];
    if (path && *path) return dlss_try(path);
    exe_dir_a(dir, sizeof dir);
    static const char* names[] = { "port_dlss.dll", "r2_dlss.dll" };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        snprintf(p, sizeof p, "%s%s", dir, names[i]);
        if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES) continue;
        const port_dlss_vk_api* a = dlss_try(p);
        if (a) return a;
    }
    return NULL;
}

static const wchar_t* exe_dir_w(void)
{
    static wchar_t d[MAX_PATH];
    if (!d[0]) {
        DWORD k = GetModuleFileNameW(NULL, d, MAX_PATH);
        while (k && d[k - 1] != L'\\' && d[k - 1] != L'/') k--;
        d[k ? k - 1 : 0] = 0;
        if (!d[0]) { d[0] = L'.'; d[1] = 0; }
    }
    return d;
}
#else
static const port_dlss_vk_api* dlss_load(const char* path) { (void)path; return NULL; }
static const wchar_t* exe_dir_w(void) { return L"."; }
#endif

/* ---- the chain ops ------------------------------------------------------------- */
static void ext_add(const char** list, unsigned* n, unsigned cap, const char* s)
{
    if (!s || !*s) return;
    for (unsigned i = 0; i < *n; i++) if (!strcmp(list[i], s)) return;
    if (*n >= cap || s_up.nn >= sizeof s_up.names / sizeof s_up.names[0]) return;
    char* c = s_up.names[s_up.nn++];
    snprintf(c, VK_MAX_EXTENSION_NAME_SIZE_COPY, "%s", s);
    list[(*n)++] = c;
}

static int op_required(void* user, unsigned* ni, const char*** ie, unsigned* nd, const char*** de)
{
    (void)user;
    s_up.ni = s_up.nd = s_up.nn = 0;
    unsigned a = 0, b = 0;
    const char** ai = NULL; const char** ad = NULL;
    /* Before any Vulkan object exists: NGX's generic list (the feature
     * queries need the instance and GPU; the generic list covers them). */
    if (s_up.dlss && s_up.dlss->required_extensions(NULL, NULL, &a, &ai, &b, &ad)) {
        for (unsigned i = 0; i < a; i++) ext_add(s_up.inst_ext, &s_up.ni, 48, ai[i]);
        for (unsigned i = 0; i < b; i++) ext_add(s_up.dev_ext, &s_up.nd, 64, ad[i]);
    }
    if (s_up.fsr3 && s_up.fsr3->required_extensions) {
        a = b = 0; ai = ad = NULL;
        if (s_up.fsr3->required_extensions(s_up.fsr3->user, &a, &ai, &b, &ad)) {
            for (unsigned i = 0; i < a && ai; i++) ext_add(s_up.inst_ext, &s_up.ni, 48, ai[i]);
            for (unsigned i = 0; i < b && ad; i++) ext_add(s_up.dev_ext, &s_up.nd, 64, ad[i]);
        }
    }
    *ni = s_up.ni; *ie = s_up.ni ? s_up.inst_ext : NULL;
    *nd = s_up.nd; *de = s_up.nd ? s_up.dev_ext : NULL;
    return 1;
}

static void op_device_features(void* user, void* phys, void* gipa, void* inst, void* features,
                               const void* have, void* v12, const void* have12, unsigned n,
                               const char* const* exts, const void** pnext)
{
    (void)user;
    if (s_up.fsr3 && s_up.fsr3->device_features)
        s_up.fsr3->device_features(s_up.fsr3->user, phys, gipa, inst, features, have, v12, have12, n, exts, pnext);
}

static int fsr3_start(void)
{
    if (s_up.fsr3_ready) return 1;
    if (!s_up.fsr3 || !s_up.fsr3->init) return 0;
    s_up.fsr3_ready = s_up.fsr3->init(s_up.fsr3->user, s_up.inst, s_up.phys, s_up.dev, s_up.gipa, s_up.gdpa);
    if (!s_up.fsr3_ready) up_log("FSR 3.1 did not start on this device");
    return s_up.fsr3_ready;
}

static int op_init(void* user, void* inst, void* phys, void* dev, void* gipa, void* gdpa)
{
    (void)user;
    s_up.inst = inst; s_up.phys = phys; s_up.dev = dev; s_up.gipa = gipa; s_up.gdpa = gdpa;
    s_up.active = RSX_VK_UPSCALER_NONE;
    if (s_up.dlss) {
        if (s_up.dlss->init(inst, phys, dev, gipa, gdpa, exe_dir_w(), up_log_raw)) {
            s_up.active = RSX_VK_UPSCALER_DLSS;
            unsigned w = 0, h = 0;
            if (s_up.dlss->optimal_render_size &&
                s_up.dlss->optimal_render_size(3840, 2160, PORT_DLSS_VK_Q_PERFORMANCE, &w, &h))
                up_log("DLSS ready (NGX: 4K Performance renders %ux%u)", w, h);
            else
                up_log("DLSS ready");
            return 1;
        }
        up_log("DLSS unavailable on this GPU/driver%s", s_up.fsr3 ? "; FSR 3.1 instead" : "");
    }
    if (fsr3_start()) {
        s_up.active = RSX_VK_UPSCALER_FSR3;
        up_log("FSR 3.1 ready");
        return 1;
    }
    return 0;
}

static int op_create(void* user, void* cmd, unsigned w, unsigned h, unsigned ow, unsigned oh, int flags)
{
    (void)user;
    int ok = 0;
    if (s_up.active == RSX_VK_UPSCALER_DLSS) {
        /* 8-bit-range colour as the D3D12 path: no HDR flag. */
        ok = s_up.dlss->create(cmd, w, h, ow, oh, flags & PORT_DLSS_VK_DEPTH_INVERTED, s_up.cfg.dlss_preset);
        if (!ok) {
            up_log("DLSS feature creation failed (%ux%u -> %ux%u)%s", w, h, ow, oh,
                   s_up.fsr3 ? "; switching to FSR 3.1" : "");
            if (fsr3_start()) s_up.active = RSX_VK_UPSCALER_FSR3;
            else s_up.active = RSX_VK_UPSCALER_NONE;
        }
    }
    if (!ok && s_up.active == RSX_VK_UPSCALER_FSR3)
        ok = s_up.fsr3->create(s_up.fsr3->user, cmd, w, h, ow, oh, flags);
    if (ok) { s_up.cw = w; s_up.ch = h; s_up.cow = ow; s_up.coh = oh; }
    return ok;
}

static port_dlss_vk_image img(const rsx_vk_upscaler_image* i)
{
    port_dlss_vk_image r = { 0, 0, 0 };
    if (i) { r.image = i->image; r.view = i->view; r.format = i->format; }
    return r;
}

static int op_evaluate(void* user, void* cmd, const rsx_vk_upscaler_image* c, const rsx_vk_upscaler_image* d,
                       const rsx_vk_upscaler_image* m, const rsx_vk_upscaler_image* k,
                       const rsx_vk_upscaler_image* o, unsigned ow, unsigned oh, float jx, float jy,
                       float sx, float sy, int reset, float ms)
{
    (void)user;
    if (s_up.active == RSX_VK_UPSCALER_DLSS) {
        port_dlss_vk_eval e;
        memset(&e, 0, sizeof e);
        e.color = img(c); e.depth = img(d); e.motion = img(m); e.output = img(o);
        if (k && k->image) e.mask = img(k);
        e.out_w = ow; e.out_h = oh;
        e.jitter_x = jx; e.jitter_y = jy;
        e.mv_scale_x = sx; e.mv_scale_y = sy;
        e.reset = reset; e.frame_ms = ms;
        return s_up.dlss->evaluate(cmd, &e);
    }
    if (s_up.active == RSX_VK_UPSCALER_FSR3)
        return s_up.fsr3->evaluate(s_up.fsr3->user, cmd, c, d, m, k, o, ow, oh, jx, jy, sx, sy, reset, ms);
    return 0;
}

static void op_shutdown(void* user)
{
    (void)user;
    if (s_up.dlss && s_up.dlss->shutdown) s_up.dlss->shutdown();
    if (s_up.fsr3_ready && s_up.fsr3->shutdown) s_up.fsr3->shutdown(s_up.fsr3->user);
    s_up.fsr3_ready = 0;
    s_up.active = RSX_VK_UPSCALER_NONE;
    s_up.cw = s_up.ch = s_up.cow = s_up.coh = 0;
}

static const rsx_vk_upscaler_ops s_ops = {
    NULL, op_required, op_init, op_create, op_evaluate, op_shutdown, op_device_features,
};

const rsx_vk_upscaler_ops* rsx_vk_upscale_setup(const rsx_vk_upscale_config* c)
{
    if (c) s_up.cfg = *c;
    else rsx_vk_upscale_config_from_env(&s_up.cfg);
    s_up.cfg_set = 1;
    const int mode = s_up.cfg.mode;
    if (mode != 2 && mode != 3) return NULL;
    if (mode == 2) {
        s_up.dlss = dlss_load(s_up.cfg.dlss_dll);
        if (!s_up.dlss) up_log("no DLSS bridge DLL (port_dlss.dll / RSX_DLSS_DLL); FSR 3.1 instead");
        else if (s_up.dlss->set_project && (s_up.cfg.project_id || s_up.cfg.engine_version))
            s_up.dlss->set_project(s_up.cfg.project_id, s_up.cfg.engine_version);
    }
    s_up.fsr3_loaded = rsx_vk_fsr3_load(NULL, s_up.cfg.log);
    s_up.fsr3 = s_up.fsr3_loaded ? rsx_vk_fsr3_ops() : NULL;
    if (s_up.fsr3) rsx_vk_fsr3_set_sharpness(s_up.cfg.sharpness);
    if (!s_up.dlss && !s_up.fsr3) return NULL;
    return &s_ops;
}

rsx_vk_upscaler_kind rsx_vk_upscale_active(void) { return s_up.active; }

const char* rsx_vk_upscale_name(rsx_vk_upscaler_kind k)
{
    return k == RSX_VK_UPSCALER_DLSS ? "DLSS" : k == RSX_VK_UPSCALER_FSR3 ? "FSR 3.1" : "none";
}

float rsx_vk_upscale_fraction(void)
{
    if (!s_up.cfg_set) { rsx_vk_upscale_config_from_env(&s_up.cfg); s_up.cfg_set = 1; }
    return s_up.cfg.fraction > 0.0f && s_up.cfg.fraction < 1.0f ? s_up.cfg.fraction : 1.0f;
}

void rsx_vk_upscale_render_size(unsigned out_w, unsigned out_h, float f, unsigned* w, unsigned* h)
{
    if (!(f > 0.0f) || f > 1.0f) f = 1.0f;
    unsigned a = (unsigned)floor((double)out_w * f + 0.5), b = (unsigned)floor((double)out_h * f + 0.5);
    *w = a ? a : 1u;
    *h = b ? b : 1u;
}

int rsx_vk_upscale_created_size(unsigned* w, unsigned* h, unsigned* ow, unsigned* oh)
{
    if (w) *w = s_up.cw;
    if (h) *h = s_up.ch;
    if (ow) *ow = s_up.cow;
    if (oh) *oh = s_up.coh;
    return s_up.cw != 0;
}

unsigned rsx_vk_upscale_phases(float f)
{
    if (s_up.cfg.phases) return s_up.cfg.phases;
    if (!(f > 0.0f) || f >= 1.0f) return 8u;
    const unsigned p = (unsigned)floor(8.0 / ((double)f * f) + 0.5);
    return p < 8u ? 8u : p > 72u ? 72u : p;
}

static float halton(unsigned i, unsigned b)
{
    float f = 1.0f, r = 0.0f;
    while (i) { f /= (float)b; r += f * (float)(i % b); i /= b; }
    return r;
}

void rsx_vk_upscale_jitter(unsigned frame, unsigned render_w, unsigned out_w, float* x, float* y)
{
    if (s_up.active == RSX_VK_UPSCALER_FSR3 && rsx_vk_fsr3_jitter(frame, s_up.cfg.phases, x, y)) return;
    const float f = (render_w && out_w > render_w) ? (float)render_w / (float)out_w : 1.0f;
    const unsigned ph = rsx_vk_upscale_phases(f);
    const unsigned i = (frame % ph) + 1u;
    *x = halton(i, 2) - 0.5f;
    *y = halton(i, 3) - 0.5f;
}

float rsx_vk_upscale_eval_jitter_sign(void)
{
    return s_up.active == RSX_VK_UPSCALER_DLSS ? (s_up.cfg.dlss_jitter_sign < 0.0f ? -1.0f : 1.0f) : 1.0f;
}

void rsx_vk_upscale_corner_xf(float xf[8], float f)
{
    if (!(f > 0.0f) || f >= 1.0f) return;
    /* x_ndc' = f x_ndc + (f - 1): [-1, 1] -> [-1, 2f - 1] (left part);
     * y up:   y_ndc' = f y_ndc + (1 - f): [-1, 1] -> [1 - 2f, 1] (top part). */
    xf[0] *= f; xf[1] *= f;
    xf[4] = xf[4] * f + (f - 1.0f);
    xf[5] = xf[5] * f + (1.0f - f);
}

void rsx_vk_upscale_corner_rect(unsigned* x, unsigned* y, unsigned* w, unsigned* h, float f)
{
    if (!(f > 0.0f) || f >= 1.0f) return;
    const double x0 = floor((double)*x * f), y0 = floor((double)*y * f);
    const double x1 = ceil((double)(*x + *w) * f), y1 = ceil((double)(*y + *h) * f);
    *x = (unsigned)x0; *y = (unsigned)y0;
    *w = (unsigned)(x1 - x0); *h = (unsigned)(y1 - y0);
}
