/*
 * ps3recomp - Real fragment-program validation
 *
 * Runs rsx_fp_decompile over a directory of raw RSX fragment programs (e.g.
 * RPCS3's shader cache `raw/ *.fp`, which are the unmodified NV40 ucode bytes
 * captured from a real title) and reports coverage:
 *   - how many decompile with every opcode handled,
 *   - which opcodes are still unhandled, and how often,
 *   - a histogram of every opcode seen across the corpus.
 *
 * This is real-data validation of the decompiler (no synthetic input, no
 * D3D12). D3DCompile of the generated HLSL is a separate, Windows-only step.
 *
 * Build:
 *   gcc -std=c11 -O2 -I../../../include -I.. \
 *       validate_fp_shaders.c ../rsx_fp_decompiler.c -o vfp.exe
 * Run:
 *   ./vfp.exe "F:/Emulador/Rpcs3/cache/<title>/.../shaders_cache/raw"
 */

#include "rsx_fp_decompiler.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <d3dcompiler.h>
#endif

/* NV40 FP opcode field + END bit + CONST-source detection (mirrors the
 * decompiler/program_size walk) so we can build a full opcode histogram. */
#define FP_OPCODE(w0)   (((w0) >> 24) & 0x3F)
#define FP_END(w0)      ((w0) & 1u)
#define FP_REG_TYPE(w)  ((w) & 3u)
#define FP_CONST        2u

static int   op_count[64];       /* histogram of opcodes seen */
static int   op_unhandled[64];   /* per-opcode count of files where it was unhandled */

static u8  filebuf[64 * 1024];
static char hlsl[128 * 1024];
static char hlsl_bank[256 * 1024];

/* FP9: the engine form with every dynamic option on, without and with
 * rsx_fp_set_bindless, under two cube/volume unit assignments. The bindless
 * text must sample the same units, keep no bank reference, and compile as
 * ps_5_1 (descriptor arrays in register spaces). */
static int bl_total, bl_fail, bl_shown;
static void check_bindless(const char* name, const u8* uc, u32 size)
{
    static const u32 masks[2][2] = { { 0u, 0u }, { 0x5555u, 0x2222u } };
    const u8 zero16[16] = { 0 };
    rsx_fp_frag_state fs; memset(&fs, 0, sizeof fs);
    rsx_fp_set_dynamic_texparam(1); rsx_fp_set_dynamic_rop(1); rsx_fp_set_res_scale_dynamic(1);
    for (int v = 0; v < 2; v++) {
        u32 nc = 0;
        rsx_fp_set_bindless(0);
        int r0 = rsx_fp_decompile_engine(uc, size, 0, masks[v][0], masks[v][1], NULL, zero16, zero16,
                                         &fs, hlsl_bank, (u32)sizeof hlsl_bank, &nc);
        rsx_fp_set_bindless(1);
        int r1 = rsx_fp_decompile_engine(uc, size, 0, masks[v][0], masks[v][1], NULL, zero16, zero16,
                                         &fs, hlsl, (u32)sizeof hlsl, &nc);
        rsx_fp_set_bindless(0);
        if (r0 < 0) continue;
        bl_total++;
        const char* why = NULL;
        if (r1 != r0) why = "decompile result differs";
        else if (rsx_fp_apply_unnorm_scale_dyn(hlsl, (u32)sizeof hlsl, 0xFFFFu, masks[v][0]) < 0 ||
                 rsx_fp_apply_unnorm_scale_dyn(hlsl_bank, (u32)sizeof hlsl_bank, 0xFFFFu, masks[v][0]) < 0)
            why = "texparam patch failed";
        else if (strstr(hlsl, "rsx_samp[") || strstr(hlsl, "rsx_tex[")) why = "bank reference left";
        else if (rsx_fp_sampled_units(hlsl) != rsx_fp_sampled_units(hlsl_bank)) why = "sampled units differ";
        else if (strstr(hlsl_bank, "_rsx_txf") && !strstr(hlsl, "_rsx_txf")) why = "texparam sites lost";
#ifdef _WIN32
        if (!why) {
            ID3DBlob* code = NULL; ID3DBlob* err = NULL;
            HRESULT hr = D3DCompile(hlsl, strlen(hlsl), name, NULL, NULL, "main", "ps_5_1", 0, 0, &code, &err);
            if (FAILED(hr)) why = err ? (const char*)err->lpVtbl->GetBufferPointer(err) : "D3DCompile";
            if (code) code->lpVtbl->Release(code);
            if (why && bl_shown < 4) printf("  [bindless FAIL] %s v%d: %s\n", name, v, why);
            if (err) err->lpVtbl->Release(err);
            if (why) { bl_fail++; bl_shown++; }
            continue;
        }
#endif
        if (why) { bl_fail++; if (bl_shown++ < 4) printf("  [bindless FAIL] %s v%d: %s\n", name, v, why); }
    }
    rsx_fp_set_dynamic_texparam(0); rsx_fp_set_dynamic_rop(0); rsx_fp_set_res_scale_dynamic(0);
}

static long read_file(const char* path, u8* buf, long cap)
{
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    long n = (long)fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return n;
}

/* Walk the program and tally opcodes (independent of the decompiler). */
static int tally_opcodes(const u8* uc, u32 size)
{
    u32 off = 0; int instrs = 0;
    while (off + 16 <= size) {
        u32 w0 = rsx_fp_read_word(uc + off + 0);
        u32 w1 = rsx_fp_read_word(uc + off + 4);
        u32 w2 = rsx_fp_read_word(uc + off + 8);
        u32 w3 = rsx_fp_read_word(uc + off + 12);
        op_count[FP_OPCODE(w0)]++;
        instrs++;
        off += 16;
        if (FP_REG_TYPE(w1) == FP_CONST || FP_REG_TYPE(w2) == FP_CONST ||
            FP_REG_TYPE(w3) == FP_CONST) off += 16;
        if (FP_END(w0)) break;
    }
    return instrs;
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: %s <dir-of-.fp>\n", argv[0]); return 2; }
    const char* dir = argv[1];

    DIR* d = opendir(dir);
    if (!d) { printf("cannot open dir: %s\n", dir); return 1; }

    int total = 0, clean = 0, with_unhandled = 0, decode_err = 0, no_end = 0;
    int compiled = 0, compile_fail = 0, shown = 0;
    struct dirent* e;
    char path[1024];

    while ((e = readdir(d)) != NULL) {
        const char* name = e->d_name;
        size_t L = strlen(name);
        if (L < 3 || strcmp(name + L - 3, ".fp") != 0) continue;

        snprintf(path, sizeof(path), "%s/%s", dir, name);
        long n = read_file(path, filebuf, (long)sizeof(filebuf));
        if (n < 16) continue;
        total++;

        u32 size = rsx_fp_program_size(filebuf, (u32)n);
        if (size == 0) { no_end++; size = (u32)n; }
        tally_opcodes(filebuf, size);

        check_bindless(name, filebuf, size);
        int instrs = rsx_fp_decompile(filebuf, size, 0, hlsl, (u32)sizeof(hlsl));
        if (instrs < 0) { decode_err++; continue; }

        /* Scan for unhandled-opcode markers the decompiler emits. */
        int file_unhandled = 0;
        const char* p = hlsl;
        const char* mark = "unhandled FP opcode ";
        while ((p = strstr(p, mark)) != NULL) {
            p += strlen(mark);
            /* opcode mnemonic follows; match it back to an opcode id */
            for (int op = 0; op < 64; op++) {
                const char* nm = rsx_fp_opcode_name((u32)op);
                if (nm[0] != '?' && strncmp(p, nm, strlen(nm)) == 0) {
                    op_unhandled[op]++; break;
                }
            }
            file_unhandled++;
        }
        if (file_unhandled) with_unhandled++; else clean++;

#ifdef _WIN32
        /* Real test: does the generated HLSL actually compile? */
        {
            ID3DBlob* code = NULL; ID3DBlob* err = NULL;
            HRESULT hr = D3DCompile(hlsl, strlen(hlsl), name, NULL, NULL,
                                    "main", "ps_5_0", 0, 0, &code, &err);
            if (SUCCEEDED(hr)) {
                compiled++;
                if (code) code->lpVtbl->Release(code);
            } else {
                compile_fail++;
                if (shown < 4) {
                    printf("  [D3DCompile FAIL] %s: %s\n", name,
                           err ? (const char*)err->lpVtbl->GetBufferPointer(err) : "?");
                    shown++;
                }
            }
            if (err) err->lpVtbl->Release(err);
        }
#endif
    }
    closedir(d);

    printf("=== fragment-program corpus: %s ===\n", dir);
    printf("total .fp        : %d\n", total);
    printf("fully decoded    : %d  (%.1f%%)\n", clean, total ? 100.0*clean/total : 0);
    printf("with unhandled op: %d\n", with_unhandled);
    printf("decode errors    : %d\n", decode_err);
    printf("no PROGRAM_END   : %d  (decoded up to file end)\n", no_end);
#ifdef _WIN32
    printf("D3DCompile OK    : %d  (%.1f%%)\n", compiled, total ? 100.0*compiled/total : 0);
    printf("D3DCompile FAIL  : %d\n", compile_fail);
#endif

    printf("bindless (ps_5_1): %d/%d OK\n", bl_total - bl_fail, bl_total);

    printf("\n=== opcode histogram (count / unhandled-files) ===\n");
    for (int op = 0; op < 64; op++) {
        if (op_count[op] == 0) continue;
        const char* nm = rsx_fp_opcode_name((u32)op);
        printf("  %-8s (0x%02X): %5d%s\n", nm, op, op_count[op],
               op_unhandled[op] ? "   <-- UNHANDLED" : "");
    }
    /* Unhandled-op files are reported, not failed: RPCS3's cache stores ucode
     * from program_start_offset, so a leading NOP's literal can become the
     * first slot (RPCS3 logs it as an illegal instruction too). */
    return (total == 0 || decode_err || compile_fail || bl_fail || !bl_total) ? 1 : 0;
}
