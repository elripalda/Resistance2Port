#include "rsx_vk_shaderdb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
typedef SRWLOCK            sdb_mutex;
typedef CONDITION_VARIABLE sdb_cond;
typedef HANDLE             sdb_thread;
#define sdb_mutex_init(m)   InitializeSRWLock(m)
#define sdb_mutex_free(m)   ((void)0)
#define sdb_lock(m)         AcquireSRWLockExclusive(m)
#define sdb_unlock(m)       ReleaseSRWLockExclusive(m)
#define sdb_cond_init(c)    InitializeConditionVariable(c)
#define sdb_cond_free(c)    ((void)0)
#define sdb_cond_wait(c, m) SleepConditionVariableSRW((c), (m), INFINITE, 0)
#define sdb_cond_wake(c)    WakeAllConditionVariable(c)
#else
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
typedef pthread_mutex_t sdb_mutex;
typedef pthread_cond_t  sdb_cond;
typedef pthread_t       sdb_thread;
#define sdb_mutex_init(m)   pthread_mutex_init((m), NULL)
#define sdb_mutex_free(m)   pthread_mutex_destroy(m)
#define sdb_lock(m)         pthread_mutex_lock(m)
#define sdb_unlock(m)       pthread_mutex_unlock(m)
#define sdb_cond_init(c)    pthread_cond_init((c), NULL)
#define sdb_cond_free(c)    pthread_cond_destroy(c)
#define sdb_cond_wait(c, m) pthread_cond_wait((c), (m))
#define sdb_cond_wake(c)    pthread_cond_broadcast(c)
#endif

#define SDB_HDR_BYTES   24u
#define SDB_REC_BYTES   16u
#define SDB_PIPE_FIXED  64u
#define SDB_SHADER_FIXED 16u
#define SDB_MAX_PAYLOAD (64u << 20)

uint64_t rsx_sdb_hash(const void* p, size_t n) { return vkps_fnv(VKPS_FNV_BASIS, p, n); }

static uint32_t sdb_sum(const void* p, size_t n)
{
    const uint64_t h = vkps_fnv(VKPS_FNV_BASIS ^ 0x5244u, p, n);
    return (uint32_t)(h ^ (h >> 32));
}

static uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t rd64(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }
static uint8_t* wr32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); return p + 4; }
static uint8_t* wr64(uint8_t* p, uint64_t v) { memcpy(p, &v, 8); return p + 8; }

void rsx_sdb_thread_name(const wchar_t* name)
{
#ifdef _WIN32
    typedef HRESULT (WINAPI *set_desc_fn)(HANDLE, PCWSTR);
    static set_desc_fn fn;
    static volatile LONG looked;
    if (!looked) {
        HMODULE k = GetModuleHandleW(L"kernel32.dll");
        fn = k ? (set_desc_fn)(void*)GetProcAddress(k, "SetThreadDescription") : NULL;
        InterlockedExchange(&looked, 1);
    }
    if (fn && name) fn(GetCurrentThread(), name);
#else
    (void)name;
#endif
}

uint64_t rsx_sdb_now_us(void)
{
#ifdef _WIN32
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e6 / (double)f.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
#endif
}

/* ---- in-memory database --------------------------------------------------- */

const rsx_sdb_shader* rsx_sdb_find_shader(const rsx_sdb* db, uint64_t hash)
{
    const uint64_t* v = vkps_map_get(&db->sidx, vkps_key(hash));
    return v ? &db->s[*v - 1u] : NULL;
}

rsx_sdb_pipe* rsx_sdb_find_pipe(const rsx_sdb* db, uint64_t key)
{
    const uint64_t* v = vkps_map_get(&db->pidx, vkps_key(key));
    return v ? &db->p[*v - 1u] : NULL;
}

const rsx_sdb_shader* rsx_sdb_add_shader(rsx_sdb* db, uint64_t hash, uint32_t stage,
                                         const char* text, uint32_t len)
{
    const rsx_sdb_shader* have = rsx_sdb_find_shader(db, hash);
    if (have) return have;
    if (db->ns == db->cs) {
        const uint32_t c = db->cs ? db->cs * 2u : 64u;
        rsx_sdb_shader* s = (rsx_sdb_shader*)realloc(db->s, (size_t)c * sizeof *s);
        if (!s) return NULL;
        db->s = s;
        db->cs = c;
    }
    char* t = (char*)malloc((size_t)len + 1u);
    if (!t) return NULL;
    memcpy(t, text, len);
    t[len] = 0;
    uint64_t* slot = vkps_map_put(&db->sidx, vkps_key(hash));
    if (!slot) { free(t); return NULL; }
    rsx_sdb_shader* s = &db->s[db->ns++];
    s->hash = hash;
    s->stage = stage;
    s->len = len;
    s->text = t;
    *slot = db->ns;
    return s;
}

rsx_sdb_pipe* rsx_sdb_add_pipe(rsx_sdb* db, const rsx_sdb_pipe* p)
{
    rsx_sdb_pipe* have = rsx_sdb_find_pipe(db, p->key);
    if (have) { have->bits |= p->bits; return have; }
    if (db->np == db->cp) {
        const uint32_t c = db->cp ? db->cp * 2u : 64u;
        rsx_sdb_pipe* n = (rsx_sdb_pipe*)realloc(db->p, (size_t)c * sizeof *n);
        if (!n) return NULL;
        db->p = n;
        db->cp = c;
    }
    const size_t bl = (size_t)p->rs_len + p->layout_len + p->vp_len + p->fp_len;
    uint8_t* b = (uint8_t*)malloc(bl ? bl : 1u);
    if (!b) return NULL;
    if (bl) memcpy(b, p->blob, bl);
    uint64_t* slot = vkps_map_put(&db->pidx, vkps_key(p->key));
    if (!slot) { free(b); return NULL; }
    rsx_sdb_pipe* d = &db->p[db->np++];
    *d = *p;
    d->blob = b;
    *slot = db->np;
    return d;
}

void rsx_sdb_free(rsx_sdb* db)
{
    for (uint32_t i = 0; i < db->np; i++) free(db->p[i].blob);
    for (uint32_t i = 0; i < db->ns; i++) free(db->s[i].text);
    free(db->p);
    free(db->s);
    vkps_map_free(&db->pidx);
    vkps_map_free(&db->sidx);
    memset(db, 0, sizeof *db);
}

uint32_t rsx_sdb_merge(rsx_sdb* dst, const rsx_sdb* src)
{
    uint32_t added = 0;
    for (uint32_t i = 0; i < src->ns; i++)
        rsx_sdb_add_shader(dst, src->s[i].hash, src->s[i].stage, src->s[i].text, src->s[i].len);
    for (uint32_t i = 0; i < src->np; i++) {
        const int had = rsx_sdb_find_pipe(dst, src->p[i].key) != NULL;
        if (rsx_sdb_add_pipe(dst, &src->p[i]) && !had) added++;
    }
    return added;
}

/* ---- records -------------------------------------------------------------- */

static void sdb_header(uint8_t h[SDB_HDR_BYTES], uint64_t settings)
{
    uint8_t* p = h;
    p = wr32(p, RSX_SDB_MAGIC);
    p = wr32(p, RSX_SDB_VERSION);
    p = wr64(p, settings);
    p = wr32(p, 0);
    wr32(p, 0);
}

/* One record (header + payload) in a malloc'd buffer. */
static uint8_t* sdb_rec(uint32_t type, uint32_t len, uint8_t** payload)
{
    uint8_t* r = (uint8_t*)malloc((size_t)SDB_REC_BYTES + len);
    if (!r) return NULL;
    wr32(r, RSX_SDB_REC_MAGIC);
    wr32(r + 4, type);
    wr32(r + 8, len);
    *payload = r + SDB_REC_BYTES;
    return r;
}
static void sdb_rec_seal(uint8_t* r) { wr32(r + 12, sdb_sum(r + SDB_REC_BYTES, rd32(r + 8))); }

static uint8_t* sdb_rec_shader(uint64_t hash, uint32_t stage, const char* text, uint32_t len, uint32_t* out_len)
{
    uint8_t* pl;
    uint8_t* r = sdb_rec(RSX_SDB_REC_SHADER, SDB_SHADER_FIXED + len, &pl);
    if (!r) return NULL;
    pl = wr64(pl, hash);
    pl = wr32(pl, stage);
    pl = wr32(pl, len);
    memcpy(pl, text, len);
    sdb_rec_seal(r);
    *out_len = SDB_REC_BYTES + SDB_SHADER_FIXED + len;
    return r;
}

static uint8_t* sdb_rec_pipe(const rsx_sdb_pipe* p, uint32_t* out_len)
{
    const uint32_t bl = p->rs_len + p->layout_len + p->vp_len + p->fp_len;
    uint8_t* pl;
    uint8_t* r = sdb_rec(RSX_SDB_REC_PIPELINE, SDB_PIPE_FIXED + bl, &pl);
    if (!r) return NULL;
    pl = wr64(pl, p->key);
    pl = wr64(pl, p->vs_hash);
    pl = wr64(pl, p->ps_hash);
    pl = wr64(pl, p->bits);
    pl = wr32(pl, p->stride);
    pl = wr32(pl, p->rt_fmt);
    pl = wr32(pl, p->rt_count);
    pl = wr32(pl, p->flags);
    pl = wr32(pl, p->rs_len);
    pl = wr32(pl, p->layout_len);
    pl = wr32(pl, p->vp_len);
    pl = wr32(pl, p->fp_len);
    if (bl) memcpy(pl, p->blob, bl);
    sdb_rec_seal(r);
    *out_len = SDB_REC_BYTES + SDB_PIPE_FIXED + bl;
    return r;
}

static uint8_t* sdb_rec_variants(uint64_t key, uint64_t bits, uint32_t* out_len)
{
    uint8_t* pl;
    uint8_t* r = sdb_rec(RSX_SDB_REC_VARIANTS, 16u, &pl);
    if (!r) return NULL;
    pl = wr64(pl, key);
    wr64(pl, bits);
    sdb_rec_seal(r);
    *out_len = SDB_REC_BYTES + 16u;
    return r;
}

/* ---- load ----------------------------------------------------------------- */

static uint8_t* sdb_read_file(const char* path, size_t* n)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t* d = NULL;
    if (!fseek(f, 0, SEEK_END)) {
        const long len = ftell(f);
        if (len >= 0 && !fseek(f, 0, SEEK_SET) && (d = (uint8_t*)malloc((size_t)len + 1u))) {
            *n = fread(d, 1, (size_t)len, f);
            if (*n != (size_t)len) { free(d); d = NULL; }
        }
    }
    fclose(f);
    return d;
}

int rsx_sdb_load(rsx_sdb* db, const char* path, uint64_t settings, rsx_sdb_load_info* info)
{
    rsx_sdb_load_info li;
    memset(&li, 0, sizeof li);
    size_t n = 0;
    uint8_t* d = sdb_read_file(path, &n);
    if (!d) {
        li.status = RSX_SDB_MISSING;
    } else if (n < SDB_HDR_BYTES || rd32(d) != RSX_SDB_MAGIC) {
        li.status = RSX_SDB_BAD_HEADER;
    } else {
        li.version = rd32(d + 4);
        li.settings = rd64(d + 8);
        if (li.version != RSX_SDB_VERSION) li.status = RSX_SDB_VERSION_MISMATCH;
        else if (settings != RSX_SDB_ANY_SETTINGS && li.settings != settings) li.status = RSX_SDB_SETTINGS_MISMATCH;
        else {
            size_t at = SDB_HDR_BYTES;
            li.status = RSX_SDB_OK;
            while (at < n) {
                if (n - at < SDB_REC_BYTES) { li.status = RSX_SDB_TRUNCATED; break; }
                const uint8_t* r = d + at;
                const uint32_t type = rd32(r + 4), len = rd32(r + 8);
                if (rd32(r) != RSX_SDB_REC_MAGIC || len > SDB_MAX_PAYLOAD || len > n - at - SDB_REC_BYTES ||
                    rd32(r + 12) != sdb_sum(r + SDB_REC_BYTES, len)) { li.status = RSX_SDB_TRUNCATED; break; }
                const uint8_t* pl = r + SDB_REC_BYTES;
                int ok = 1;
                if (type == RSX_SDB_REC_SHADER) {
                    ok = len >= SDB_SHADER_FIXED && rd32(pl + 12) == len - SDB_SHADER_FIXED &&
                         rsx_sdb_add_shader(db, rd64(pl), rd32(pl + 8), (const char*)pl + SDB_SHADER_FIXED,
                                            len - SDB_SHADER_FIXED) != NULL;
                    li.shaders += ok;
                } else if (type == RSX_SDB_REC_PIPELINE) {
                    rsx_sdb_pipe p;
                    memset(&p, 0, sizeof p);
                    if (len < SDB_PIPE_FIXED) ok = 0;
                    else {
                        p.key = rd64(pl); p.vs_hash = rd64(pl + 8); p.ps_hash = rd64(pl + 16); p.bits = rd64(pl + 24);
                        p.stride = rd32(pl + 32); p.rt_fmt = rd32(pl + 36); p.rt_count = rd32(pl + 40); p.flags = rd32(pl + 44);
                        p.rs_len = rd32(pl + 48); p.layout_len = rd32(pl + 52); p.vp_len = rd32(pl + 56); p.fp_len = rd32(pl + 60);
                        const uint64_t bl = (uint64_t)p.rs_len + p.layout_len + p.vp_len + p.fp_len;
                        p.blob = (uint8_t*)pl + SDB_PIPE_FIXED;
                        ok = bl == (uint64_t)len - SDB_PIPE_FIXED && rsx_sdb_add_pipe(db, &p) != NULL;
                    }
                    li.pipelines += ok;
                } else if (type == RSX_SDB_REC_VARIANTS) {
                    rsx_sdb_pipe* p = len == 16u ? rsx_sdb_find_pipe(db, rd64(pl)) : NULL;
                    ok = len == 16u;
                    if (p) p->bits |= rd64(pl + 8);
                }   /* unknown types with a good checksum: a newer minor writer, skipped */
                if (!ok) { li.status = RSX_SDB_TRUNCATED; break; }
                at += SDB_REC_BYTES + len;
                li.records++;
            }
            li.good_end = (long)at;
        }
    }
    free(d);
    if (info) *info = li;
    return li.status;
}

int rsx_sdb_save(const rsx_sdb* db, const char* path, uint64_t settings)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    if (!f) return -1;
    uint8_t h[SDB_HDR_BYTES];
    sdb_header(h, settings);
    int ok = fwrite(h, sizeof h, 1, f) == 1;
    for (uint32_t i = 0; ok && i < db->ns; i++) {
        uint32_t n;
        uint8_t* r = sdb_rec_shader(db->s[i].hash, db->s[i].stage, db->s[i].text, db->s[i].len, &n);
        ok = r && fwrite(r, 1, n, f) == n;
        free(r);
    }
    for (uint32_t i = 0; ok && i < db->np; i++) {
        uint32_t n;
        uint8_t* r = sdb_rec_pipe(&db->p[i], &n);
        ok = r && fwrite(r, 1, n, f) == n;
        free(r);
    }
    ok = !fclose(f) && ok;
    if (!ok) { remove(tmp); return -1; }
    remove(path);
    return rename(tmp, path) ? -1 : 0;
}

/* ---- background writer ---------------------------------------------------- */

typedef struct sdb_node { struct sdb_node* next; uint32_t len; uint8_t* data; } sdb_node;

struct rsx_sdb_writer {
    FILE*      f;
    sdb_mutex  mu;
    sdb_cond   cv, idle;
    sdb_node  *head, *tail;
    int        stop, busy;
    sdb_thread th;
    int        has_thread;
    vkps_map   keys;      /* pipeline key -> variant bits on file or queued */
    vkps_map   shaders;   /* shader hash -> 1 */
    uint64_t   bytes;
};

#ifdef _WIN32
static DWORD WINAPI sdb_writer_main(LPVOID arg)
#else
static void* sdb_writer_main(void* arg)
#endif
{
    rsx_sdb_writer* w = (rsx_sdb_writer*)arg;
    rsx_sdb_thread_name(L"RSX shaderdb writer");
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif
    sdb_lock(&w->mu);
    for (;;) {
        while (!w->head && !w->stop) sdb_cond_wait(&w->cv, &w->mu);
        if (!w->head) break;                       /* stop with nothing left */
        sdb_node* n = w->head;
        w->head = w->tail = NULL;
        w->busy = 1;
        sdb_unlock(&w->mu);
        uint64_t wrote = 0;
        while (n) {
            sdb_node* nx = n->next;
            if (w->f && fwrite(n->data, 1, n->len, w->f) == n->len) wrote += n->len;
            free(n->data);
            free(n);
            n = nx;
        }
        if (w->f) fflush(w->f);
        sdb_lock(&w->mu);
        w->bytes += wrote;
        w->busy = 0;
        sdb_cond_wake(&w->idle);
    }
    sdb_unlock(&w->mu);
    return 0;
}

static void sdb_writer_push(rsx_sdb_writer* w, uint8_t* data, uint32_t len)
{
    if (!data) return;
    sdb_node* n = (sdb_node*)malloc(sizeof *n);
    if (!n) { free(data); return; }
    n->next = NULL;
    n->len = len;
    n->data = data;
    if (w->tail) w->tail->next = n; else w->head = n;
    w->tail = n;
    sdb_cond_wake(&w->cv);
}

static int sdb_truncate(FILE* f, long at)
{
#ifdef _WIN32
    return _chsize_s(_fileno(f), (__int64)at) ? -1 : 0;
#else
    return ftruncate(fileno(f), (off_t)at) ? -1 : 0;
#endif
}

rsx_sdb_writer* rsx_sdb_writer_open(const char* path, uint64_t settings, const rsx_sdb* known,
                                    const rsx_sdb_load_info* info)
{
    rsx_sdb_writer* w = (rsx_sdb_writer*)calloc(1, sizeof *w);
    if (!w) return NULL;
    const int st = info ? info->status : RSX_SDB_MISSING;
    FILE* f = NULL;
    if (st == RSX_SDB_OK || st == RSX_SDB_TRUNCATED) {
        f = fopen(path, "r+b");
        if (f && st == RSX_SDB_TRUNCATED && sdb_truncate(f, info->good_end)) { fclose(f); f = NULL; }
        if (f && fseek(f, 0, SEEK_END)) { fclose(f); f = NULL; }
    }
    if (!f) {
        known = NULL;                              /* started again: nothing is on file */
        f = fopen(path, "wb");
        if (f) {
            uint8_t h[SDB_HDR_BYTES];
            sdb_header(h, settings);
            if (fwrite(h, sizeof h, 1, f) != 1) { fclose(f); f = NULL; }
            else fflush(f);
        }
    }
    if (!f) { free(w); return NULL; }
    w->f = f;
    if (known) {
        for (uint32_t i = 0; i < known->np; i++) {
            uint64_t* v = vkps_map_put(&w->keys, vkps_key(known->p[i].key));
            if (v) *v |= known->p[i].bits;
        }
        for (uint32_t i = 0; i < known->ns; i++) {
            uint64_t* v = vkps_map_put(&w->shaders, vkps_key(known->s[i].hash));
            if (v) *v = 1;
        }
    }
    sdb_mutex_init(&w->mu);
    sdb_cond_init(&w->cv);
    sdb_cond_init(&w->idle);
#ifdef _WIN32
    w->th = CreateThread(NULL, 256u << 10, sdb_writer_main, w, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    w->has_thread = w->th != NULL;
#else
    w->has_thread = pthread_create(&w->th, NULL, sdb_writer_main, w) == 0;
#endif
    return w;
}

/* Without a thread (creation failed) records are written inline. */
static void sdb_writer_kick(rsx_sdb_writer* w)
{
    if (w->has_thread || !w->head) return;
    for (sdb_node* n = w->head; n;) {
        sdb_node* nx = n->next;
        if (fwrite(n->data, 1, n->len, w->f) == n->len) w->bytes += n->len;
        free(n->data);
        free(n);
        n = nx;
    }
    w->head = w->tail = NULL;
    fflush(w->f);
}

void rsx_sdb_writer_pipeline(rsx_sdb_writer* w, uint64_t key, const char* vs, uint32_t vs_len,
                             const char* ps, uint32_t ps_len, uint32_t stride, uint32_t rt_fmt,
                             uint32_t rt_count, uint32_t flags, const void* rs, uint32_t rs_len,
                             const void* layout, uint32_t layout_len, const void* vp, uint32_t vp_len,
                             const void* fp, uint32_t fp_len, uint64_t bits)
{
    if (!w || !key || !vs || !ps) return;
    sdb_lock(&w->mu);
    uint64_t* have = vkps_map_get(&w->keys, vkps_key(key));
    if (have) {
        const uint64_t nb = bits & ~*have;
        if (nb) {
            uint32_t n;
            *have |= nb;
            uint8_t* r = sdb_rec_variants(key, nb, &n);
            sdb_writer_push(w, r, n);
        }
        sdb_writer_kick(w);
        sdb_unlock(&w->mu);
        return;
    }
    uint64_t* slot = vkps_map_put(&w->keys, vkps_key(key));
    if (!slot) { sdb_unlock(&w->mu); return; }
    *slot = bits;
    const char* txt[2] = { vs, ps };
    const uint32_t tl[2] = { vs_len, ps_len };
    uint64_t th[2];
    for (uint32_t s = 0; s < 2; s++) {
        th[s] = rsx_sdb_hash(txt[s], tl[s]) ^ (uint64_t)s;
        uint64_t* sv = vkps_map_put(&w->shaders, vkps_key(th[s]));
        if (sv && !*sv) {
            uint32_t n = 0;
            *sv = 1;
            uint8_t* r = sdb_rec_shader(th[s], s, txt[s], tl[s], &n);
            sdb_writer_push(w, r, n);
        }
    }
    const uint32_t bl = rs_len + layout_len + vp_len + fp_len;
    uint8_t* blob = (uint8_t*)malloc(bl ? bl : 1u);
    if (blob) {
        uint8_t* b = blob;
        if (rs_len) memcpy(b, rs, rs_len);
        b += rs_len;
        if (layout_len) memcpy(b, layout, layout_len);
        b += layout_len;
        if (vp_len) memcpy(b, vp, vp_len);
        b += vp_len;
        if (fp_len) memcpy(b, fp, fp_len);
        const rsx_sdb_pipe p = { key, th[0], th[1], bits, stride, rt_fmt, rt_count, flags,
                                 rs_len, layout_len, vp_len, fp_len, blob };
        uint32_t n = 0;
        uint8_t* r = sdb_rec_pipe(&p, &n);
        sdb_writer_push(w, r, n);
        free(blob);
    }
    sdb_writer_kick(w);
    sdb_unlock(&w->mu);
}

void rsx_sdb_writer_variants(rsx_sdb_writer* w, uint64_t key, uint64_t bits)
{
    if (!w || !key || !bits) return;
    sdb_lock(&w->mu);
    uint64_t* have = vkps_map_get(&w->keys, vkps_key(key));
    /* Variants of a pipeline never recorded have nothing to attach to. */
    if (have && (bits & ~*have)) {
        uint32_t n = 0;
        const uint64_t nb = bits & ~*have;
        *have |= nb;
        uint8_t* r = sdb_rec_variants(key, nb, &n);
        sdb_writer_push(w, r, n);
        sdb_writer_kick(w);
    }
    sdb_unlock(&w->mu);
}

void rsx_sdb_writer_sync(rsx_sdb_writer* w)
{
    if (!w) return;
    sdb_lock(&w->mu);
    while (w->has_thread && (w->head || w->busy)) sdb_cond_wait(&w->idle, &w->mu);
    sdb_unlock(&w->mu);
}

uint64_t rsx_sdb_writer_bytes(const rsx_sdb_writer* w) { return w ? w->bytes : 0; }

void rsx_sdb_writer_close(rsx_sdb_writer* w)
{
    if (!w) return;
    sdb_lock(&w->mu);
    w->stop = 1;
    sdb_cond_wake(&w->cv);
    sdb_unlock(&w->mu);
    if (w->has_thread) {
#ifdef _WIN32
        WaitForSingleObject(w->th, INFINITE);
        CloseHandle(w->th);
#else
        pthread_join(w->th, NULL);
#endif
    }
    sdb_writer_kick(w);
    for (sdb_node* n = w->head; n;) { sdb_node* nx = n->next; free(n->data); free(n); n = nx; }
    if (w->f) fclose(w->f);
    vkps_map_free(&w->keys);
    vkps_map_free(&w->shaders);
    sdb_cond_free(&w->cv);
    sdb_cond_free(&w->idle);
    sdb_mutex_free(&w->mu);
    free(w);
}

/* ---- merge tool ----------------------------------------------------------- */
#ifdef RSX_SDB_TOOL
static const char* sdb_status_name(int s)
{
    static const char* n[] = { "ok", "missing", "not a pipeline db", "version mismatch",
                               "settings mismatch", "truncated (tail ignored)" };
    return (s >= 0 && s <= RSX_SDB_TRUNCATED) ? n[s] : "?";
}

int main(int argc, char** argv)
{
    if (argc >= 3 && !strcmp(argv[1], "info")) {
        for (int i = 2; i < argc; i++) {
            rsx_sdb db;
            memset(&db, 0, sizeof db);
            rsx_sdb_load_info li;
            rsx_sdb_load(&db, argv[i], RSX_SDB_ANY_SETTINGS, &li);
            printf("%s: %s, version %u, settings %016llx, %u pipelines, %u programs, %u records\n",
                   argv[i], sdb_status_name(li.status), li.version, (unsigned long long)li.settings,
                   db.np, db.ns, li.records);
            rsx_sdb_free(&db);
        }
        return 0;
    }
    if (argc < 4 || strcmp(argv[1], "merge")) {
        fprintf(stderr, "usage: %s merge <out.db> <in.db>...   (inputs with other settings are skipped)\n"
                        "       %s info <db>...\n", argv[0], argv[0]);
        return 2;
    }
    rsx_sdb out;
    memset(&out, 0, sizeof out);
    uint64_t settings = 0;
    int have = 0, used = 0;
    for (int i = 3; i < argc; i++) {
        rsx_sdb db;
        memset(&db, 0, sizeof db);
        rsx_sdb_load_info li;
        rsx_sdb_load(&db, argv[i], have ? settings : RSX_SDB_ANY_SETTINGS, &li);
        if (li.status != RSX_SDB_OK && li.status != RSX_SDB_TRUNCATED) {
            printf("skip  %s: %s (file %016llx, merge %016llx)\n", argv[i], sdb_status_name(li.status),
                   (unsigned long long)li.settings, (unsigned long long)settings);
            rsx_sdb_free(&db);
            continue;
        }
        if (!have) { settings = li.settings; have = 1; }
        const uint32_t added = rsx_sdb_merge(&out, &db);
        printf("merge %s: %u pipelines (%u new)%s\n", argv[i], db.np, added,
               li.status == RSX_SDB_TRUNCATED ? ", damaged tail ignored" : "");
        rsx_sdb_free(&db);
        used++;
    }
    if (!used) { fprintf(stderr, "nothing to merge\n"); rsx_sdb_free(&out); return 1; }
    const int rc = rsx_sdb_save(&out, argv[2], settings);
    printf("%s %s: %u pipelines, %u programs, settings %016llx\n", rc ? "FAILED writing" : "wrote",
           argv[2], out.np, out.ns, (unsigned long long)settings);
    rsx_sdb_free(&out);
    return rc ? 1 : 0;
}
#endif
