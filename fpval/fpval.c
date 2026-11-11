/* Scratch harness: decompile real R2 fragment programs with rsx_fp_frag_state
 * configurations and write HLSL for glslangValidator / spirv-val. */
#include "rsx_fp_decompiler.h"
#include "rsx_vp_decompiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static u8 buf[65536];
static char ps[262144];
static char vs[262144];

static long rd(const char* p, u8* b, long cap)
{
    FILE* f = fopen(p, "rb"); if (!f) return -1;
    long n = (long)fread(b, 1, (size_t)cap, f); fclose(f); return n;
}
static void wr(const char* p, const char* s)
{
    FILE* f = fopen(p, "w"); if (!f) return; fputs(s, f); fclose(f);
}

static int emit(const char* outdir, const char* name, int cfg, u32 ctrl,
                const u8* uc, u32 n)
{
    rsx_fp_frag_state fs; memset(&fs, 0, sizeof fs);
    const u32 inputs = rsx_fp_input_mask(uc, n);
    const u32 units = rsx_fp_texture_units(uc, n);
    u8 gm[16] = {0}, em[16] = {0};
    if (cfg == 0) {        /* everything prolog-side + ROP */
        { static const u32 m[6] = {0x2601,0x0800,0x0801,0x0804,0x0802,0x0803}; static int k; fs.fog = 1; fs.fog_mode = m[k++ % 6]; }
        if (inputs & 1u) { fs.wpos = 1; fs.wpos_origin_bottom = 1; fs.wpos_center_integer = 1; }
        if (inputs & 6u) fs.two_sided = 1;
        for (u32 t = 0; t < 10; t++) if (inputs & (1u << (4 + t))) {
            if (t & 1u) fs.texcoord_2d_mask |= (u16)(1u << t);
            fs.centroid_mask |= (u16)(1u << t);
        }
        fs.stipple = 1;
        for (u32 y = 0; y < 32; y++) fs.stipple_pattern[y] = 0xAAAA5555u ^ y;
        fs.alpha_kill_mask = (u16)units;
        for (u32 u = 0; u < 16; u++) if (units & (1u << u)) fs.sext_masks[u] = (u & 1) ? 0x0F : 0x13;
        fs.rop_round = 1; fs.alpha_to_coverage = 1; fs.alpha_to_one = 1;
    } else if (cfg == 1) { /* depth compare + MRT + exp2 fog */
        { fs.fog = 1; fs.fog_mode = 0x0802; }
        fs.mrt_count = 3; fs.rop_round = 1;
        for (u32 u = 16; u-- > 0; ) if (!(units & (1u << u))) {
            fs.depth_compare = 1; fs.depth_compare_unit = (u8)u; fs.depth_compare_24 = 1; break;
        }
    } else {               /* depth compare with 16-bit zeta, centroid on tc8/9 */
        for (u32 u = 16; u-- > 0; ) if (!(units & (1u << u))) {
            fs.depth_compare = 1; fs.depth_compare_unit = (u8)u; break;
        }
        fs.centroid_mask = (u16)(((inputs >> 4) & 0x3FFu) | 0x300u);
        fs.alpha_to_coverage = 1;
    }
    u32 nconst = 0;
    int r = rsx_fp_decompile_engine(uc, n, ctrl, cfg == 2 ? 0x0002u : 0u, 0, NULL, gm, em,
                                    &fs, ps, sizeof ps, &nconst);
    if (r <= 0) {
        int r0 = rsx_fp_decompile_engine(uc, n, ctrl, 0, 0, NULL, gm, em, NULL, ps, sizeof ps, &nconst);
        printf("%s cfg%d: decompile %d (without fs %d)\n", name, cfg, r, r0); return 0;
    }
    if (rsx_fp_apply_rop_stage(ps, sizeof ps, &fs, 0) < 0) printf("%s cfg%d: rop0 fail\n", name, cfg);
    if (cfg != 2 && rsx_fp_apply_alpha_test_buffered(ps, sizeof ps, 0x0204u) < 0)
        printf("%s cfg%d: alpha test fail\n", name, cfg);
    if (rsx_fp_apply_rop_stage(ps, sizeof ps, &fs, 1) < 0) printf("%s cfg%d: rop1 fail\n", name, cfg);
    char path[512];
    snprintf(path, sizeof path, "%s\\%s_c%d_%02x.frag.hlsl", outdir, name, cfg, ctrl);
    wr(path, ps);
    return 1;
}

int main(int argc, char** argv)
{
    if (argc < 4) return 2;
    const char* dir = argv[1]; const char* outdir = argv[2]; int maxn = atoi(argv[3]);
    char pat[512]; snprintf(pat, sizeof pat, "%s\\*.fp", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    int count = 0;
    if (h != INVALID_HANDLE_VALUE) do {
        char p[512]; snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
        long n = rd(p, buf, sizeof buf); if (n <= 0) continue;
        char name[64]; snprintf(name, sizeof name, "%.*s", (int)(strlen(fd.cFileName) - 3), fd.cFileName);
        for (int cfg = 0; cfg < 3; cfg++) emit(outdir, name, cfg, 0x40u, buf, (u32)n);
        emit(outdir, name, 1, 0x4Eu, buf, (u32)n);   /* depth export + compare */
        emit(outdir, name, 0, 0x00u, buf, (u32)n);   /* h-register export */
        if (++count >= maxn) break;
    } while (FindNextFileA(h, &fd));
    /* VS: a few .vp, back-colour patched. */
    snprintf(pat, sizeof pat, "%s\\*.vp", dir);
    h = FindFirstFileA(pat, &fd);
    int nv = 0;
    if (h != INVALID_HANDLE_VALUE) do {
        char p[512]; snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
        long n = rd(p, buf, sizeof buf); if (n <= 0) continue;
        if (rsx_vp_decompile_compact_ex(buf, (u32)n, 0, 0xFFFFu, vs, sizeof vs) <= 0) continue;
        if (rsx_fp_patch_vs_back_colors(vs, sizeof vs) < 0) { printf("vs patch fail %s\n", fd.cFileName); continue; }
        snprintf(p, sizeof p, "%s\\vs%d.vert.hlsl", outdir, nv);
        wr(p, vs);
        if (++nv >= 4) break;
    } while (FindNextFileA(h, &fd));
    printf("programs %d, vs %d\n", count, nv);
    return 0;
}
