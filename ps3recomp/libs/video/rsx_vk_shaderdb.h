/* Persistent pipeline database (RPCS3 shaders_cache, one file per title):
 * every pipeline a backend creates, with everything needed to build exactly
 * that pipeline again, so a later run can precompile all of them at boot.
 *
 * File: shadercache/pipelines_<title>.db, append-only.
 *   header  { u32 magic "RSDB", u32 version, u64 settings hash, u32 0, u32 0 }
 *   records { u32 magic "RREC", u32 type, u32 payload bytes, u32 checksum, payload }
 *     SHADER    u64 hash, u32 stage, u32 len, text            (deduplicated programs)
 *     PIPELINE  u64 key, vs hash, ps hash, bits, u32 stride, rt_fmt, rt_count, flags,
 *               rs_len, layout_len, vp_len, fp_len, then those four blobs
 *     VARIANTS  u64 key, u64 bits                              (ORed per key)
 * The settings hash names the decompiler / backend configuration the stored
 * programs were produced by: a file with another one is not used, and the
 * user's own file is started again. A record cut short or failing its
 * checksum ends the file (a crash mid-append): everything before it loads,
 * and the writer truncates the damage away before appending.
 *
 * Plain C, no Vulkan: the file format, the merge tool and the background
 * writer. The Vulkan glue is rsx_vk_shaderdb_glue.inc.
 * Build the merge tool with -DRSX_SDB_TOOL (scratch/shaderdb_merge.ps1). */
#ifndef RSX_VK_SHADERDB_H
#define RSX_VK_SHADERDB_H

#include <stddef.h>
#include <stdint.h>

#include "rsx_vk_pipestore.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RSX_SDB_MAGIC      0x42445352u   /* "RSDB" */
#define RSX_SDB_REC_MAGIC  0x43455252u   /* "RREC" */
#define RSX_SDB_VERSION    1u
/* rsx_sdb_load's `settings` that accepts any file's (tools). */
#define RSX_SDB_ANY_SETTINGS UINT64_MAX

enum { RSX_SDB_REC_SHADER = 1, RSX_SDB_REC_PIPELINE = 2, RSX_SDB_REC_VARIANTS = 3 };

enum {
    RSX_SDB_OK = 0,          /* whole file read */
    RSX_SDB_MISSING,         /* no file */
    RSX_SDB_BAD_HEADER,      /* not a pipeline database */
    RSX_SDB_VERSION_MISMATCH,
    RSX_SDB_SETTINGS_MISMATCH,
    RSX_SDB_TRUNCATED        /* damaged tail: records up to good_end loaded */
};

typedef struct {
    uint64_t hash;
    uint32_t stage, len;
    char*    text;           /* NUL-terminated */
} rsx_sdb_shader;

typedef struct {
    uint64_t key, vs_hash, ps_hash, bits;
    uint32_t stride, rt_fmt, rt_count, flags;
    uint32_t rs_len, layout_len, vp_len, fp_len;
    uint8_t* blob;           /* rs | layout | vp ucode | fp ucode */
} rsx_sdb_pipe;

typedef struct {
    rsx_sdb_pipe*   p;
    uint32_t        np, cp;
    rsx_sdb_shader* s;
    uint32_t        ns, cs;
    vkps_map        pidx, sidx;   /* key -> index + 1 */
} rsx_sdb;

typedef struct {
    int      status;              /* RSX_SDB_* */
    uint32_t version;
    uint64_t settings;            /* the file's settings hash (0 when unreadable) */
    long     good_end;            /* bytes of intact header + records */
    uint32_t records, pipelines, shaders;
} rsx_sdb_load_info;

uint64_t rsx_sdb_hash(const void* p, size_t n);

/* Records are added to `db` (pipelines deduplicated by key, variant bits ORed). */
int  rsx_sdb_load(rsx_sdb* db, const char* path, uint64_t settings, rsx_sdb_load_info* info);
/* Write `db` compactly (shaders first) to `path` via a temporary file. 0 = ok. */
int  rsx_sdb_save(const rsx_sdb* db, const char* path, uint64_t settings);
void rsx_sdb_free(rsx_sdb* db);

/* In-memory adds (the loader, the merge tool, tests). NULL / -1 on failure. */
const rsx_sdb_shader* rsx_sdb_add_shader(rsx_sdb* db, uint64_t hash, uint32_t stage,
                                         const char* text, uint32_t len);
rsx_sdb_pipe* rsx_sdb_add_pipe(rsx_sdb* db, const rsx_sdb_pipe* p);   /* copies the blob; ORs bits if known */
const rsx_sdb_shader* rsx_sdb_find_shader(const rsx_sdb* db, uint64_t hash);
rsx_sdb_pipe* rsx_sdb_find_pipe(const rsx_sdb* db, uint64_t key);
/* Add everything of `src` into `dst`. Returns the pipelines added. */
uint32_t rsx_sdb_merge(rsx_sdb* dst, const rsx_sdb* src);

/* ---- background writer: records are queued by the render threads and
 * appended by one below-normal thread. Duplicates (by key, by shader hash,
 * by variant bit) are dropped before queueing. */
typedef struct rsx_sdb_writer rsx_sdb_writer;

/* `known` (may be NULL): what the file already holds, so it is not written
 * twice. info: the rsx_sdb_load result for `path` with the same settings:
 * OK appends, TRUNCATED cuts the file at good_end first, anything else
 * starts the file again. */
rsx_sdb_writer* rsx_sdb_writer_open(const char* path, uint64_t settings, const rsx_sdb* known,
                                    const rsx_sdb_load_info* info);
/* Shader texts are written once each; the pipeline record once per key. */
void rsx_sdb_writer_pipeline(rsx_sdb_writer* w, uint64_t key, const char* vs, uint32_t vs_len,
                             const char* ps, uint32_t ps_len, uint32_t stride, uint32_t rt_fmt,
                             uint32_t rt_count, uint32_t flags, const void* rs, uint32_t rs_len,
                             const void* layout, uint32_t layout_len, const void* vp, uint32_t vp_len,
                             const void* fp, uint32_t fp_len, uint64_t bits);
void rsx_sdb_writer_variants(rsx_sdb_writer* w, uint64_t key, uint64_t bits);
/* Wait until everything queued is on disk. */
void rsx_sdb_writer_sync(rsx_sdb_writer* w);
void rsx_sdb_writer_close(rsx_sdb_writer* w);
uint64_t rsx_sdb_writer_bytes(const rsx_sdb_writer* w);

/* Name the calling thread (SetThreadDescription where available). */
void     rsx_sdb_thread_name(const wchar_t* name);
uint64_t rsx_sdb_now_us(void);

/* ---- glue entry points (rsx_vk_shaderdb_glue.inc, Vulkan backend). */
/* Optional: the guest programs the next pipeline_create is built from
 * (stored for tooling; the backend rebuilds from the decompiled programs). */
void rsx_vk_shaderdb_set_guest_programs(const void* vp, uint32_t vp_len, const void* fp, uint32_t fp_len);
/* Optional: the database's file name part (default: the executable's name). */
void rsx_vk_shaderdb_set_title(const char* title);
/* Start loading + precompiling now (the engine's render thread, once the
 * backend is up; also done by itself at engine init / the first pipeline).
 * wait != 0 blocks until the precompile finished. Returns the entries
 * queued, -1 when the database is off. */
int  rsx_vk_shaderdb_precompile(int wait);
/* Counters: [0] entries loaded, [1] pipelines (variants) linked by the
 * precompile, [2] precompile entries finished, [3] engine pipelines served
 * by a precompiled slot, [4] pipelines linked after the precompile finished,
 * [5] shader modules compiled after it finished, [6] draws skipped,
 * [7] synchronous waits that got their pipeline, [8] total synchronous wait
 * in microseconds, [9] precompile entries queued. */
void rsx_vk_shaderdb_stats(uint64_t out[10]);

#ifdef __cplusplus
}
#endif

#endif
