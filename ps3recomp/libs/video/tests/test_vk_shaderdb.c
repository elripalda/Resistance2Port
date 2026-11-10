/*
 * ps3recomp - tests for rsx_vk_shaderdb (the persistent pipeline database:
 * file format, crash tolerance, invalidation, background writer, merge).
 *
 *   cc -I libs/video -o t libs/video/tests/test_vk_shaderdb.c libs/video/rsx_vk_shaderdb.c \
 *      libs/video/rsx_vk_pipestore.c && ./t
 */
#include "rsx_vk_shaderdb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond)                                                        \
    do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
                        g_fail++; } } while (0)

#define SET_A 0x1111222233334444ull
#define SET_B 0x5555666677778888ull

static const char* VS[3] = { "float4 vs0() { return 0; }", "float4 vs1() { return 1; }",
                             "float4 vs2() { return 2; }" };
static const char* PS = "float4 ps() : SV_Target { return 1; }";

static void put_pipe(rsx_sdb_writer* w, uint64_t key, int vs, uint64_t bits)
{
    unsigned char rs[40], lay[24], vp[16], fp[32];
    for (unsigned i = 0; i < sizeof rs; i++) rs[i] = (unsigned char)(key + i);
    for (unsigned i = 0; i < sizeof lay; i++) lay[i] = (unsigned char)(key * 3u + i);
    memset(vp, 0xA5, sizeof vp);
    memset(fp, 0x5A, sizeof fp);
    rsx_sdb_writer_pipeline(w, key, VS[vs], (uint32_t)strlen(VS[vs]), PS, (uint32_t)strlen(PS), 16, 3, 1,
                            0, rs, sizeof rs, lay, sizeof lay, vp, sizeof vp, fp, sizeof fp, bits);
}

static long file_size(const char* p)
{
    FILE* f = fopen(p, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fclose(f);
    return n;
}

static unsigned char* slurp(const char* p, long* n)
{
    *n = file_size(p);
    FILE* f = fopen(p, "rb");
    unsigned char* d = (unsigned char*)malloc((size_t)*n + 1u);
    if (fread(d, 1, (size_t)*n, f) != (size_t)*n) *n = 0;
    fclose(f);
    return d;
}

static void spit(const char* p, const unsigned char* d, long n)
{
    FILE* f = fopen(p, "wb");
    fwrite(d, 1, (size_t)n, f);
    fclose(f);
}

static rsx_sdb_writer* open_w(const char* path, uint64_t set, rsx_sdb* known, rsx_sdb_load_info* li)
{
    memset(known, 0, sizeof *known);
    rsx_sdb_load(known, path, set, li);
    return rsx_sdb_writer_open(path, set, known, li);
}

/* Written, read back exactly, deduplicated (shader texts once, pipelines once, variants ORed). */
static void test_roundtrip(void)
{
    const char* p = "sdb_t1.db";
    remove(p);
    rsx_sdb k; rsx_sdb_load_info li;
    rsx_sdb_writer* w = open_w(p, SET_A, &k, &li);
    CHECK(li.status == RSX_SDB_MISSING && w);
    put_pipe(w, 100, 0, 1);
    put_pipe(w, 100, 0, 1);          /* duplicate: nothing */
    put_pipe(w, 200, 0, 2);          /* same programs: no new shader record */
    put_pipe(w, 300, 1, 0);
    rsx_sdb_writer_variants(w, 100, 4);
    rsx_sdb_writer_variants(w, 100, 4);   /* duplicate */
    rsx_sdb_writer_variants(w, 999, 8);   /* unknown key: dropped */
    rsx_sdb_writer_sync(w);
    rsx_sdb_writer_close(w);
    rsx_sdb_free(&k);

    rsx_sdb db; memset(&db, 0, sizeof db);
    CHECK(rsx_sdb_load(&db, p, SET_A, &li) == RSX_SDB_OK);
    CHECK(db.np == 3 && db.ns == 3);
    CHECK(li.records == 3 + 3 + 1);
    CHECK(li.good_end == file_size(p));
    const rsx_sdb_pipe* a = rsx_sdb_find_pipe(&db, 100);
    CHECK(a && a->bits == 5 && a->stride == 16 && a->rt_fmt == 3 && a->rt_count == 1);
    CHECK(a && a->rs_len == 40 && a->layout_len == 24 && a->vp_len == 16 && a->fp_len == 32);
    CHECK(a && a->blob[0] == 100 && a->blob[40] == (unsigned char)300 && a->blob[64] == 0xA5 && a->blob[80] == 0x5A);
    const rsx_sdb_shader* vs = a ? rsx_sdb_find_shader(&db, a->vs_hash) : NULL;
    const rsx_sdb_shader* ps = a ? rsx_sdb_find_shader(&db, a->ps_hash) : NULL;
    CHECK(vs && vs->stage == 0 && !strcmp(vs->text, VS[0]));
    CHECK(ps && ps->stage == 1 && !strcmp(ps->text, PS));
    CHECK(rsx_sdb_find_pipe(&db, 300) && rsx_sdb_find_pipe(&db, 300)->vs_hash != a->vs_hash);
    rsx_sdb_free(&db);

    /* Reopened with what is on file: nothing known is written again. */
    const long before = file_size(p);
    w = open_w(p, SET_A, &k, &li);
    put_pipe(w, 100, 0, 1);
    rsx_sdb_writer_variants(w, 200, 2);
    rsx_sdb_writer_close(w);
    rsx_sdb_free(&k);
    CHECK(file_size(p) == before);
    remove(p);
}

/* A crash mid-append: every cut point loads the intact prefix, and the
 * writer truncates the damage before appending. */
static void test_truncation(void)
{
    const char* p = "sdb_t2.db";
    remove(p);
    rsx_sdb k; rsx_sdb_load_info li;
    rsx_sdb_writer* w = open_w(p, SET_A, &k, &li);
    for (uint64_t i = 1; i <= 6; i++) put_pipe(w, i * 17u, (int)(i % 3u), i);
    rsx_sdb_writer_close(w);
    rsx_sdb_free(&k);
    long n;
    unsigned char* d = slurp(p, &n);
    CHECK(n > 100);
    uint32_t prev = 0;
    for (long cut = 0; cut < n; cut++) {
        spit(p, d, cut);
        rsx_sdb db; memset(&db, 0, sizeof db);
        const int st = rsx_sdb_load(&db, p, SET_A, &li);
        if (cut < 24) CHECK(st == RSX_SDB_BAD_HEADER);
        else {
            CHECK(st == RSX_SDB_OK || st == RSX_SDB_TRUNCATED);
            CHECK(li.good_end <= cut);
            CHECK(db.np >= prev);
            prev = db.np;
            for (uint32_t i = 0; i < db.np; i++) {      /* whatever loads is whole */
                CHECK(rsx_sdb_find_shader(&db, db.p[i].vs_hash) != NULL);
                CHECK(db.p[i].bits == db.p[i].key / 17u);
            }
        }
        rsx_sdb_free(&db);
    }
    CHECK(prev == 5);   /* the last pipeline record is the one cut */

    /* Writer after a cut in the middle of a record: cut back, append, all intact. */
    spit(p, d, n - 7);
    w = open_w(p, SET_A, &k, &li);
    CHECK(li.status == RSX_SDB_TRUNCATED && k.np == 5);
    put_pipe(w, 6u * 17u, 0, 6);
    put_pipe(w, 7u * 17u, 1, 7);
    rsx_sdb_writer_close(w);
    rsx_sdb_free(&k);
    rsx_sdb db; memset(&db, 0, sizeof db);
    CHECK(rsx_sdb_load(&db, p, SET_A, &li) == RSX_SDB_OK && db.np == 7);
    rsx_sdb_free(&db);

    /* A flipped byte inside a record fails its checksum: the file ends there. */
    free(d);
    d = slurp(p, &n);
    d[n - 3] ^= 0x40;
    spit(p, d, n);
    memset(&db, 0, sizeof db);
    CHECK(rsx_sdb_load(&db, p, SET_A, &li) == RSX_SDB_TRUNCATED && db.np == 6);
    rsx_sdb_free(&db);
    /* Garbage after the header: nothing but no crash. */
    memset(d + 24, 0xEE, (size_t)(n - 24));
    spit(p, d, n);
    memset(&db, 0, sizeof db);
    CHECK(rsx_sdb_load(&db, p, SET_A, &li) == RSX_SDB_TRUNCATED && db.np == 0 && li.good_end == 24);
    rsx_sdb_free(&db);
    free(d);
    remove(p);
}

/* Another settings hash or format version: not used, the user's file started again. */
static void test_invalidation(void)
{
    const char* p = "sdb_t3.db";
    remove(p);
    rsx_sdb k; rsx_sdb_load_info li;
    rsx_sdb_writer* w = open_w(p, SET_A, &k, &li);
    put_pipe(w, 42, 0, 1);
    rsx_sdb_writer_close(w);
    rsx_sdb_free(&k);

    rsx_sdb db; memset(&db, 0, sizeof db);
    CHECK(rsx_sdb_load(&db, p, SET_B, &li) == RSX_SDB_SETTINGS_MISMATCH && db.np == 0 && li.settings == SET_A);
    CHECK(rsx_sdb_load(&db, p, RSX_SDB_ANY_SETTINGS, &li) == RSX_SDB_OK && db.np == 1);
    rsx_sdb_free(&db);

    w = open_w(p, SET_B, &k, &li);
    CHECK(li.status == RSX_SDB_SETTINGS_MISMATCH && w);
    put_pipe(w, 43, 1, 1);
    rsx_sdb_writer_close(w);
    rsx_sdb_free(&k);
    memset(&db, 0, sizeof db);
    CHECK(rsx_sdb_load(&db, p, SET_B, &li) == RSX_SDB_OK && db.np == 1 && rsx_sdb_find_pipe(&db, 43));
    rsx_sdb_free(&db);

    long n;
    unsigned char* d = slurp(p, &n);
    d[4] = (unsigned char)(RSX_SDB_VERSION + 1u);
    spit(p, d, n);
    memset(&db, 0, sizeof db);
    CHECK(rsx_sdb_load(&db, p, SET_B, &li) == RSX_SDB_VERSION_MISMATCH && db.np == 0);
    d[0] ^= 1;
    spit(p, d, n);
    CHECK(rsx_sdb_load(&db, p, SET_B, &li) == RSX_SDB_BAD_HEADER && db.np == 0);
    rsx_sdb_free(&db);
    free(d);
    w = open_w(p, SET_B, &k, &li);
    put_pipe(w, 44, 2, 1);
    rsx_sdb_writer_close(w);
    rsx_sdb_free(&k);
    memset(&db, 0, sizeof db);
    CHECK(rsx_sdb_load(&db, p, SET_B, &li) == RSX_SDB_OK && db.np == 1 && rsx_sdb_find_pipe(&db, 44));
    rsx_sdb_free(&db);
    remove(p);
}

/* Several players' files into one shippable file: union, variants ORed. */
static void test_merge(void)
{
    const char* a = "sdb_t4a.db"; const char* b = "sdb_t4b.db"; const char* o = "sdb_t4o.db";
    remove(a); remove(b); remove(o);
    rsx_sdb k; rsx_sdb_load_info li;
    rsx_sdb_writer* w = open_w(a, SET_A, &k, &li);
    put_pipe(w, 1, 0, 1); put_pipe(w, 2, 1, 1);
    rsx_sdb_writer_close(w); rsx_sdb_free(&k);
    w = open_w(b, SET_A, &k, &li);
    put_pipe(w, 2, 1, 2); put_pipe(w, 3, 2, 1);
    rsx_sdb_writer_close(w); rsx_sdb_free(&k);

    rsx_sdb m; memset(&m, 0, sizeof m);
    rsx_sdb x; memset(&x, 0, sizeof x);
    CHECK(rsx_sdb_load(&x, a, SET_A, &li) == RSX_SDB_OK);
    CHECK(rsx_sdb_merge(&m, &x) == 2);
    rsx_sdb_free(&x); memset(&x, 0, sizeof x);
    CHECK(rsx_sdb_load(&x, b, SET_A, &li) == RSX_SDB_OK);
    CHECK(rsx_sdb_merge(&m, &x) == 1);
    rsx_sdb_free(&x);
    CHECK(rsx_sdb_save(&m, o, SET_A) == 0);
    rsx_sdb_free(&m);

    rsx_sdb db; memset(&db, 0, sizeof db);
    CHECK(rsx_sdb_load(&db, o, SET_A, &li) == RSX_SDB_OK && db.np == 3 && db.ns == 4);
    CHECK(rsx_sdb_find_pipe(&db, 2) && rsx_sdb_find_pipe(&db, 2)->bits == 3);
    for (uint32_t i = 0; i < db.np; i++) {
        const rsx_sdb_shader* s = rsx_sdb_find_shader(&db, db.p[i].vs_hash);
        CHECK(s && !strcmp(s->text, VS[db.p[i].key - 1u]));
        CHECK(db.p[i].rs_len == 40 && db.p[i].blob[0] == (unsigned char)db.p[i].key);
    }
    rsx_sdb_free(&db);
    remove(a); remove(b); remove(o);
}

int main(void)
{
    test_roundtrip();
    test_truncation();
    test_invalidation();
    test_merge();
    if (g_fail) { printf("%d check(s) FAILED\n", g_fail); return 1; }
    printf("test_vk_shaderdb: all passed\n");
    return 0;
}
