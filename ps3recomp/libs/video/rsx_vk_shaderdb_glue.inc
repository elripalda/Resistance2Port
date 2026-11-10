/* Shader / pipeline database glue for the Vulkan engine backend (RPCS3
 * shaders_cache + its boot-time "Compiling shaders" pass). Included once by
 * rsx_vulkan_backend.c after vk_eng_variant_build; the file format, writer
 * and merge tool are rsx_vk_shaderdb.c.
 *
 * - Every engine pipeline_create is recorded (both translated programs, the
 *   render state, the vertex layout plan, stride, colour format and target
 *   count, plus the guest microcode when the engine hands it over), and every
 *   (pass class, topology) variant a draw links, into
 *   shadercache/pipelines_<title>.db by a background writer.
 * - At engine init the user's file and the optional read-only
 *   shadercache/pipelines_shipped.db are loaded and each entry is created as
 *   an engine pipeline slot of its own ("shadow"): exactly what
 *   pipeline_create would make, its modules and recorded variants queued on
 *   the async compiler pool as speculative jobs. When the engine later asks
 *   for the same pipeline (same content key) it is handed that slot, built
 *   or still building.
 * - A draw whose variant is not ready queues it on demand (ahead of every
 *   speculative job) and may wait for it, at most RSX_PIPE_SYNC_WAIT_MS per
 *   frame in total, before it is skipped as before.
 *
 * Switches (read once): RSX_SHADERDB=0 (no database at all),
 * RSX_SHADERDB_PRECOMPILE=0 (record only), RSX_PIPE_SYNC_WAIT_MS=N (default
 * 4; RSX_PERF_ALL=0 makes it 0), RSX_SHADERDB_PATH / RSX_SHADERDB_SHIPPED
 * (file overrides). */

#define VK_SDB_TAG        "rsx-vk-shaderdb glue 1"
#define VK_SDB_MAX_SHADOW (VK_ENG_MAX_PIPE / 2u)

static struct {
    int             state;              /* 0 untried, 1 on, -1 off */
    int             kicked, in_kick;
    char            title[128];
    char            path[512], shipped[512];
    u64             settings;
    rsx_sdb_writer* w;
    vkps_map        shadow;             /* pipeline key -> slot + 1 */
    u32*            slots;
    u8*             has_msv;            /* per shadow: pass-class tables existed at kick */
    u8*             done;               /* per shadow: finished (sticky) */
    u32             nshadow;
    u8              claimed[VK_ENG_MAX_PIPE];
    u64             loaded;
    /* the precompile monitor */
    vk_thread_t     mon;
    int             mon_on;
    vk_atomic       mon_stop, fin, ndone;
    u64             t0_us, fin_us;
    long            built0, built_fin, made_fin;
    long            claims;
    /* the next pipeline_create's guest programs (rsx_vk_shaderdb_set_guest_programs) */
    void*           vp; u32 vp_len;
    void*           fp; u32 fp_len;
    /* the bounded wait */
    u32             wepoch;
    u64             wused_us, wtotal_us;
    long            wok;
    /* the skip summary */
    u64             sum_us;
    long            sum_skipped;
} s_sdb;
static char s_sdb_title[128];          /* kept across engine restarts */

static int vk_sdb_env_off(const char* name)
{
    const char* e = getenv(name);
    return e && *e == '0';
}

static u32 vk_sdb_wait_budget_us(void)
{
    static int ms = -1;
    if (ms < 0) {
        const char* a = getenv("RSX_PERF_ALL");
        const char* e = getenv("RSX_PIPE_SYNC_WAIT_MS");
        ms = (e && *e) ? atoi(e) : 4;
        if ((a && *a == '0') || ms < 0) ms = 0;
        if (ms > 1000) ms = 1000;
    }
    return (u32)ms * 1000u;
}

/* Everything the stored programs and inputs depend on beyond themselves:
 * the decompilers' switches, the shapes of the stored structs and the
 * format. Another configuration's file is not used (and the user's own is
 * started again). Not the executable: entries a newer build no longer
 * produces are simply never asked for. */
static u64 vk_sdb_settings(void)
{
    static const char* const env[] = {
        "RSX_PERF_ALL", "RSX_PERF_GPU_VFETCH", "RSX_PERF_DYN_ROP", "RSX_PERF_DYN_TEXPARAM",
        "RSX_PERF_VK_EDS", "RSX_CFG_LEGACY", "RSX_ROP_LEGACY", "RSX_RES_SCALE", "RSX_VK_RES_SCALE",
        "RSX_ENGINE_DEPTH_COMPARE_MS", "RSX_FP_FLR_BIAS", "RSX_FP_JOIN_BARRIER", "RSX_FP_NATIVE_HALF",
        "RSX_FP_SANITIZE_K", "RSX_FP_SHADOW_EXT", "RSX_FP_SHADOW_LOD", "RSX_FP_UNNORM_ALLFETCH",
        "FP_SWAP_RB", "RSX_VP_CMP_FIXUP", "RSX_VP_LEGACY_MUL", "RSX_VK_MSAA", "RSX_VK_MRT_INHERIT",
        "RSX_VK_DEPTH_LEGACY", "RSX_VK_NO_F32RT" };
    u64 h = rsx_sdb_hash(VK_SDB_TAG, sizeof VK_SDB_TAG);
    const u32 shape[3] = { (u32)sizeof(rsx_be_render_state), (u32)sizeof(rsx_vertex_layout_plan),
                           (u32)vk_gvf_on() };
    h = vkps_fnv(h, shape, sizeof shape);
    for (u32 i = 0; i < sizeof env / sizeof env[0]; i++) {
        const char* v = getenv(env[i]);
        h = vkps_fnv(h, env[i], strlen(env[i]) + 1u);
        h = vkps_fnv(h, v ? v : "", v ? strlen(v) + 1u : 1u);
    }
    return h ? h : 1u;
}

static void vk_sdb_paths(void)
{
    char name[128];
    const char* t = s_sdb_title[0] ? s_sdb_title : NULL;
    char exe[512] = "game";
#ifdef _WIN32
    if (!t) {
        const DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof exe);
        if (n && n < sizeof exe) {
            const char* b = exe + n;
            while (b > exe && b[-1] != '\\' && b[-1] != '/') b--;
            memmove(exe, b, strlen(b) + 1u);
            char* dot = strrchr(exe, '.');
            if (dot) *dot = 0;
        }
        t = exe;
    }
#else
    if (!t) t = exe;
#endif
    u32 j = 0;
    for (const char* c = t; *c && j + 1u < sizeof name; c++)
        name[j++] = (char)(((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                            (*c >= '0' && *c <= '9') || *c == '-' || *c == '_') ? *c : '_');
    name[j] = 0;
    const char* pe = getenv("RSX_SHADERDB_PATH");
    const char* se = getenv("RSX_SHADERDB_SHIPPED");
    if (pe && *pe) snprintf(s_sdb.path, sizeof s_sdb.path, "%s", pe);
    else snprintf(s_sdb.path, sizeof s_sdb.path, "shadercache/pipelines_%s.db", j ? name : "game");
    snprintf(s_sdb.shipped, sizeof s_sdb.shipped, "%s",
             (se && *se) ? se : "shadercache/pipelines_shipped.db");
}

static const char* vk_sdb_status(int s)
{
    static const char* const n[] = { "ok", "none", "not a pipeline database (started again)",
                                      "other format version (started again)",
                                      "other settings (started again)", "damaged tail dropped" };
    return (s >= 0 && s <= RSX_SDB_TRUNCATED) ? n[s] : "?";
}

/* A shadow slot is finished once nothing of it is pending any more. */
static int vk_sdb_slot_done(u32 i)
{
    const vk_eng_pipe* P = &s_epipe[s_sdb.slots[i]];
    if (VK_AT_LOAD((vk_atomic*)&P->mstate) == VK_AS_PENDING) return 0;
    for (u32 t = 0; t < VK_ENG_TOPOLOGIES; t++)
        if (VK_AT_LOAD((vk_atomic*)&P->vstate[t]) == VK_AS_PENDING) return 0;
    if (s_sdb.has_msv[i])
        for (u32 k = 0; k < (VK_EMS_PC - 1u) * VK_ENG_TOPOLOGIES; k++)
            if (VK_AT_LOAD(&P->msvstate[k]) == VK_AS_PENDING) return 0;
    return 1;
}

VK_THREAD_FN(vk_sdb_monitor)
{
    (void)arg_;
    VK_THREAD_NAME("RSX shaderdb precompile monitor");
    u64 last = s_sdb.t0_us;
    while (!VK_AT_LOAD(&s_sdb.mon_stop)) {
        u32 n = 0;
        for (u32 i = 0; i < s_sdb.nshadow; i++) {
            if (!s_sdb.done[i] && vk_sdb_slot_done(i)) s_sdb.done[i] = 1;
            n += s_sdb.done[i];
        }
        VK_AT_STORE(&s_sdb.ndone, n);
        const u64 now = rsx_sdb_now_us();
        if (n >= s_sdb.nshadow) {
            s_sdb.fin_us = now;
            s_sdb.built_fin = (long)VK_AT_LOAD(&s_vka.built);
            s_sdb.made_fin = (long)s_vkmod.made;
            VK_AT_STORE(&s_sdb.fin, 1);
            fprintf(stderr, "[shaderdb] precompiled %u/%u in %llu ms (%ld pipelines linked, %u shader modules)\n",
                    n, s_sdb.nshadow, (unsigned long long)((now - s_sdb.t0_us) / 1000u),
                    s_sdb.built_fin - s_sdb.built0, s_vkmod.made);
            break;
        }
        if (now - last >= 2000000u) {
            last = now;
            fprintf(stderr, "[shaderdb] precompiling %u/%u (%llu ms)\n", n, s_sdb.nshadow,
                    (unsigned long long)((now - s_sdb.t0_us) / 1000u));
        }
#ifdef _WIN32
        Sleep(20);
#else
        usleep(20000);
#endif
    }
    VK_THREAD_RET;
}

/* One stored pipeline -> a shadow slot. 0 = made. */
static int vk_sdb_shadow(const rsx_sdb* db, const rsx_sdb_pipe* p)
{
    const rsx_sdb_shader* vs = rsx_sdb_find_shader(db, p->vs_hash);
    const rsx_sdb_shader* ps = rsx_sdb_find_shader(db, p->ps_hash);
    if (!vs || !ps || vs->stage != 0 || ps->stage != 1 ||
        p->rs_len != sizeof(rsx_be_render_state) || p->layout_len != sizeof(rsx_vertex_layout_plan))
        return -1;
    rsx_be_render_state rs;
    rsx_vertex_layout_plan lp;
    memcpy(&rs, p->blob, sizeof rs);
    memcpy(&lp, p->blob + p->rs_len, sizeof lp);
    u32 bpp, rt_count = p->rt_count ? p->rt_count : 1u;
    if (rt_count > RSX_BE_MAX_COLOR_TARGETS) rt_count = RSX_BE_MAX_COLOR_TARGETS;
    const VkFormat cf = vk_eng_format((rsx_be_format)p->rt_fmt, &bpp);
    if (vk_eng_rt_index(cf) < 0) return -1;
    /* Stored inputs that no longer hash to their key: damaged, or another layout of the structs. */
    if (vk_pipe_key(vs->text, ps->text, &rs, lp.count, p->stride, cf, rt_count) != p->key) return -1;
    if (vkps_map_get(&s_sdb.shadow, p->key)) return -1;
    const u32 h = vk_eng_pipeline_create(NULL, vs->text, ps->text, &rs, &lp, p->stride,
                                         (rsx_be_format)p->rt_fmt, rt_count);
    if (!h) return -1;
    vk_eng_pipe* P = &s_epipe[h - 1u];
    if (P->pkey != p->key) { vk_eng_pipeline_release(NULL, h); return -1; }
    const u64 more = p->bits & ~P->vmask;
    P->vmask |= more;
    vk_async_request_mask(P, more);
    u64* m = vkps_map_put(&s_sdb.shadow, p->key);
    if (!m) return -1;
    *m = h;
    s_sdb.has_msv[s_sdb.nshadow] = P->msvstate != NULL;
    s_sdb.slots[s_sdb.nshadow++] = h - 1u;
    return 0;
}

/* Load, open the writer, queue the precompile. Render thread, once the
 * guest programs and the engine layouts exist; retried until they do. */
static void vk_sdb_kick(void)
{
    if (s_sdb.kicked || s_sdb.in_kick) return;
    if (vk_sdb_env_off("RSX_SHADERDB")) { s_sdb.kicked = 1; s_sdb.state = -1; return; }
    if (!s_g.on || !s_vk.device || vk_eng_layouts()) return;
    s_sdb.kicked = 1;
    s_sdb.state = 1;
    vk_sdb_paths();
#ifdef _WIN32
    CreateDirectoryA("shadercache", NULL);
#else
    mkdir("shadercache", 0755);
#endif
    s_sdb.settings = vk_sdb_settings();
    rsx_sdb user, ship, all;
    memset(&user, 0, sizeof user);
    memset(&ship, 0, sizeof ship);
    memset(&all, 0, sizeof all);
    rsx_sdb_load_info ui, si;
    rsx_sdb_load(&user, s_sdb.path, s_sdb.settings, &ui);
    rsx_sdb_load(&ship, s_sdb.shipped, s_sdb.settings, &si);
    /* The user's own first: what this machine actually drew. */
    rsx_sdb_merge(&all, &user);
    rsx_sdb_merge(&all, &ship);
    s_sdb.loaded = all.np;
    s_sdb.w = rsx_sdb_writer_open(s_sdb.path, s_sdb.settings, &user, &ui);
    fprintf(stderr, "[shaderdb] %s: %u pipeline(s), %s; shipped %s: %u pipeline(s), %s%s\n",
            s_sdb.path, user.np, vk_sdb_status(ui.status), s_sdb.shipped, ship.np,
            si.status == RSX_SDB_MISSING ? "none" : si.status == RSX_SDB_OK ? "ok" :
            si.status == RSX_SDB_TRUNCATED ? "damaged tail dropped" : "not used (other version / settings)",
            s_sdb.w ? "" : " (cannot write the database)");
    rsx_sdb_free(&user);
    rsx_sdb_free(&ship);
    if (vk_sdb_env_off("RSX_SHADERDB_PRECOMPILE") || !all.np) { rsx_sdb_free(&all); return; }
    if (!vk_async_on()) {
        fprintf(stderr, "[shaderdb] no precompile: pipelines are built synchronously (RSX_VK_ASYNC_PIPELINES=0)\n");
        rsx_sdb_free(&all);
        return;
    }
    const u32 cap = all.np < VK_SDB_MAX_SHADOW ? all.np : VK_SDB_MAX_SHADOW;
    s_sdb.slots = (u32*)calloc(cap, sizeof(u32));
    s_sdb.has_msv = (u8*)calloc(cap, 1);
    s_sdb.done = (u8*)calloc(cap, 1);
    if (!s_sdb.slots || !s_sdb.has_msv || !s_sdb.done) { rsx_sdb_free(&all); return; }
    s_sdb.t0_us = rsx_sdb_now_us();
    s_sdb.built0 = (long)VK_AT_LOAD(&s_vka.built);
    /* Modules go on the pool too (not inline), as for the engine's own preload. */
    const int drawn = s_vka.drawn;
    s_vka.drawn = 0;
    s_sdb.in_kick = 1;
    u32 bad = 0;
    for (u32 i = 0; i < all.np && s_sdb.nshadow < cap; i++)
        if (vk_sdb_shadow(&all, &all.p[i])) bad++;
    s_sdb.in_kick = 0;
    s_vka.drawn = drawn;
    rsx_sdb_free(&all);
    fprintf(stderr, "[shaderdb] precompiling %u pipeline(s) on %u compiler thread(s) (%u entr%s not usable)\n",
            s_sdb.nshadow, s_vka.nthreads, bad, bad == 1 ? "y" : "ies");
    if (!s_sdb.nshadow) return;
    VK_AT_STORE(&s_sdb.mon_stop, 0);
    s_sdb.mon_on = vk_thread_start(&s_sdb.mon, vk_sdb_monitor) == 0;
}

/* pipeline_create hook: record it, and hand over the shadow slot of the
 * same pipeline if the precompile made one. 0 = create as usual. */
static u32 vk_sdb_on_create(const char* vs, const char* ps, const rsx_be_render_state* rs,
                            const rsx_vertex_layout_plan* layout, u32 stride, rsx_be_format rt_fmt,
                            VkFormat cf, u32 rt_count)
{
    if (s_sdb.in_kick) return 0;
    vk_sdb_kick();
    u32 got = 0;
    if (s_sdb.state > 0) {
        const u64 key = vk_pipe_key(vs, ps, rs, layout->count, stride, cf, rt_count);
        u64 bits = vk_vfile_mask(key);
        const u64* m = vkps_map_get(&s_sdb.shadow, key);
        if (m && *m) {
            const u32 slot = (u32)*m - 1u;
            vk_eng_pipe* P = &s_epipe[slot];
            if (P->used && P->pkey == key && !s_sdb.claimed[slot] &&
                VK_AT_LOAD(&P->mstate) != VK_AS_FAILED) {
                s_sdb.claimed[slot] = 1;
                /* Outside the key (dynamic state); nothing compiled reads them. */
                P->rs.blend_color = rs->blend_color;
                P->rs.blend_color2 = rs->blend_color2;
                s_sdb.claims++;
                bits |= P->vmask;        /* a shipped entry's variants become the user's */
                got = slot + 1u;
            }
        }
        rsx_sdb_writer_pipeline(s_sdb.w, key, vs, (u32)strlen(vs), ps, (u32)strlen(ps), stride,
                                (u32)rt_fmt, rt_count, (!stride && !layout->count) ? 1u : 0u,
                                rs, (u32)sizeof *rs, layout, (u32)sizeof *layout,
                                s_sdb.vp, s_sdb.vp_len, s_sdb.fp, s_sdb.fp_len, bits);
    }
    free(s_sdb.vp); free(s_sdb.fp);
    s_sdb.vp = s_sdb.fp = NULL;
    s_sdb.vp_len = s_sdb.fp_len = 0;
    return got;
}

/* vk_vfile_note hook: a variant linked for the first time. */
static void vk_sdb_note_variant(u64 key, u64 bits)
{
    if (s_sdb.state > 0) rsx_sdb_writer_variants(s_sdb.w, key, bits);
}

static void vk_sdb_relax(void)
{
#ifdef _WIN32
    if (!SwitchToThread()) YieldProcessor();
#else
    sched_yield();
#endif
}

/* A draw's variant is queued (on demand): wait for it within this frame's
 * budget. 1 = it is ready now. */
static int vk_sdb_sync_wait(vk_eng_pipe* P, u32 t, u32 pc)
{
    const u32 budget = vk_sdb_wait_budget_us();
    vk_atomic* vs = vk_vstate(P, t, pc);
    if (!budget || VK_AT_LOAD(vs) != VK_AS_PENDING) return 0;
    if (s_sdb.wepoch != s_f.epoch) { s_sdb.wepoch = s_f.epoch; s_sdb.wused_us = 0; }
    if (s_sdb.wused_us >= budget) return 0;
    const u64 t0 = rsx_sdb_now_us();
    u64 now = t0;
    int st;
    while ((st = (int)VK_AT_LOAD(vs)) == VK_AS_PENDING) {
        now = rsx_sdb_now_us();
        if (s_sdb.wused_us + (now - t0) >= budget) break;
        vk_sdb_relax();
    }
    now = rsx_sdb_now_us();
    s_sdb.wused_us += now - t0;
    s_sdb.wtotal_us += now - t0;
    if (st != VK_AS_READY) return 0;
    s_sdb.wok++;
    return 1;
}

/* The skip log is throttled to powers of 1024: every 2 s while draws are
 * skipped, how many and what the pool is doing. */
static void vk_sdb_skip_summary(void)
{
    const u64 now = rsx_sdb_now_us();
    if (!s_sdb.sum_us) { s_sdb.sum_us = now; s_sdb.sum_skipped = (long)VK_AT_LOAD(&s_vka.skipped) - 1; return; }
    if (now - s_sdb.sum_us < 2000000u) return;
    const long sk = (long)VK_AT_LOAD(&s_vka.skipped);
    pthread_mutex_lock(&s_vka.mu);
    const u32 ql = s_vka.count, qh = s_vka.hcount;
    pthread_mutex_unlock(&s_vka.mu);
    fprintf(stderr, "[shaderdb] %ld draw(s) skipped in the last %.1f s (%ld total); on-demand jobs pending %ld, "
                    "queued %u on demand + %u speculative; %ld pipelines built\n",
            sk - s_sdb.sum_skipped, (double)(now - s_sdb.sum_us) / 1e6, sk,
            (long)VK_AT_LOAD(&s_vka.hi_pending), qh, ql, (long)VK_AT_LOAD(&s_vka.built));
    s_sdb.sum_us = now;
    s_sdb.sum_skipped = sk;
}

/* vk_eng_shutdown hook, before the pool stops and the slots are released. */
static void vk_sdb_shutdown(void)
{
    if (s_sdb.mon_on) {
        VK_AT_STORE(&s_sdb.mon_stop, 1);
        vk_thread_join(s_sdb.mon);
    }
    if (s_sdb.w) {
        rsx_sdb_writer_close(s_sdb.w);
        fprintf(stderr, "[shaderdb] %s closed; %ld engine pipeline(s) served by the precompile, "
                        "%ld synchronous wait(s) satisfied (%llu ms total)\n",
                s_sdb.path, s_sdb.claims, s_sdb.wok, (unsigned long long)(s_sdb.wtotal_us / 1000u));
    }
    vkps_map_free(&s_sdb.shadow);
    free(s_sdb.slots); free(s_sdb.has_msv); free(s_sdb.done);
    free(s_sdb.vp); free(s_sdb.fp);
    memset(&s_sdb, 0, sizeof s_sdb);
}

void rsx_vk_shaderdb_set_guest_programs(const void* vp, uint32_t vp_len, const void* fp, uint32_t fp_len)
{
    free(s_sdb.vp); free(s_sdb.fp);
    s_sdb.vp = s_sdb.fp = NULL;
    s_sdb.vp_len = s_sdb.fp_len = 0;
    if (vp && vp_len && (s_sdb.vp = malloc(vp_len))) { memcpy(s_sdb.vp, vp, vp_len); s_sdb.vp_len = vp_len; }
    if (fp && fp_len && (s_sdb.fp = malloc(fp_len))) { memcpy(s_sdb.fp, fp, fp_len); s_sdb.fp_len = fp_len; }
}

void rsx_vk_shaderdb_set_title(const char* title)
{
    snprintf(s_sdb_title, sizeof s_sdb_title, "%s", title ? title : "");
}

int rsx_vk_shaderdb_precompile(int wait)
{
    vk_sdb_kick();
    if (s_sdb.state <= 0) return -1;
    if (wait && s_sdb.mon_on) {
        while (!VK_AT_LOAD(&s_sdb.fin) && !VK_AT_LOAD(&s_sdb.mon_stop)) {
#ifdef _WIN32
            Sleep(1);
#else
            usleep(1000);
#endif
        }
    }
    return (int)s_sdb.nshadow;
}

void rsx_vk_shaderdb_stats(uint64_t out[10])
{
    const int fin = (int)VK_AT_LOAD(&s_sdb.fin);
    out[0] = s_sdb.loaded;
    out[1] = fin ? (u64)(s_sdb.built_fin - s_sdb.built0) : 0;
    out[2] = (u64)VK_AT_LOAD(&s_sdb.ndone);
    out[3] = (u64)s_sdb.claims;
    out[4] = fin ? (u64)((long)VK_AT_LOAD(&s_vka.built) - s_sdb.built_fin) : 0;
    out[5] = fin ? (u64)((long)s_vkmod.made - s_sdb.made_fin) : 0;
    out[6] = (u64)VK_AT_LOAD(&s_vka.skipped);
    out[7] = (u64)s_sdb.wok;
    out[8] = s_sdb.wtotal_us;
    out[9] = s_sdb.nshadow;
}
