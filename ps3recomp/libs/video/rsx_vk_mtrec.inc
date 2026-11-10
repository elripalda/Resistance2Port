/* rsx_vk_mtrec.inc -- parallel command recording (RSX_PERF_RECORD_MT).
 * Included by rsx_vulkan_backend.c after the occlusion / conditional
 * rendering code.
 *
 * Every vkCmd* entry point that may run inside a render pass is wrapped
 * (function pointer swap at device load). While an engine render pass is
 * open on the frame's main command buffer, the wrappers do not record: they
 * append a compact packet (op, size, arguments with arrays inline) to the
 * current chunk. The pass's vkCmdBeginRenderPass is deferred the same way.
 *
 * - Chunks are cut between draws (top of vk_eng_draw) every `chunk` draws.
 *   An open host occlusion query is ended in the old chunk and a fresh slot
 *   begun in the new one (a logical query is already the sum of its host
 *   slots), so a query never spans two secondaries.
 * - Bound state is tracked per class (pipeline, sets, vertex / index buffers,
 *   push constant ranges, every dynamic state) for the whole primary; a chunk
 *   starts with the latest record of every class, replayed in the original
 *   order, so a secondary sees exactly the state the primary had there.
 * - Sealed chunks go to worker threads which record them into secondary
 *   command buffers (RENDER_PASS_CONTINUE, per-worker pools per frame slot,
 *   reset when the slot retires). The engine thread helps out when the queue
 *   is long and at pass end.
 * - At vkCmdEndRenderPass the pass is assembled in order: BeginRenderPass
 *   (SECONDARY_COMMAND_BUFFERS), vkCmdExecuteCommands(chunks in cut order),
 *   EndRenderPass. A pass of one chunk is replayed inline (no secondary).
 *
 * The command stream is the serial one plus redundant state re-binds at
 * chunk starts and query splits, so the output is identical. Ring data,
 * descriptor sets and pipelines are all resolved serially before a packet is
 * made; workers only call vkCmd*.
 *
 * Switches (read once at device load; RSX_PERF_ALL=0 turns all off):
 *   RSX_PERF_RECORD_MT=auto|0|N  workers (auto: from the affinity mask)
 *   RSX_PERF_RECORD_MT_CHUNK=N   draws per chunk (default 64, 8..1024)
 *   RSX_PERF_PACKETS=0|1         packet capture; =0 forces the direct path.
 *                                With 0 workers and packets on, passes are
 *                                captured and replayed inline (test mode). */

#define MTR_MAXW 16u

enum {
    MTR_NONE = 0,
    /* state classes */
    MTR_PIPE, MTR_VB, MTR_DS, MTR_PC, MTR_VP, MTR_SC, MTR_SREF, MTR_DBND, MTR_BLEND,
    MTR_DBIAS, MTR_LW, MTR_SWM, MTR_SCM, MTR_IB, MTR_CULL, MTR_FRONT, MTR_TOPO,
    MTR_DTEST, MTR_DWRITE, MTR_DOP, MTR_DBT, MTR_STEST, MTR_SOP,
    MTR_NSTATE,
    /* stream only */
    MTR_DRAW = MTR_NSTATE, MTR_DRAWI, MTR_CLEAR, MTR_BARRIER, MTR_MD, MTR_MDI,
    MTR_QB, MTR_QE, MTR_CRB, MTR_CRE, MTR_PIPE_X, MTR_DS_X, MTR_PC_X,
    MTR_NOPS
};

typedef struct { u16 op, pad; u32 size; } mtr_hdr;          /* size: whole record, 8-aligned */

typedef struct mtr_chunk {
    struct mtr_chunk* next;
    u8*               buf;
    size_t            len, cap;
    u32               ndraw, slot;
    VkRenderPass      rp;
    VkFramebuffer     fb;
    VkCommandBuffer   out;
    vk_atomic         state;                                  /* 0 queued, 1 taken, 2 done */
} mtr_chunk;

typedef struct { u32 lo, hi, seq, len, cap; u8* rec; } mtr_ent;
typedef struct { mtr_ent* e; u32 n, cap; } mtr_fam;
typedef struct { VkCommandPool pool; VkCommandBuffer* cb; u32 n, used, cap; } mtr_pool;

static struct {
    int             cfg_read, installed;
    int             want, chunk, packets, kick;  /* configuration */
    int             env_want, env_chunk, env_packets;
    int             pend_want, pend_chunk, pend_packets; /* test override, applied at main begin */
    int             active;                /* tracking + capture in this main cb */
    u32             workers;               /* in this main cb */
    /* threads */
    u32             nth;
    vk_thread_t     th[MTR_MAXW];
    pthread_mutex_t mu;
    vk_cond         cv_work, cv_done;
    int             stop, mu_ok;
    vk_atomic       widx;
    mtr_chunk      *qh, *qt;
    u32             qn;
    vk_atomic       inflight;
    mtr_pool        pools[MTR_MAXW + 1][VK_FRAMES_MAX];
    /* capture */
    int             cap, cr_open;
    VkCommandBuffer pcb;
    VkRenderPassBeginInfo rbi;
    VkClearValue    clr[RSX_BE_MAX_COLOR_TARGETS + 2];
    mtr_chunk*      cur;
    mtr_chunk**     pass;
    u32             npass, passcap;
    mtr_chunk*      freel;
    /* state table */
    VkCommandBuffer tab_cb;
    u32             seq;
    mtr_fam         fam[MTR_NSTATE];
    mtr_ent**       gather;
    u32             gcap;
    u8*             tmp;
    size_t          tmpcap;
    /* real entry points */
    struct {
        PFN_vkCmdBindPipeline BindPipeline;
        PFN_vkCmdBindVertexBuffers BindVertexBuffers;
        PFN_vkCmdBindDescriptorSets BindDescriptorSets;
        PFN_vkCmdPushConstants PushConstants;
        PFN_vkCmdSetViewport SetViewport;
        PFN_vkCmdSetScissor SetScissor;
        PFN_vkCmdDraw Draw;
        PFN_vkCmdDrawIndexed DrawIndexed;
        PFN_vkCmdSetStencilReference SetStencilReference;
        PFN_vkCmdSetDepthBounds SetDepthBounds;
        PFN_vkCmdSetBlendConstants SetBlendConstants;
        PFN_vkCmdSetDepthBias SetDepthBias;
        PFN_vkCmdSetLineWidth SetLineWidth;
        PFN_vkCmdSetStencilWriteMask SetStencilWriteMask;
        PFN_vkCmdBindIndexBuffer BindIndexBuffer;
        PFN_vkCmdClearAttachments ClearAttachments;
        PFN_vkCmdPipelineBarrier PipelineBarrier;
        PFN_vkCmdBeginRenderPass BeginRenderPass;
        PFN_vkCmdEndRenderPass EndRenderPass;
        PFN_vkCmdExecuteCommands ExecuteCommands;
        PFN_vkCmdSetCullModeEXT cull;
        PFN_vkCmdSetFrontFaceEXT front;
        PFN_vkCmdSetPrimitiveTopologyEXT topo;
        PFN_vkCmdSetDepthTestEnableEXT dtest;
        PFN_vkCmdSetDepthWriteEnableEXT dwrite;
        PFN_vkCmdSetDepthCompareOpEXT dop;
        PFN_vkCmdSetDepthBoundsTestEnableEXT dbt;
        PFN_vkCmdSetStencilTestEnableEXT stest;
        PFN_vkCmdSetStencilOpEXT sop;
        PFN_vkCmdSetStencilCompareMask scmask;
        PFN_vkCmdDrawMultiEXT md;
        PFN_vkCmdDrawMultiIndexedEXT mdi;
        PFN_vkCmdBeginQuery qb;
        PFN_vkCmdEndQuery qe;
        PFN_vkCmdBeginConditionalRenderingEXT crb;
        PFN_vkCmdEndConditionalRenderingEXT cre;
    } r;
    /* stats */
    u64 passes, inl, chunks, draws, helped, fails, splits, secondaries;
} s_mtr;

/* ---- configuration -------------------------------------------------------*/

static u32 mtr_usable_cores(u32* logical)
{
    u32 p = 0, l = 0;
#ifdef _WIN32
    DWORD_PTR pm = 0, sm = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm) || !pm) pm = ~(DWORD_PTR)0;
    for (DWORD_PTR m = pm; m; m &= m - 1) l++;
    DWORD len = 0;
    GetLogicalProcessorInformation(NULL, &len);
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION* info = len ? (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)malloc(len) : NULL;
    if (info && GetLogicalProcessorInformation(info, &len)) {
        for (DWORD i = 0; i < len / sizeof *info; i++)
            if (info[i].Relationship == RelationProcessorCore && (info[i].ProcessorMask & pm)) p++;
    }
    free(info);
    if (!p) p = l;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    l = n > 0 ? (u32)n : 1u;
    p = l;
#endif
    if (logical) *logical = l;
    return p;
}

/* auto: min(cores - 3, 6) on 5+ usable physical cores; on 4 or fewer the
 * walker, engine and game threads already fill the machine, so one worker
 * (the engine thread also records, help-out) -- see the benchmark notes. */
static int mtr_auto_workers(void)
{
    u32 l = 0;
    const u32 p = mtr_usable_cores(&l);
    if (p <= 4) return p <= 2 ? 0 : 1;
    int w = (int)p - 3;
    return w > 6 ? 6 : w;
}

static void mtr_read_cfg(void)
{
    if (s_mtr.cfg_read) return;
    s_mtr.cfg_read = 1;
    const char* a = getenv("RSX_PERF_ALL");
    const char* e = getenv("RSX_PERF_RECORD_MT");
    const char* pk = getenv("RSX_PERF_PACKETS");
    const char* c = getenv("RSX_PERF_RECORD_MT_CHUNK");
    const int all_off = a && *a == '0';
    if (all_off || (pk && *pk == '0')) { s_mtr.want = 0; s_mtr.packets = 0; }
    else {
        if (!e || !*e || !strcmp(e, "auto")) s_mtr.want = mtr_auto_workers();
        else { s_mtr.want = atoi(e); }
        if (s_mtr.want < 0) s_mtr.want = 0;
        if (s_mtr.want > (int)MTR_MAXW) s_mtr.want = (int)MTR_MAXW;
        s_mtr.packets = s_mtr.want > 0 || (pk && *pk == '1');
    }
    s_mtr.chunk = c && atoi(c) > 0 ? atoi(c) : 64;
    if (s_mtr.chunk < 8) s_mtr.chunk = 8;
    if (s_mtr.chunk > 1024) s_mtr.chunk = 1024;
    const char* kk = getenv("RSX_PERF_RECORD_MT_KICK");
    s_mtr.kick = kk && atoi(kk) > 0 ? atoi(kk) : 0;
    s_mtr.pend_want = s_mtr.pend_chunk = s_mtr.pend_packets = -1;
    s_mtr.env_want = s_mtr.want; s_mtr.env_chunk = s_mtr.chunk; s_mtr.env_packets = s_mtr.packets;
}

/* ---- packet buffers -------------------------------------------------------*/

static void* mtr_grow(u8** buf, size_t* cap, size_t need)
{
    if (need <= *cap) return *buf;
    size_t nc = *cap ? *cap : 16384;
    while (nc < need) nc *= 2;
    u8* nb = (u8*)realloc(*buf, nc);
    if (!nb) return NULL;
    *buf = nb; *cap = nc;
    return nb;
}

#define MTR_AL(x) (((x) + 7u) & ~(size_t)7u)

/* Space for a record: in the chunk while capturing, else a scratch buffer
 * (tracking only). NULL on allocation failure. */
static u8* mtr_rec(int capt, u32 op, size_t payload)
{
    const size_t sz = MTR_AL(sizeof(mtr_hdr) + payload);
    u8* p;
    if (capt) {
        mtr_chunk* k = s_mtr.cur;
        if (!mtr_grow(&k->buf, &k->cap, k->len + sz)) return NULL;
        p = k->buf + k->len;
        k->len += sz;
    } else {
        if (!mtr_grow(&s_mtr.tmp, &s_mtr.tmpcap, sz)) return NULL;
        p = s_mtr.tmp;
    }
    mtr_hdr* h = (mtr_hdr*)p;
    h->op = (u16)op; h->pad = 0; h->size = (u32)sz;
    return p + sizeof *h;
}

static void mtr_tab_reset(void)
{
    for (u32 f = 0; f < MTR_NSTATE; f++) s_mtr.fam[f].n = 0;
}

/* The latest record of class `op` covering [lo, hi); records it covers
 * completely are dropped. */
static void mtr_track(u32 op, u32 lo, u32 hi, const u8* payload)
{
    const mtr_hdr* h = (const mtr_hdr*)(payload - sizeof(mtr_hdr));
    mtr_fam* F = &s_mtr.fam[op];
    int at = -1;
    for (u32 i = 0; i < F->n; i++) {
        mtr_ent* e = &F->e[i];
        if (e->lo < lo || e->hi > hi) continue;
        if (at < 0) { at = (int)i; continue; }
        const mtr_ent t = F->e[F->n - 1u];
        F->e[F->n - 1u] = *e;
        *e = t;
        F->n--;
        i--;
    }
    if (at < 0) {
        if (F->n == F->cap) {
            const u32 nc = F->cap ? F->cap * 2u : 4u;
            mtr_ent* ne = (mtr_ent*)realloc(F->e, nc * sizeof *ne);
            if (!ne) return;
            memset(ne + F->cap, 0, (nc - F->cap) * sizeof *ne);
            F->e = ne; F->cap = nc;
        }
        at = (int)F->n++;
    }
    mtr_ent* e = &F->e[at];
    if (e->cap < h->size) {
        u8* nb = (u8*)realloc(e->rec, h->size);
        if (!nb) { F->n--; return; }
        e->rec = nb; e->cap = h->size;
    }
    memcpy(e->rec, h, h->size);
    e->len = h->size;
    e->lo = lo; e->hi = hi;
    e->seq = ++s_mtr.seq;
}

/* ---- chunks ---------------------------------------------------------------*/

static mtr_chunk* mtr_chunk_new(void)
{
    mtr_chunk* k = s_mtr.freel;
    if (k) s_mtr.freel = k->next;
    else if (!(k = (mtr_chunk*)calloc(1, sizeof *k))) return NULL;
    k->next = NULL;
    k->len = 0; k->ndraw = 0;
    k->slot = s_f.cur;
    k->rp = s_mtr.rbi.renderPass; k->fb = s_mtr.rbi.framebuffer;
    k->out = VK_NULL_HANDLE;
    VK_AT_STORE(&k->state, 0);
    /* prefix: every tracked class, in the order it was recorded */
    u32 n = 0, tot = 0;
    for (u32 f = 0; f < MTR_NSTATE; f++) tot += s_mtr.fam[f].n;
    if (tot > s_mtr.gcap) {
        mtr_ent** ng = (mtr_ent**)realloc(s_mtr.gather, tot * 2u * sizeof *ng);
        if (ng) { s_mtr.gather = ng; s_mtr.gcap = tot * 2u; }
        else tot = 0;
    }
    size_t bytes = 0;
    for (u32 f = 0; f < MTR_NSTATE && tot; f++)
        for (u32 i = 0; i < s_mtr.fam[f].n; i++) {
            mtr_ent* e = &s_mtr.fam[f].e[i];
            u32 j = n++;
            while (j && s_mtr.gather[j - 1u]->seq > e->seq) { s_mtr.gather[j] = s_mtr.gather[j - 1u]; j--; }
            s_mtr.gather[j] = e;
            bytes += e->len;
        }
    if (n && mtr_grow(&k->buf, &k->cap, bytes)) {
        for (u32 i = 0; i < n; i++) {
            memcpy(k->buf + k->len, s_mtr.gather[i]->rec, s_mtr.gather[i]->len);
            k->len += s_mtr.gather[i]->len;
        }
    }
    return k;
}

static void mtr_chunk_free(mtr_chunk* k)
{
    k->next = s_mtr.freel;
    s_mtr.freel = k;
}

/* ---- replay ---------------------------------------------------------------*/

typedef struct { u32 bp; u32 pad; VkPipeline p; } mtr_a_pipe;
typedef struct { u32 first, count; } mtr_a_vb;               /* + VkBuffer[n], VkDeviceSize[n] */
typedef struct { VkPipelineLayout layout; u32 bp, first, count, ndyn; } mtr_a_ds; /* + sets, offsets */
typedef struct { VkPipelineLayout layout; u32 stages, off, size, pad; } mtr_a_pc; /* + bytes */
typedef struct { u32 first, count; } mtr_a_arr;              /* + VkViewport[n] / VkRect2D[n] */
typedef struct { u32 a, b, c, d; } mtr_a_draw;
typedef struct { u32 ic, inst, fi; int32_t vo; u32 finst; } mtr_a_drawi;
typedef struct { u32 face, v; } mtr_a_face;
typedef struct { float f[4]; } mtr_a_f4;
typedef struct { VkBuffer b; VkDeviceSize off; u32 type, pad; } mtr_a_ib;
typedef struct { u32 na, nr; } mtr_a_clear;                  /* + VkClearAttachment[na], VkClearRect[nr] */
typedef struct { u32 src, dst, dep, nm, nb, ni; } mtr_a_bar; /* + barrier arrays */
typedef struct { u32 face, fail, pass, dfail, cmp, pad; } mtr_a_sop;
typedef struct { u32 n, inst, first, has_vo; int32_t vo, pad; } mtr_a_md; /* + infos */
typedef struct { VkQueryPool pool; u32 q, flags; } mtr_a_q;
typedef struct { VkBuffer b; VkDeviceSize off; u32 flags, pad; } mtr_a_cr;

static void mtr_replay(VkCommandBuffer cb, const u8* p, size_t len)
{
    const u8* end = p + len;
    while (p < end) {
        const mtr_hdr* h = (const mtr_hdr*)p;
        const u8* a = p + sizeof *h;
        switch (h->op) {
        case MTR_PIPE: case MTR_PIPE_X: {
            const mtr_a_pipe* x = (const mtr_a_pipe*)a;
            s_mtr.r.BindPipeline(cb, (VkPipelineBindPoint)x->bp, x->p);
        } break;
        case MTR_VB: {
            const mtr_a_vb* x = (const mtr_a_vb*)a;
            const VkBuffer* b = (const VkBuffer*)(x + 1);
            s_mtr.r.BindVertexBuffers(cb, x->first, x->count, b, (const VkDeviceSize*)(b + x->count));
        } break;
        case MTR_DS: case MTR_DS_X: {
            const mtr_a_ds* x = (const mtr_a_ds*)a;
            const VkDescriptorSet* s = (const VkDescriptorSet*)(x + 1);
            s_mtr.r.BindDescriptorSets(cb, (VkPipelineBindPoint)x->bp, x->layout, x->first, x->count, s,
                                       x->ndyn, x->ndyn ? (const u32*)(s + x->count) : NULL);
        } break;
        case MTR_PC: case MTR_PC_X: {
            const mtr_a_pc* x = (const mtr_a_pc*)a;
            s_mtr.r.PushConstants(cb, x->layout, x->stages, x->off, x->size, x + 1);
        } break;
        case MTR_VP: {
            const mtr_a_arr* x = (const mtr_a_arr*)a;
            s_mtr.r.SetViewport(cb, x->first, x->count, (const VkViewport*)(x + 1));
        } break;
        case MTR_SC: {
            const mtr_a_arr* x = (const mtr_a_arr*)a;
            s_mtr.r.SetScissor(cb, x->first, x->count, (const VkRect2D*)(x + 1));
        } break;
        case MTR_DRAW: {
            const mtr_a_draw* x = (const mtr_a_draw*)a;
            s_mtr.r.Draw(cb, x->a, x->b, x->c, x->d);
        } break;
        case MTR_DRAWI: {
            const mtr_a_drawi* x = (const mtr_a_drawi*)a;
            s_mtr.r.DrawIndexed(cb, x->ic, x->inst, x->fi, x->vo, x->finst);
        } break;
        case MTR_SREF: { const mtr_a_face* x = (const mtr_a_face*)a; s_mtr.r.SetStencilReference(cb, x->face, x->v); } break;
        case MTR_SWM:  { const mtr_a_face* x = (const mtr_a_face*)a; s_mtr.r.SetStencilWriteMask(cb, x->face, x->v); } break;
        case MTR_SCM:  { const mtr_a_face* x = (const mtr_a_face*)a; s_mtr.r.scmask(cb, x->face, x->v); } break;
        case MTR_DBND: { const mtr_a_f4* x = (const mtr_a_f4*)a; s_mtr.r.SetDepthBounds(cb, x->f[0], x->f[1]); } break;
        case MTR_BLEND: { const mtr_a_f4* x = (const mtr_a_f4*)a; s_mtr.r.SetBlendConstants(cb, x->f); } break;
        case MTR_DBIAS: { const mtr_a_f4* x = (const mtr_a_f4*)a; s_mtr.r.SetDepthBias(cb, x->f[0], x->f[1], x->f[2]); } break;
        case MTR_LW:   { const mtr_a_f4* x = (const mtr_a_f4*)a; s_mtr.r.SetLineWidth(cb, x->f[0]); } break;
        case MTR_IB: {
            const mtr_a_ib* x = (const mtr_a_ib*)a;
            s_mtr.r.BindIndexBuffer(cb, x->b, x->off, (VkIndexType)x->type);
        } break;
        case MTR_CULL:   s_mtr.r.cull(cb, *(const u32*)a); break;
        case MTR_FRONT:  s_mtr.r.front(cb, (VkFrontFace)*(const u32*)a); break;
        case MTR_TOPO:   s_mtr.r.topo(cb, (VkPrimitiveTopology)*(const u32*)a); break;
        case MTR_DTEST:  s_mtr.r.dtest(cb, *(const u32*)a); break;
        case MTR_DWRITE: s_mtr.r.dwrite(cb, *(const u32*)a); break;
        case MTR_DOP:    s_mtr.r.dop(cb, (VkCompareOp)*(const u32*)a); break;
        case MTR_DBT:    s_mtr.r.dbt(cb, *(const u32*)a); break;
        case MTR_STEST:  s_mtr.r.stest(cb, *(const u32*)a); break;
        case MTR_SOP: {
            const mtr_a_sop* x = (const mtr_a_sop*)a;
            s_mtr.r.sop(cb, x->face, (VkStencilOp)x->fail, (VkStencilOp)x->pass, (VkStencilOp)x->dfail,
                        (VkCompareOp)x->cmp);
        } break;
        case MTR_CLEAR: {
            const mtr_a_clear* x = (const mtr_a_clear*)a;
            const VkClearAttachment* ca = (const VkClearAttachment*)(x + 1);
            s_mtr.r.ClearAttachments(cb, x->na, ca, x->nr, (const VkClearRect*)(ca + x->na));
        } break;
        case MTR_BARRIER: {
            const mtr_a_bar* x = (const mtr_a_bar*)a;
            const VkMemoryBarrier* m = (const VkMemoryBarrier*)(x + 1);
            const VkBufferMemoryBarrier* b = (const VkBufferMemoryBarrier*)(m + x->nm);
            const VkImageMemoryBarrier* im = (const VkImageMemoryBarrier*)(b + x->nb);
            s_mtr.r.PipelineBarrier(cb, x->src, x->dst, x->dep, x->nm, m, x->nb, b, x->ni, im);
        } break;
        case MTR_MD: {
            const mtr_a_md* x = (const mtr_a_md*)a;
            s_mtr.r.md(cb, x->n, (const VkMultiDrawInfoEXT*)(x + 1), x->inst, x->first,
                       sizeof(VkMultiDrawInfoEXT));
        } break;
        case MTR_MDI: {
            const mtr_a_md* x = (const mtr_a_md*)a;
            s_mtr.r.mdi(cb, x->n, (const VkMultiDrawIndexedInfoEXT*)(x + 1), x->inst, x->first,
                        sizeof(VkMultiDrawIndexedInfoEXT), x->has_vo ? &x->vo : NULL);
        } break;
        case MTR_QB: { const mtr_a_q* x = (const mtr_a_q*)a; s_mtr.r.qb(cb, x->pool, x->q, x->flags); } break;
        case MTR_QE: { const mtr_a_q* x = (const mtr_a_q*)a; s_mtr.r.qe(cb, x->pool, x->q); } break;
        case MTR_CRB: {
            const mtr_a_cr* x = (const mtr_a_cr*)a;
            const VkConditionalRenderingBeginInfoEXT bi = {
                .sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT,
                .buffer = x->b, .offset = x->off, .flags = x->flags };
            s_mtr.r.crb(cb, &bi);
        } break;
        case MTR_CRE: s_mtr.r.cre(cb); break;
        default: break;
        }
        p += h->size;
    }
}

/* ---- workers --------------------------------------------------------------*/

static VkCommandBuffer mtr_pool_cb(u32 w, u32 slot)
{
    mtr_pool* P = &s_mtr.pools[w][slot];
    if (!P->pool) {
        VkCommandPoolCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                       .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                                       .queueFamilyIndex = s_vk.queue_family };
        if (pvkCreateCommandPool(s_vk.device, &ci, NULL, &P->pool) != VK_SUCCESS) {
            P->pool = VK_NULL_HANDLE;
            return VK_NULL_HANDLE;
        }
    }
    if (P->used < P->n) return P->cb[P->used++];
    if (P->n == P->cap) {
        const u32 nc = P->cap ? P->cap * 2u : 32u;
        VkCommandBuffer* ncb = (VkCommandBuffer*)realloc(P->cb, nc * sizeof *ncb);
        if (!ncb) return VK_NULL_HANDLE;
        P->cb = ncb; P->cap = nc;
    }
    VkCommandBufferAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                       .commandPool = P->pool,
                                       .level = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
                                       .commandBufferCount = 1 };
    if (pvkAllocateCommandBuffers(s_vk.device, &ai, &P->cb[P->n]) != VK_SUCCESS) return VK_NULL_HANDLE;
    P->n++;
    return P->cb[P->used++];
}

/* Record chunk `k` into a secondary from recorder w's pools. */
static void mtr_record(mtr_chunk* k, u32 w)
{
    VkCommandBuffer cb = mtr_pool_cb(w, k->slot);
    k->out = VK_NULL_HANDLE;
    if (!cb) return;
    VkCommandBufferInheritanceInfo ii = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
                                          .renderPass = k->rp, .subpass = 0, .framebuffer = k->fb };
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                    .flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT |
                                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                                    .pInheritanceInfo = &ii };
    if (pvkBeginCommandBuffer(cb, &bi) != VK_SUCCESS) return;
    mtr_replay(cb, k->buf, k->len);
    if (pvkEndCommandBuffer(cb) != VK_SUCCESS) return;
    k->out = cb;
}

static void mtr_done(mtr_chunk* k)
{
    pthread_mutex_lock(&s_mtr.mu);
    VK_AT_STORE(&k->state, 2);
    VK_AT_ADD(&s_mtr.inflight, -1);
    vk_cond_broadcast(&s_mtr.cv_done);
    pthread_mutex_unlock(&s_mtr.mu);
}

/* Caller holds mu. */
static mtr_chunk* mtr_pop_locked(void)
{
    mtr_chunk* k = s_mtr.qh;
    if (!k) return NULL;
    s_mtr.qh = k->next;
    if (!s_mtr.qh) s_mtr.qt = NULL;
    s_mtr.qn--;
    VK_AT_STORE(&k->state, 1);
    return k;
}

VK_THREAD_FN(mtr_worker_main)
{
    (void)arg_;
    const u32 w = (u32)VK_AT_ADD(&s_mtr.widx, 1);
#ifdef _WIN32
    wchar_t nm[48];
    swprintf(nm, 48, L"RSX record worker %u", w);
    vk_thread_name(nm);
#endif
    for (;;) {
        pthread_mutex_lock(&s_mtr.mu);
        while (!s_mtr.qh && !s_mtr.stop) vk_cond_wait(&s_mtr.cv_work, &s_mtr.mu);
        mtr_chunk* k = mtr_pop_locked();
        pthread_mutex_unlock(&s_mtr.mu);
        if (!k) break;
        mtr_record(k, w);
        mtr_done(k);
    }
    VK_THREAD_RET;
}

static void mtr_threads_up(u32 n)
{
    if (!s_mtr.mu_ok) {
        pthread_mutex_init(&s_mtr.mu, NULL);
        vk_cond_init(&s_mtr.cv_work);
        vk_cond_init(&s_mtr.cv_done);
        s_mtr.mu_ok = 1;
    }
    while (s_mtr.nth < n && s_mtr.nth < MTR_MAXW) {
        if (vk_thread_start(&s_mtr.th[s_mtr.nth], mtr_worker_main) != 0) break;
        s_mtr.nth++;
    }
}

/* Every queued chunk recorded. */
static void mtr_wait_idle(void)
{
    if (!s_mtr.mu_ok || !VK_AT_LOAD(&s_mtr.inflight)) return;
    pthread_mutex_lock(&s_mtr.mu);
    while (VK_AT_LOAD(&s_mtr.inflight)) vk_cond_wait(&s_mtr.cv_done, &s_mtr.mu);
    pthread_mutex_unlock(&s_mtr.mu);
}

/* ---- capture --------------------------------------------------------------*/

/* 2: capture, 1: track only (and record), 0: pass through. */
static inline int mtr_hit(VkCommandBuffer cb)
{
    if (!s_mtr.active) return 0;
    if (s_mtr.cap && cb == s_mtr.pcb) return 2;
    if (cb == s_mtr.tab_cb) return 1;
    return 0;
}

/* Seal the current chunk: queue it (or record it here when the queue is
 * long or there are no workers) and start the next one. */
static void mtr_seal(void)
{
    mtr_chunk* k = s_mtr.cur;
    if (s_mtr.npass == s_mtr.passcap) {
        const u32 nc = s_mtr.passcap ? s_mtr.passcap * 2u : 64u;
        mtr_chunk** np = (mtr_chunk**)realloc(s_mtr.pass, nc * sizeof *np);
        if (!np) return;                       /* keep growing this chunk */
        s_mtr.pass = np; s_mtr.passcap = nc;
    }
    mtr_chunk* nk = mtr_chunk_new();
    if (!nk) return;
    s_mtr.pass[s_mtr.npass++] = k;
    s_mtr.chunks++;
    VK_AT_ADD(&s_mtr.inflight, 1);
    if (!s_mtr.workers || s_mtr.qn >= 4u * s_mtr.workers) {
        VK_AT_STORE(&k->state, 1);
        mtr_record(k, MTR_MAXW);
        mtr_done(k);
        s_mtr.helped++;
    } else {
        pthread_mutex_lock(&s_mtr.mu);
        if (s_mtr.qt) s_mtr.qt->next = k; else s_mtr.qh = k;
        s_mtr.qt = k;
        k->next = NULL;
        s_mtr.qn++;
        vk_cond_signal(&s_mtr.cv_work);
        pthread_mutex_unlock(&s_mtr.mu);
    }
    s_mtr.cur = nk;
}

/* vkCmdEndRenderPass of a captured pass: assemble it in order. */
static void mtr_pass_finish(void)
{
    VkCommandBuffer pcb = s_mtr.pcb;
    mtr_chunk* last = s_mtr.cur;
    s_mtr.cap = 0;
    s_mtr.cur = NULL;
    if (!s_mtr.npass) {                        /* one chunk: inline */
        s_mtr.r.BeginRenderPass(pcb, &s_mtr.rbi, VK_SUBPASS_CONTENTS_INLINE);
        mtr_replay(pcb, last->buf, last->len);
        s_mtr.r.EndRenderPass(pcb);
        mtr_chunk_free(last);
        s_mtr.inl++;
        return;
    }
    /* the last one: recorded here while the workers finish theirs */
    if (s_mtr.npass == s_mtr.passcap) {
        mtr_chunk** np = (mtr_chunk**)realloc(s_mtr.pass, s_mtr.passcap * 2u * sizeof *np);
        if (np) { s_mtr.pass = np; s_mtr.passcap *= 2u; }
    }
    if (s_mtr.npass == s_mtr.passcap) {        /* out of memory: wait, then the packets inline */
        mtr_wait_idle();
        s_mtr.r.BeginRenderPass(pcb, &s_mtr.rbi, VK_SUBPASS_CONTENTS_INLINE);
        for (u32 i = 0; i < s_mtr.npass; i++) mtr_replay(pcb, s_mtr.pass[i]->buf, s_mtr.pass[i]->len);
        mtr_replay(pcb, last->buf, last->len);
        s_mtr.r.EndRenderPass(pcb);
        for (u32 i = 0; i < s_mtr.npass; i++) mtr_chunk_free(s_mtr.pass[i]);
        mtr_chunk_free(last);
        s_mtr.npass = 0;
        s_mtr.fails++;
        return;
    }
    s_mtr.pass[s_mtr.npass++] = last;
    s_mtr.chunks++;
    VK_AT_ADD(&s_mtr.inflight, 1);
    VK_AT_STORE(&last->state, 1);
    mtr_record(last, MTR_MAXW);
    mtr_done(last);
    for (;;) {                                 /* help out */
        pthread_mutex_lock(&s_mtr.mu);
        mtr_chunk* k = mtr_pop_locked();
        pthread_mutex_unlock(&s_mtr.mu);
        if (!k) break;
        mtr_record(k, MTR_MAXW);
        mtr_done(k);
        s_mtr.helped++;
    }
    int ok = 1;
    pthread_mutex_lock(&s_mtr.mu);
    for (u32 i = 0; i < s_mtr.npass; i++) {
        while (VK_AT_LOAD(&s_mtr.pass[i]->state) != 2) vk_cond_wait(&s_mtr.cv_done, &s_mtr.mu);
        if (!s_mtr.pass[i]->out) ok = 0;
    }
    pthread_mutex_unlock(&s_mtr.mu);
    if (ok) {
        VkCommandBuffer cbs[64];
        s_mtr.r.BeginRenderPass(pcb, &s_mtr.rbi, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
        for (u32 i = 0; i < s_mtr.npass;) {
            u32 m = 0;
            while (m < 64u && i < s_mtr.npass) cbs[m++] = s_mtr.pass[i++]->out;
            s_mtr.r.ExecuteCommands(pcb, m, cbs);
        }
        s_mtr.secondaries += s_mtr.npass;
    } else {                                   /* a recorder failed: the packets inline */
        if (!s_mtr.fails++) VK_LOG("engine: parallel recording: a secondary failed; pass recorded inline\n");
        s_mtr.r.BeginRenderPass(pcb, &s_mtr.rbi, VK_SUBPASS_CONTENTS_INLINE);
        for (u32 i = 0; i < s_mtr.npass; i++) mtr_replay(pcb, s_mtr.pass[i]->buf, s_mtr.pass[i]->len);
    }
    s_mtr.r.EndRenderPass(pcb);
    for (u32 i = 0; i < s_mtr.npass; i++) mtr_chunk_free(s_mtr.pass[i]);
    s_mtr.npass = 0;
    if (ok) {
        /* the primary's state is undefined after vkCmdExecuteCommands; the
         * serial code's shadows assume what it last set, so set it again
         * (state commands are valid outside a render pass) */
        mtr_chunk* k = mtr_chunk_new();
        if (k) { mtr_replay(pcb, k->buf, k->len); mtr_chunk_free(k); }
    }
}

/* RSX_PERF_VK_KICK submits (and so ends the pass) VK_KICK_DRAWS draws after
 * an occlusion query ended. With frequent queries that cuts passes into
 * pieces too short to share out; RSX_PERF_RECORD_MT_KICK=N (opt-in) lets
 * the kick wait N draws while workers record. Default: unchanged timing, so
 * polled query results arrive as soon as with serial recording. */
static u32 vk_mtr_kick_draws(u32 base)
{
    if (!s_mtr.active || !s_mtr.workers || s_mtr.kick <= 0) return base;
    return (u32)s_mtr.kick > base ? (u32)s_mtr.kick : base;
}

/* Test hook: RSX_PERF_RECORD_MT_KICK's value (0: the default timing). */
void rsx_vulkan_backend_record_mt_kick(int draws)
{
    mtr_read_cfg();
    s_mtr.kick = draws > 0 ? draws : 0;
}

/* Top of vk_eng_draw: cut the chunk here once it is long enough. */
static void vk_mtr_draw_point(void)
{
    if (!s_mtr.cap || !s_mtr.cur || s_mtr.cur->ndraw < (u32)s_mtr.chunk || s_mtr.cr_open) return;
    if (!s_mtr.workers) return;                /* packets only: one inline chunk per pass */
    const int q = s_vkq.ok && s_vkq.open != VKQ_NONE;
    if (q && !s_vkq.nready) return;            /* no slot to continue the query in */
    const int ka = s_kick.armed;
    const u32 kq = s_kick.qdraws;
    if (q) vkq_close();
    mtr_seal();
    if (q) { vkq_open(); s_mtr.splits++; }
    s_kick.armed = ka;                         /* as if the query had not been split */
    s_kick.qdraws = kq;
}

/* ---- wrappers -------------------------------------------------------------*/

#define MTR_W_BEGIN(op_, payload_)                                  \
    const int c_ = mtr_hit(cb);                                     \
    u8* a_ = c_ ? mtr_rec(c_ == 2, (op_), (payload_)) : NULL;       \
    if (!a_)
#define MTR_W_TAIL(op_, lo_, hi_, real_call_)                       \
    if (c_ == 1) real_call_;                                        \
    mtr_track((op_), (lo_), (hi_), a_)

static void VKAPI_CALL mtr_w_BindPipeline(VkCommandBuffer cb, VkPipelineBindPoint bp, VkPipeline p)
{
    const int gfx = bp == VK_PIPELINE_BIND_POINT_GRAPHICS;
    MTR_W_BEGIN(gfx ? MTR_PIPE : MTR_PIPE_X, sizeof(mtr_a_pipe)) { s_mtr.r.BindPipeline(cb, bp, p); return; }
    mtr_a_pipe* x = (mtr_a_pipe*)a_;
    x->bp = (u32)bp; x->pad = 0; x->p = p;
    if (!gfx) { if (c_ == 1) s_mtr.r.BindPipeline(cb, bp, p); return; }
    MTR_W_TAIL(MTR_PIPE, 0, 1, s_mtr.r.BindPipeline(cb, bp, p));
}

static void VKAPI_CALL mtr_w_BindVertexBuffers(VkCommandBuffer cb, uint32_t first, uint32_t count,
                                               const VkBuffer* b, const VkDeviceSize* o)
{
    MTR_W_BEGIN(MTR_VB, sizeof(mtr_a_vb) + count * (sizeof *b + sizeof *o)) {
        s_mtr.r.BindVertexBuffers(cb, first, count, b, o); return;
    }
    mtr_a_vb* x = (mtr_a_vb*)a_;
    x->first = first; x->count = count;
    VkBuffer* db = (VkBuffer*)(x + 1);
    memcpy(db, b, count * sizeof *b);
    memcpy(db + count, o, count * sizeof *o);
    MTR_W_TAIL(MTR_VB, first, first + count, s_mtr.r.BindVertexBuffers(cb, first, count, b, o));
}

static void VKAPI_CALL mtr_w_BindDescriptorSets(VkCommandBuffer cb, VkPipelineBindPoint bp, VkPipelineLayout l,
                                                uint32_t first, uint32_t count, const VkDescriptorSet* s,
                                                uint32_t ndyn, const uint32_t* dyn)
{
    const int gfx = bp == VK_PIPELINE_BIND_POINT_GRAPHICS;
    MTR_W_BEGIN(gfx ? MTR_DS : MTR_DS_X, sizeof(mtr_a_ds) + count * sizeof *s + ndyn * sizeof(u32)) {
        s_mtr.r.BindDescriptorSets(cb, bp, l, first, count, s, ndyn, dyn); return;
    }
    mtr_a_ds* x = (mtr_a_ds*)a_;
    x->layout = l; x->bp = (u32)bp; x->first = first; x->count = count; x->ndyn = ndyn;
    VkDescriptorSet* ds = (VkDescriptorSet*)(x + 1);
    memcpy(ds, s, count * sizeof *s);
    if (ndyn) memcpy(ds + count, dyn, ndyn * sizeof(u32));
    if (!gfx) { if (c_ == 1) s_mtr.r.BindDescriptorSets(cb, bp, l, first, count, s, ndyn, dyn); return; }
    MTR_W_TAIL(MTR_DS, first, first + count, s_mtr.r.BindDescriptorSets(cb, bp, l, first, count, s, ndyn, dyn));
}

static void VKAPI_CALL mtr_w_PushConstants(VkCommandBuffer cb, VkPipelineLayout l, VkShaderStageFlags st,
                                           uint32_t off, uint32_t size, const void* v)
{
    const int gfx = (st & VK_SHADER_STAGE_ALL_GRAPHICS) != 0;
    MTR_W_BEGIN(gfx ? MTR_PC : MTR_PC_X, sizeof(mtr_a_pc) + size) {
        s_mtr.r.PushConstants(cb, l, st, off, size, v); return;
    }
    mtr_a_pc* x = (mtr_a_pc*)a_;
    x->layout = l; x->stages = st; x->off = off; x->size = size; x->pad = 0;
    memcpy(x + 1, v, size);
    if (!gfx) { if (c_ == 1) s_mtr.r.PushConstants(cb, l, st, off, size, v); return; }
    MTR_W_TAIL(MTR_PC, off, off + size, s_mtr.r.PushConstants(cb, l, st, off, size, v));
}

static void VKAPI_CALL mtr_w_SetViewport(VkCommandBuffer cb, uint32_t first, uint32_t count, const VkViewport* v)
{
    MTR_W_BEGIN(MTR_VP, sizeof(mtr_a_arr) + count * sizeof *v) { s_mtr.r.SetViewport(cb, first, count, v); return; }
    mtr_a_arr* x = (mtr_a_arr*)a_;
    x->first = first; x->count = count;
    memcpy(x + 1, v, count * sizeof *v);
    MTR_W_TAIL(MTR_VP, first, first + count, s_mtr.r.SetViewport(cb, first, count, v));
}

static void VKAPI_CALL mtr_w_SetScissor(VkCommandBuffer cb, uint32_t first, uint32_t count, const VkRect2D* v)
{
    MTR_W_BEGIN(MTR_SC, sizeof(mtr_a_arr) + count * sizeof *v) { s_mtr.r.SetScissor(cb, first, count, v); return; }
    mtr_a_arr* x = (mtr_a_arr*)a_;
    x->first = first; x->count = count;
    memcpy(x + 1, v, count * sizeof *v);
    MTR_W_TAIL(MTR_SC, first, first + count, s_mtr.r.SetScissor(cb, first, count, v));
}

/* Stream commands: captured, or straight through. */
static void VKAPI_CALL mtr_w_Draw(VkCommandBuffer cb, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    if (!(s_mtr.cap && cb == s_mtr.pcb)) { s_mtr.r.Draw(cb, a, b, c, d); return; }
    mtr_a_draw* x = (mtr_a_draw*)mtr_rec(1, MTR_DRAW, sizeof *x);
    if (!x) { s_mtr.fails++; return; }
    x->a = a; x->b = b; x->c = c; x->d = d;
    s_mtr.cur->ndraw++; s_mtr.draws++;
}

static void VKAPI_CALL mtr_w_DrawIndexed(VkCommandBuffer cb, uint32_t ic, uint32_t inst, uint32_t fi,
                                         int32_t vo, uint32_t finst)
{
    if (!(s_mtr.cap && cb == s_mtr.pcb)) { s_mtr.r.DrawIndexed(cb, ic, inst, fi, vo, finst); return; }
    mtr_a_drawi* x = (mtr_a_drawi*)mtr_rec(1, MTR_DRAWI, sizeof *x);
    if (!x) { s_mtr.fails++; return; }
    x->ic = ic; x->inst = inst; x->fi = fi; x->vo = vo; x->finst = finst;
    s_mtr.cur->ndraw++; s_mtr.draws++;
}

#define MTR_FACE_LO(f) (((f) & VK_STENCIL_FACE_FRONT_BIT) ? 0u : 1u)
#define MTR_FACE_HI(f) (((f) & VK_STENCIL_FACE_BACK_BIT) ? 2u : 1u)
#define MTR_W_FACE(name, op, realf)                                                          \
static void VKAPI_CALL name(VkCommandBuffer cb, VkStencilFaceFlags f, uint32_t v)            \
{                                                                                            \
    MTR_W_BEGIN(op, sizeof(mtr_a_face)) { realf(cb, f, v); return; }                         \
    mtr_a_face* x = (mtr_a_face*)a_;                                                         \
    x->face = f; x->v = v;                                                                   \
    MTR_W_TAIL(op, MTR_FACE_LO(f), MTR_FACE_HI(f), realf(cb, f, v));                         \
}
MTR_W_FACE(mtr_w_SetStencilReference, MTR_SREF, s_mtr.r.SetStencilReference)
MTR_W_FACE(mtr_w_SetStencilWriteMask, MTR_SWM, s_mtr.r.SetStencilWriteMask)
MTR_W_FACE(mtr_w_scmask, MTR_SCM, s_mtr.r.scmask)

static void VKAPI_CALL mtr_w_SetDepthBounds(VkCommandBuffer cb, float a, float b)
{
    MTR_W_BEGIN(MTR_DBND, sizeof(mtr_a_f4)) { s_mtr.r.SetDepthBounds(cb, a, b); return; }
    mtr_a_f4* x = (mtr_a_f4*)a_;
    x->f[0] = a; x->f[1] = b; x->f[2] = x->f[3] = 0.0f;
    MTR_W_TAIL(MTR_DBND, 0, 1, s_mtr.r.SetDepthBounds(cb, a, b));
}

static void VKAPI_CALL mtr_w_SetBlendConstants(VkCommandBuffer cb, const float v[4])
{
    MTR_W_BEGIN(MTR_BLEND, sizeof(mtr_a_f4)) { s_mtr.r.SetBlendConstants(cb, v); return; }
    memcpy(((mtr_a_f4*)a_)->f, v, 4 * sizeof(float));
    MTR_W_TAIL(MTR_BLEND, 0, 1, s_mtr.r.SetBlendConstants(cb, v));
}

static void VKAPI_CALL mtr_w_SetDepthBias(VkCommandBuffer cb, float a, float b, float c)
{
    MTR_W_BEGIN(MTR_DBIAS, sizeof(mtr_a_f4)) { s_mtr.r.SetDepthBias(cb, a, b, c); return; }
    mtr_a_f4* x = (mtr_a_f4*)a_;
    x->f[0] = a; x->f[1] = b; x->f[2] = c; x->f[3] = 0.0f;
    MTR_W_TAIL(MTR_DBIAS, 0, 1, s_mtr.r.SetDepthBias(cb, a, b, c));
}

static void VKAPI_CALL mtr_w_SetLineWidth(VkCommandBuffer cb, float a)
{
    MTR_W_BEGIN(MTR_LW, sizeof(mtr_a_f4)) { s_mtr.r.SetLineWidth(cb, a); return; }
    mtr_a_f4* x = (mtr_a_f4*)a_;
    x->f[0] = a; x->f[1] = x->f[2] = x->f[3] = 0.0f;
    MTR_W_TAIL(MTR_LW, 0, 1, s_mtr.r.SetLineWidth(cb, a));
}

static void VKAPI_CALL mtr_w_BindIndexBuffer(VkCommandBuffer cb, VkBuffer b, VkDeviceSize off, VkIndexType t)
{
    MTR_W_BEGIN(MTR_IB, sizeof(mtr_a_ib)) { s_mtr.r.BindIndexBuffer(cb, b, off, t); return; }
    mtr_a_ib* x = (mtr_a_ib*)a_;
    x->b = b; x->off = off; x->type = (u32)t; x->pad = 0;
    MTR_W_TAIL(MTR_IB, 0, 1, s_mtr.r.BindIndexBuffer(cb, b, off, t));
}

#define MTR_W_U32(name, op, T, realf)                                                        \
static void VKAPI_CALL name(VkCommandBuffer cb, T v)                                         \
{                                                                                            \
    MTR_W_BEGIN(op, sizeof(u32)) { realf(cb, v); return; }                                   \
    *(u32*)a_ = (u32)v;                                                                      \
    MTR_W_TAIL(op, 0, 1, realf(cb, v));                                                      \
}
MTR_W_U32(mtr_w_cull, MTR_CULL, VkCullModeFlags, s_mtr.r.cull)
MTR_W_U32(mtr_w_front, MTR_FRONT, VkFrontFace, s_mtr.r.front)
MTR_W_U32(mtr_w_topo, MTR_TOPO, VkPrimitiveTopology, s_mtr.r.topo)
MTR_W_U32(mtr_w_dtest, MTR_DTEST, VkBool32, s_mtr.r.dtest)
MTR_W_U32(mtr_w_dwrite, MTR_DWRITE, VkBool32, s_mtr.r.dwrite)
MTR_W_U32(mtr_w_dop, MTR_DOP, VkCompareOp, s_mtr.r.dop)
MTR_W_U32(mtr_w_dbt, MTR_DBT, VkBool32, s_mtr.r.dbt)
MTR_W_U32(mtr_w_stest, MTR_STEST, VkBool32, s_mtr.r.stest)

static void VKAPI_CALL mtr_w_sop(VkCommandBuffer cb, VkStencilFaceFlags f, VkStencilOp fail, VkStencilOp pass,
                                 VkStencilOp dfail, VkCompareOp cmp)
{
    MTR_W_BEGIN(MTR_SOP, sizeof(mtr_a_sop)) { s_mtr.r.sop(cb, f, fail, pass, dfail, cmp); return; }
    mtr_a_sop* x = (mtr_a_sop*)a_;
    x->face = f; x->fail = (u32)fail; x->pass = (u32)pass; x->dfail = (u32)dfail; x->cmp = (u32)cmp; x->pad = 0;
    MTR_W_TAIL(MTR_SOP, MTR_FACE_LO(f), MTR_FACE_HI(f), s_mtr.r.sop(cb, f, fail, pass, dfail, cmp));
}

static void VKAPI_CALL mtr_w_ClearAttachments(VkCommandBuffer cb, uint32_t na, const VkClearAttachment* ca,
                                              uint32_t nr, const VkClearRect* cr)
{
    if (!(s_mtr.cap && cb == s_mtr.pcb)) { s_mtr.r.ClearAttachments(cb, na, ca, nr, cr); return; }
    mtr_a_clear* x = (mtr_a_clear*)mtr_rec(1, MTR_CLEAR, sizeof *x + na * sizeof *ca + nr * sizeof *cr);
    if (!x) { s_mtr.fails++; return; }
    x->na = na; x->nr = nr;
    VkClearAttachment* d = (VkClearAttachment*)(x + 1);
    memcpy(d, ca, na * sizeof *ca);
    memcpy(d + na, cr, nr * sizeof *cr);
}

static void VKAPI_CALL mtr_w_PipelineBarrier(VkCommandBuffer cb, VkPipelineStageFlags src, VkPipelineStageFlags dst,
                                             VkDependencyFlags dep, uint32_t nm, const VkMemoryBarrier* m,
                                             uint32_t nb, const VkBufferMemoryBarrier* b,
                                             uint32_t ni, const VkImageMemoryBarrier* im)
{
    if (!(s_mtr.cap && cb == s_mtr.pcb)) { s_mtr.r.PipelineBarrier(cb, src, dst, dep, nm, m, nb, b, ni, im); return; }
    mtr_a_bar* x = (mtr_a_bar*)mtr_rec(1, MTR_BARRIER, sizeof *x + nm * sizeof *m + nb * sizeof *b + ni * sizeof *im);
    if (!x) { s_mtr.fails++; return; }
    x->src = src; x->dst = dst; x->dep = dep; x->nm = nm; x->nb = nb; x->ni = ni;
    VkMemoryBarrier* dm = (VkMemoryBarrier*)(x + 1);
    memcpy(dm, m, nm * sizeof *m);
    VkBufferMemoryBarrier* dbb = (VkBufferMemoryBarrier*)(dm + nm);
    memcpy(dbb, b, nb * sizeof *b);
    memcpy(dbb + nb, im, ni * sizeof *im);
}

static void VKAPI_CALL mtr_w_md(VkCommandBuffer cb, uint32_t n, const VkMultiDrawInfoEXT* di, uint32_t inst,
                                uint32_t first, uint32_t stride)
{
    if (!(s_mtr.cap && cb == s_mtr.pcb)) { s_mtr.r.md(cb, n, di, inst, first, stride); return; }
    mtr_a_md* x = (mtr_a_md*)mtr_rec(1, MTR_MD, sizeof *x + n * sizeof *di);
    if (!x) { s_mtr.fails++; return; }
    x->n = n; x->inst = inst; x->first = first; x->has_vo = 0; x->vo = 0; x->pad = 0;
    VkMultiDrawInfoEXT* d = (VkMultiDrawInfoEXT*)(x + 1);
    for (u32 i = 0; i < n; i++) d[i] = *(const VkMultiDrawInfoEXT*)((const u8*)di + (size_t)i * stride);
    s_mtr.cur->ndraw++; s_mtr.draws++;
}

static void VKAPI_CALL mtr_w_mdi(VkCommandBuffer cb, uint32_t n, const VkMultiDrawIndexedInfoEXT* di, uint32_t inst,
                                 uint32_t first, uint32_t stride, const int32_t* vo)
{
    if (!(s_mtr.cap && cb == s_mtr.pcb)) { s_mtr.r.mdi(cb, n, di, inst, first, stride, vo); return; }
    mtr_a_md* x = (mtr_a_md*)mtr_rec(1, MTR_MDI, sizeof *x + n * sizeof *di);
    if (!x) { s_mtr.fails++; return; }
    x->n = n; x->inst = inst; x->first = first; x->has_vo = vo != NULL; x->vo = vo ? *vo : 0; x->pad = 0;
    VkMultiDrawIndexedInfoEXT* d = (VkMultiDrawIndexedInfoEXT*)(x + 1);
    for (u32 i = 0; i < n; i++) d[i] = *(const VkMultiDrawIndexedInfoEXT*)((const u8*)di + (size_t)i * stride);
    s_mtr.cur->ndraw++; s_mtr.draws++;
}

static void VKAPI_CALL mtr_w_qb(VkCommandBuffer cb, VkQueryPool pool, uint32_t q, VkQueryControlFlags fl)
{
    if (!(s_mtr.cap && cb == s_mtr.pcb)) { s_mtr.r.qb(cb, pool, q, fl); return; }
    mtr_a_q* x = (mtr_a_q*)mtr_rec(1, MTR_QB, sizeof *x);
    if (!x) { s_mtr.fails++; return; }
    x->pool = pool; x->q = q; x->flags = fl;
}

static void VKAPI_CALL mtr_w_qe(VkCommandBuffer cb, VkQueryPool pool, uint32_t q)
{
    if (!(s_mtr.cap && cb == s_mtr.pcb)) { s_mtr.r.qe(cb, pool, q); return; }
    mtr_a_q* x = (mtr_a_q*)mtr_rec(1, MTR_QE, sizeof *x);
    if (!x) { s_mtr.fails++; return; }
    x->pool = pool; x->q = q; x->flags = 0;
}

static void VKAPI_CALL mtr_w_crb(VkCommandBuffer cb, const VkConditionalRenderingBeginInfoEXT* bi)
{
    if (!(s_mtr.cap && cb == s_mtr.pcb)) { s_mtr.r.crb(cb, bi); return; }
    mtr_a_cr* x = (mtr_a_cr*)mtr_rec(1, MTR_CRB, sizeof *x);
    if (!x) { s_mtr.fails++; return; }
    x->b = bi->buffer; x->off = bi->offset; x->flags = bi->flags; x->pad = 0;
    s_mtr.cr_open = 1;
}

static void VKAPI_CALL mtr_w_cre(VkCommandBuffer cb)
{
    if (!(s_mtr.cap && cb == s_mtr.pcb)) { s_mtr.r.cre(cb); return; }
    if (!mtr_rec(1, MTR_CRE, 0)) { s_mtr.fails++; return; }
    s_mtr.cr_open = 0;
}

static void VKAPI_CALL mtr_w_BeginRenderPass(VkCommandBuffer cb, const VkRenderPassBeginInfo* rbi,
                                             VkSubpassContents contents)
{
    if (!s_mtr.active || s_mtr.cap || cb != s_mtr.tab_cb || !s_f.main.open || cb != s_f.main.cb ||
        contents != VK_SUBPASS_CONTENTS_INLINE ||
        rbi->pNext || rbi->clearValueCount > RSX_BE_MAX_COLOR_TARGETS + 2u) {
        s_mtr.r.BeginRenderPass(cb, rbi, contents);
        return;
    }
    s_mtr.rbi = *rbi;
    if (rbi->clearValueCount) {
        memcpy(s_mtr.clr, rbi->pClearValues, rbi->clearValueCount * sizeof *s_mtr.clr);
        s_mtr.rbi.pClearValues = s_mtr.clr;
    }
    s_mtr.cur = mtr_chunk_new();
    if (!s_mtr.cur) { s_mtr.r.BeginRenderPass(cb, rbi, contents); return; }
    s_mtr.pcb = cb;
    s_mtr.cap = 1;
    s_mtr.cr_open = 0;
    s_mtr.passes++;
}

static void VKAPI_CALL mtr_w_EndRenderPass(VkCommandBuffer cb)
{
    if (!(s_mtr.cap && cb == s_mtr.pcb)) { s_mtr.r.EndRenderPass(cb); return; }
    mtr_pass_finish();
}

/* ---- installation ---------------------------------------------------------*/

#define MTR_SWAP(ptr, slot, wrap) \
    do { if ((ptr) && (void*)(ptr) != (void*)(wrap)) { s_mtr.r.slot = (ptr); (ptr) = (wrap); } } while (0)

/* The lazily loaded entry points (queries, conditional rendering). */
static void vk_mtr_rewrap(void)
{
    if (!s_mtr.installed) return;
    MTR_SWAP(s_vkq.CmdBeginQuery, qb, mtr_w_qb);
    MTR_SWAP(s_vkq.CmdEndQuery, qe, mtr_w_qe);
    MTR_SWAP(s_cr.Begin, crb, mtr_w_crb);
    MTR_SWAP(s_cr.End, cre, mtr_w_cre);
}

static void mtr_install(void)
{
    if (s_mtr.installed || !s_vk.device) return;
    s_mtr.r.ExecuteCommands = (PFN_vkCmdExecuteCommands)(void*)pvkGetDeviceProcAddr(s_vk.device, "vkCmdExecuteCommands");
    if (!s_mtr.r.ExecuteCommands) { s_mtr.want = 0; s_mtr.packets = 0; return; }
    MTR_SWAP(pvkCmdBindPipeline, BindPipeline, mtr_w_BindPipeline);
    MTR_SWAP(pvkCmdBindVertexBuffers, BindVertexBuffers, mtr_w_BindVertexBuffers);
    MTR_SWAP(pvkCmdBindDescriptorSets, BindDescriptorSets, mtr_w_BindDescriptorSets);
    MTR_SWAP(pvkCmdPushConstants, PushConstants, mtr_w_PushConstants);
    MTR_SWAP(pvkCmdSetViewport, SetViewport, mtr_w_SetViewport);
    MTR_SWAP(pvkCmdSetScissor, SetScissor, mtr_w_SetScissor);
    MTR_SWAP(pvkCmdDraw, Draw, mtr_w_Draw);
    MTR_SWAP(pvkCmdDrawIndexed, DrawIndexed, mtr_w_DrawIndexed);
    MTR_SWAP(pvkCmdSetStencilReference, SetStencilReference, mtr_w_SetStencilReference);
    MTR_SWAP(pvkCmdSetDepthBounds, SetDepthBounds, mtr_w_SetDepthBounds);
    MTR_SWAP(pvkCmdSetBlendConstants, SetBlendConstants, mtr_w_SetBlendConstants);
    MTR_SWAP(pvkCmdSetDepthBias, SetDepthBias, mtr_w_SetDepthBias);
    MTR_SWAP(pvkCmdSetLineWidth, SetLineWidth, mtr_w_SetLineWidth);
    MTR_SWAP(pvkCmdSetStencilWriteMask, SetStencilWriteMask, mtr_w_SetStencilWriteMask);
    MTR_SWAP(pvkCmdBindIndexBuffer, BindIndexBuffer, mtr_w_BindIndexBuffer);
    MTR_SWAP(pvkCmdClearAttachments, ClearAttachments, mtr_w_ClearAttachments);
    MTR_SWAP(pvkCmdPipelineBarrier, PipelineBarrier, mtr_w_PipelineBarrier);
    MTR_SWAP(pvkCmdBeginRenderPass, BeginRenderPass, mtr_w_BeginRenderPass);
    MTR_SWAP(pvkCmdEndRenderPass, EndRenderPass, mtr_w_EndRenderPass);
    if (s_eds.swmask) s_eds.swmask = mtr_w_SetStencilWriteMask;   /* the same command */
    MTR_SWAP(s_eds.cull, cull, mtr_w_cull);
    MTR_SWAP(s_eds.front, front, mtr_w_front);
    MTR_SWAP(s_eds.topo, topo, mtr_w_topo);
    MTR_SWAP(s_eds.dtest, dtest, mtr_w_dtest);
    MTR_SWAP(s_eds.dwrite, dwrite, mtr_w_dwrite);
    MTR_SWAP(s_eds.dop, dop, mtr_w_dop);
    MTR_SWAP(s_eds.dbt, dbt, mtr_w_dbt);
    MTR_SWAP(s_eds.stest, stest, mtr_w_stest);
    MTR_SWAP(s_eds.sop, sop, mtr_w_sop);
    MTR_SWAP(s_eds.scmask, scmask, mtr_w_scmask);
    MTR_SWAP(s_md.draw, md, mtr_w_md);
    MTR_SWAP(s_md.draw_idx, mdi, mtr_w_mdi);
    s_mtr.installed = 1;
    vk_mtr_rewrap();
}

static void vk_mtr_device_loaded(void)
{
    mtr_read_cfg();
    if (!s_mtr.packets) return;
    mtr_install();
    if (s_mtr.installed)
        VK_LOG("engine: parallel recording (RSX_PERF_RECORD_MT): %d worker(s), %d draws per chunk\n",
               s_mtr.want, s_mtr.chunk);
}

/* vk_rec_get began the frame's main command buffer: tracking starts over,
 * and a pending configuration change applies from here. */
static void vk_mtr_main_begun(VkCommandBuffer cb)
{
    if (!s_mtr.installed) { s_mtr.active = 0; return; }
    if (s_mtr.pend_want >= 0) { s_mtr.want = s_mtr.pend_want; s_mtr.pend_want = -1; }
    if (s_mtr.pend_chunk > 0) { s_mtr.chunk = s_mtr.pend_chunk; s_mtr.pend_chunk = -1; }
    if (s_mtr.pend_packets >= 0) { s_mtr.packets = s_mtr.pend_packets; s_mtr.pend_packets = -1; }
    s_mtr.active = s_mtr.packets;
    s_mtr.workers = (u32)s_mtr.want;
    if (s_mtr.workers) mtr_threads_up(s_mtr.workers);
    if (s_mtr.workers > s_mtr.nth) s_mtr.workers = s_mtr.nth;
    s_mtr.tab_cb = cb;
    s_vkq_ready_target = (s_mtr.active && s_mtr.workers) ? VKQ_READY_MT : VKQ_READY;
    mtr_tab_reset();
}

/* Frame slot `slot` retired: its secondaries are free again. */
static void vk_mtr_retire(u32 slot)
{
    if (!s_mtr.installed || slot >= VK_FRAMES_MAX) return;
    mtr_wait_idle();
    for (u32 w = 0; w <= MTR_MAXW; w++) {
        mtr_pool* P = &s_mtr.pools[w][slot];
        if (P->pool && P->used) pvkResetCommandPool(s_vk.device, P->pool, 0);
        P->used = 0;
    }
}

static void vk_mtr_shutdown(void)
{
    if (s_mtr.mu_ok) {
        mtr_wait_idle();
        pthread_mutex_lock(&s_mtr.mu);
        s_mtr.stop = 1;
        vk_cond_broadcast(&s_mtr.cv_work);
        pthread_mutex_unlock(&s_mtr.mu);
        for (u32 i = 0; i < s_mtr.nth; i++) vk_thread_join(s_mtr.th[i]);
        s_mtr.nth = 0;
        s_mtr.stop = 0;
    }
    for (u32 w = 0; w <= MTR_MAXW; w++)
        for (u32 s = 0; s < VK_FRAMES_MAX; s++) {
            mtr_pool* P = &s_mtr.pools[w][s];
            if (P->pool && s_vk.device) pvkDestroyCommandPool(s_vk.device, P->pool, NULL);
            free(P->cb);
            memset(P, 0, sizeof *P);
        }
    s_mtr.active = 0;
    s_mtr.cap = 0;
    s_mtr.installed = 0;                       /* the device's entry points are reloaded */
}

/* Test / tooling hook, applied when the next main command buffer begins.
 * workers: N > 0 parallel recording with N workers; 0 off (the serial path,
 * nothing captured); -2 capture with inline replay (packets, no workers);
 * -1 back to the environment's configuration. chunk: draws per chunk (<= 0
 * keep). Returns the workers configured, -1 when unavailable. */
int rsx_vulkan_backend_record_mt_config(int workers, int chunk)
{
    mtr_read_cfg();
    if (!s_mtr.installed && (workers > 0 || workers == -2)) mtr_install();
    if (!s_mtr.installed) return workers == 0 || workers == -1 ? 0 : -1;
    if (workers == -1) {
        s_mtr.pend_want = s_mtr.env_want; s_mtr.pend_packets = s_mtr.env_packets;
        if (chunk <= 0) s_mtr.pend_chunk = s_mtr.env_chunk;
    } else if (workers == -2) {
        s_mtr.pend_want = 0; s_mtr.pend_packets = 1;
    } else {
        s_mtr.pend_want = workers > (int)MTR_MAXW ? (int)MTR_MAXW : (workers < 0 ? 0 : workers);
        s_mtr.pend_packets = s_mtr.pend_want > 0;
    }
    if (chunk > 0) s_mtr.pend_chunk = chunk < 8 ? 8 : (chunk > 1024 ? 1024 : chunk);
    return s_mtr.pend_want;
}

/* passes captured, passes inline, chunks, draws captured, chunks recorded by
 * the engine thread, secondaries executed, query splits, failures. */
void rsx_vulkan_backend_record_mt_stats(uint64_t out[8])
{
    out[0] = s_mtr.passes; out[1] = s_mtr.inl; out[2] = s_mtr.chunks; out[3] = s_mtr.draws;
    out[4] = s_mtr.helped; out[5] = s_mtr.secondaries; out[6] = s_mtr.splits; out[7] = s_mtr.fails;
}
