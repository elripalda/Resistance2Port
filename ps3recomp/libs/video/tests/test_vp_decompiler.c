/*
 * Vertex program decompiler parity tests against RPCS3's
 * VertexProgramDecompiler / analyse_vertex_program rules. Synthetic programs
 * check the individual rules; the RPCS3 shader-cache corpus (when present)
 * must decompile and compile as vs_5_0.
 */
#include "rsx_vp_decompiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11.h>

static int s_fail;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); s_fail++; } } while (0)

static char hlsl[256 * 1024];

/* ---- encoder ---------------------------------------------------------- */
#define SWZ_ID 0x1B00u                     /* x y z w */
static u32 src_temp(u32 r)  { return 1u | (r << 2) | SWZ_ID; }
static u32 src_in(void)     { return 2u | SWZ_ID; }
static u32 src_const(void)  { return 3u | SWZ_ID; }

typedef struct {
    u32 vec, sca, src0, src1, src2, input, cst, index_const;
    u32 dst, dst_tmp, sca_tmp, vmask, smask, vec_result, end;
    u32 cond, cond_test, cc_up0, cc_up1, saturate, addr_sel, addr_swz, cc_swz;
    u32 target, brb_bit, brb_true;
} Ins;

static void enc(u8* p, const Ins* i)
{
    const u32 s0 = i->src0, s1 = i->src1, s2 = i->src2;
    u32 d0 = (i->addr_swz & 3u) | ((i->cc_swz & 0xFFu) << 2) | ((i->cond & 7u) << 10) |
             (i->cond_test << 13) | (i->cc_up0 << 14) | ((i->dst_tmp & 0x3Fu) << 15) |
             (((i->target >> 9) & 1u) << 23) | (i->addr_sel << 24) |
             (i->saturate << 26) | (i->cc_up1 << 29) | (i->vec_result << 30);
    u32 d1 = ((s0 >> 9) & 0xFFu) | ((i->input & 0xFu) << 8) | ((i->cst & 0x3FFu) << 12) |
             ((i->vec & 0x1Fu) << 22) | ((i->sca & 0x1Fu) << 27);
    u32 d2 = ((s2 >> 11) & 0x3Fu) | ((s1 & 0x1FFFFu) << 6) | ((s0 & 0x1FFu) << 23);
    u32 d3 = i->end | (i->index_const << 1) | ((i->dst & 0x1Fu) << 2) |
             ((i->sca_tmp & 0x3Fu) << 7) | ((i->vmask & 0xFu) << 13) |
             ((i->smask & 0xFu) << 17) | ((s2 & 0x7FFu) << 21);
    if (i->sca == 0x09 || i->sca == 0x11 || i->sca == 0x0A || i->sca == 0x0C) {
        d2 = (d2 & ~0x3Fu) | ((i->target >> 3) & 0x3Fu);
        d3 = (d3 & ~(0x1FFu << 23)) | ((i->brb_bit & 0x1Fu) << 23) |
             (i->brb_true << 28) | ((i->target & 7u) << 29);
    }
    const u32 w[4] = { d0, d1, d2, d3 };
    memcpy(p, w, 16);
}

/* Defaults: no destinations, sources valid, cond always. */
static Ins base_ins(void)
{
    Ins i; memset(&i, 0, sizeof i);
    i.src0 = i.src1 = i.src2 = src_in();
    i.dst = 0x1F; i.dst_tmp = 0x3F; i.sca_tmp = 0x3F; i.cond = 7; i.cc_swz = 0x1B;
    return i;
}
static Ins mov_out(u32 dst, u32 src) { Ins i = base_ins(); i.vec = 1; i.src0 = src; i.dst = dst; i.vec_result = 1; i.vmask = 0xF; return i; }

static int decompile(const u8* mem, u32 slots, u32 entry)
{
    return rsx_vp_decompile_program(mem, slots, entry, 0, 0xFFFFu, 0, hlsl, sizeof hlsl);
}

static int compiles(const char* src)
{
    ID3DBlob *code = NULL, *err = NULL;
    HRESULT hr = D3DCompile(src, strlen(src), "vp", NULL, NULL, "main", "vs_5_0",
                            D3DCOMPILE_SKIP_OPTIMIZATION, 0, &code, &err);
    if (FAILED(hr) && err) printf("%.*s\n", (int)err->lpVtbl->GetBufferSize(err) > 600 ? 600 :
                                  (int)err->lpVtbl->GetBufferSize(err),
                                  (const char*)err->lpVtbl->GetBufferPointer(err));
    if (code) code->lpVtbl->Release(code);
    if (err) err->lpVtbl->Release(err);
    return SUCCEEDED(hr);
}

static void test_rules(void)
{
    u8 m[16 * 16];
    Ins i;

    /* ARL truncates (int4 cast), into a[dst_tmp]; indexed constants wrap at
     * 512 and read 0 past 467; a direct constant past 467 is float4(0). */
    memset(m, 0, sizeof m);
    i = base_ins(); i.vec = 0x0D; i.dst_tmp = 1; i.vmask = 0xF; enc(m, &i);
    i = mov_out(0, src_const()); i.cst = 40; i.index_const = 1; i.addr_sel = 1; i.addr_swz = 2; enc(m + 16, &i);
    i = mov_out(1, src_const()); i.cst = 470; i.end = 1; enc(m + 32, &i);
    CHECK(decompile(m, 3, 0) > 0, "decompile arl");
    CHECK(strstr(hlsl, "(int4)_v") && !strstr(hlsl, "floor(_v") && !strstr(hlsl, "floor((") , "ARL must truncate");
    CHECK(strstr(hlsl, "a1.xyzw = _vi.xyzw"), "ARL writes a[dst_tmp]");
    CHECK(strstr(hlsl, "vp_cidx((40u + (uint)a1.z) & 511u)"), "indexed constant via vp_cidx");
    CHECK(!strstr(hlsl, "vp_c[470]") && strstr(hlsl, "(float4)(float4(0,0,0,0))"), "const >= 468 reads 0");
    CHECK(compiles(hlsl), "arl program compiles");

    /* COV-P9: the constant range a program reads, for a partial upload;
     * an indexed read (or flow control) needs all 468. */
    {
        u32 cnt = 0;
        CHECK(rsx_vp_analyze_constants(m, sizeof m, &cnt) == 3 && cnt == 468u,
              "indexed constant read -> full range (%u)", cnt);
        u8 k[3 * 16];
        i = mov_out(0, src_const()); i.cst = 7; enc(k, &i);
        i = mov_out(1, src_const()); i.cst = 21; enc(k + 16, &i);
        i = mov_out(2, src_const()); i.cst = 470; i.end = 1; enc(k + 32, &i);
        CHECK(rsx_vp_analyze_constants(k, sizeof k, &cnt) == 3 && cnt == 22u,
              "direct reads 7, 21 (470 reads 0) -> 22 (%u)", cnt);
        i = mov_out(2, src_temp(0)); enc(k + 32, &i);
        CHECK(rsx_vp_analyze_constants(k, sizeof k, &cnt) == -1 && cnt == 468u,
              "unterminated -> -1, full range");
    }
    /* cond == FL writes nothing; a VEC op with no destination but CC update
     * writes CC; a temp write with CC update bits does not also write CC. */
    memset(m, 0, sizeof m);
    i = base_ins(); i.vec = 1; i.dst_tmp = 3; i.vmask = 0xF; i.cond = 0; enc(m, &i);
    i = base_ins(); i.vec = 1; i.vmask = 0xF; i.cc_up0 = 1; enc(m + 16, &i);
    i = base_ins(); i.vec = 1; i.dst_tmp = 4; i.vmask = 0xF; i.cc_up0 = 1; i.cc_up1 = 1; enc(m + 32, &i);
    i = mov_out(0, src_temp(4)); i.end = 1; enc(m + 48, &i);
    CHECK(decompile(m, 4, 0) > 0, "decompile cc");
    CHECK(!strstr(hlsl, "r[3]."), "cond FL must not write");
    {
        const char* a = strstr(hlsl, "cc[0].xyzw = _v.xyzw");
        CHECK(a && !strstr(a + 1, "cc[0].xyzw = _v.xyzw"), "exactly one CC-only write");
    }
    CHECK(compiles(hlsl), "cc program compiles");

    /* Branch condition: any() over the whole CC swizzle. */
    memset(m, 0, sizeof m);
    i = base_ins(); i.sca = 0x09; i.cond = 2; i.target = 2; enc(m, &i);
    i = mov_out(0, src_in()); enc(m + 16, &i);
    i = mov_out(1, src_in()); i.end = 1; enc(m + 32, &i);
    CHECK(decompile(m, 3, 0) > 0, "decompile bri");
    CHECK(strstr(hlsl, "any(cc[0].xyzw == (float4)0)"), "BRI uses any() of the CC swizzle");
    CHECK(compiles(hlsl), "bri program compiles");

    /* Entry past a subroutine: BRB at slot 4 (entry) branches back to slot
     * 2; slot 3 is never reached. Base 2, entry 2 relative, slot 3 dead. */
    memset(m, 0, sizeof m);
    i = mov_out(1, src_in()); i.end = 1; enc(m, &i);                 /* slot 0: other program */
    i = mov_out(0, src_in()); i.input = 1; i.end = 1; enc(m + 32, &i);
    i = mov_out(2, src_in()); i.end = 1; enc(m + 48, &i);             /* dead */
    i = base_ins(); i.sca = 0x11; i.target = 2; i.brb_bit = 3; i.brb_true = 1; enc(m + 64, &i);
    i = mov_out(0, src_in()); i.end = 1; enc(m + 80, &i);
    {
        rsx_vp_program_info info;
        CHECK(rsx_vp_analyse_program(m, 16, 4, &info) == 4, "analysis count");
        CHECK(info.base == 2 && info.entry == 2 && info.has_branch, "analysis base/entry");
        CHECK(info.mask[0] == 0xDu, "analysis live mask %x", info.mask[0]);
    }
    CHECK(decompile(m, 16, 4) > 0, "decompile entry");
    CHECK(strstr(hlsl, "int _pc = 2;"), "starts at the relative entry");
    CHECK(strstr(hlsl, "((vp_branch.x & 8u) != 0u) ? 0 : 3"), "BRB target relative to base");
    CHECK(!strstr(hlsl, "o[2]."), "dead slot skipped");
    CHECK(compiles(hlsl), "entry program compiles");

    /* Conditional forward branch past an end bit keeps decoding. */
    memset(m, 0, sizeof m);
    i = base_ins(); i.sca = 0x11; i.target = 3; i.brb_bit = 0; enc(m, &i);
    i = mov_out(0, src_in()); i.end = 1; enc(m + 16, &i);
    i = mov_out(2, src_in()); i.end = 1; enc(m + 32, &i);
    i = mov_out(0, src_in()); i.input = 2; i.end = 1; enc(m + 48, &i);
    {
        /* RPCS3 walks on until the furthest target, so slot 2 is live too. */
        rsx_vp_program_info info;
        const int n = rsx_vp_analyse_program(m, 16, 0, &info);
        CHECK(n == 4 && info.mask[0] == 0xFu, "branch past end: count %d mask %x", n, info.mask[0]);
    }

    /* Scalar ops are per component; RSQ broadcasts x without abs; LG2 has no abs. */
    memset(m, 0, sizeof m);
    i = base_ins(); i.sca = 0x04; i.sca_tmp = 1; i.smask = 0xF; enc(m, &i);
    i = base_ins(); i.sca = 0x0D; i.sca_tmp = 2; i.smask = 0xF; enc(m + 16, &i);
    i = base_ins(); i.sca = 0x02; i.dst = 0; i.smask = 0xF; i.end = 1; enc(m + 32, &i);
    CHECK(decompile(m, 3, 0) > 0, "decompile sca");
    CHECK(strstr(hlsl, "1.0/sqrt(max(("), "RSQ form");
    CHECK(strstr(hlsl, "log2(max(((v[0]).xyzw), 1e-10))"), "LG2 per component, no abs");
    CHECK(strstr(hlsl, "(1.0/((v[0]).xyzw))") && strstr(hlsl, "o[0].xyzw"), "SCA RCP writes o[dst] when !vec_result");
    CHECK(compiles(hlsl), "sca program compiles");

    /* No position write: the whole body is NOPed. */
    memset(m, 0, sizeof m);
    i = mov_out(1, src_in()); i.end = 1; enc(m, &i);
    CHECK(decompile(m, 1, 0) > 0, "decompile nopos");
    CHECK(!strstr(hlsl, "o[1].xyzw = _v") && strstr(hlsl, "Out.col0 = _d;"), "no-POS program is NOPed");

    /* Register type 0 aborts the program there. */
    memset(m, 0, sizeof m);
    i = mov_out(0, src_in()); enc(m, &i);
    i = mov_out(1, src_in()); i.src1 = 0; enc(m + 16, &i);
    i = mov_out(2, src_in()); i.end = 1; enc(m + 32, &i);
    CHECK(decompile(m, 3, 0) > 0, "decompile abort");
    CHECK(strstr(hlsl, "o[0].xyzw") && !strstr(hlsl, "o[1].xyzw = _v") && !strstr(hlsl, "o[2].xyzw = _v"),
          "invalid source ends the program");

    /* NVIDIA compares (RPCS3 VKVertexProgram.cpp:367 low_precision_tests):
     * both operands through CMP_FIXUP. Point size: POINT_SIZE whenever the
     * program's o6.x is not exported, mask bit clear included. */
    memset(m, 0, sizeof m);
    i = mov_out(0, src_in()); i.vec = 0x0B; i.src1 = src_const(); i.cst = 3; i.end = 1; enc(m, &i);
    CHECK(decompile(m, 1, 0) > 0, "decompile slt");
    CHECK(strstr(hlsl, "(float4)(vp_cmpfix(") && strstr(hlsl, "return sign(a) * 16.0 + a;"),
          "SLT compares CMP_FIXUP'd operands");
    CHECK(strstr(hlsl, "Out.psize = (vp_branch.z & 0x80000000u) != 0u ? asfloat(vp_branch.w) : 1.0;"),
          "point size defaults to POINT_SIZE");
    CHECK(compiles(hlsl), "slt program compiles");

    /* 3D / cube vertex textures sample .xyz (RPCS3 VERTEX_TEXTURE_FETCH3D/CUBE). */
    memset(m, 0, sizeof m);
    i = mov_out(0, src_in()); i.vec = 0x19; i.end = 1; enc(m, &i);
    rsx_vp_set_vtex_3d_mask(1u);
    CHECK(rsx_vp_decompile_program(m, 1, 0, 1u, 0xFFFFu, 0, hlsl, sizeof hlsl) > 0, "decompile txl 3d");
    CHECK(strstr(hlsl, "Texture3D rsx_vtex0") && strstr(hlsl, "rsx_vtex0.SampleLevel(rsx_vsamp0, (") &&
          strstr(hlsl, ").xyz, 0.0)"),
          "3D vertex texture");
    CHECK(compiles(hlsl), "txl 3d compiles");
    rsx_vp_set_vtex_3d_mask(0);
    rsx_vp_set_vtex_cube_mask(1u);
    CHECK(rsx_vp_decompile_program(m, 1, 0, 1u, 0xFFFFu, 0, hlsl, sizeof hlsl) > 0, "decompile txl cube");
    CHECK(strstr(hlsl, "TextureCube rsx_vtex0"), "cube vertex texture");
    CHECK(compiles(hlsl), "txl cube compiles");
    rsx_vp_set_vtex_cube_mask(0);

    /* H2-02: temp writes keep the full 6-bit index (R33 is not R1). */
    memset(m, 0, sizeof m);
    i = base_ins(); i.vec = 1; i.dst_tmp = 33; i.vmask = 0xF; enc(m, &i);
    i = mov_out(0, src_temp(33)); i.end = 1; enc(m + 16, &i);
    CHECK(decompile(m, 2, 0) > 0, "decompile temp33");
    CHECK(strstr(hlsl, "r[33].xyzw = _v") && !strstr(hlsl, "r[1].xyzw = _v"), "temp 33 write stays r[33]");
    CHECK(compiles(hlsl), "temp33 compiles");

    /* H2-07: z-clip snap is floor(d + 0.5) (RSXVertexPrologue.glsl). */
    CHECK(strstr(hlsl, "float _zn = floor(_zd + 0.5);"), "zsnap floor");

    /* H2-01: no CMP_FIXUP on a non-NVIDIA host. */
    memset(m, 0, sizeof m);
    i = mov_out(0, src_in()); i.vec = 0x0B; i.src1 = src_const(); i.cst = 3; i.end = 1; enc(m, &i);
    rsx_vp_set_low_precision_compare(0);
    CHECK(decompile(m, 1, 0) > 0, "decompile slt non-nv");
    CHECK(strstr(hlsl, "float4 vp_cmpfix(float4 a) { return a; }"), "non-NVIDIA compare is plain");
    rsx_vp_set_low_precision_compare(1);

    /* T2-08: UN vertex textures divide by the texture size (baked or queried). */
    memset(m, 0, sizeof m);
    i = mov_out(0, src_in()); i.vec = 0x19; i.end = 1; enc(m, &i);
    {
        const u32 dims[4][2] = { { 256, 128 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
        rsx_vp_set_vtex_unnorm(1u, dims);
        CHECK(rsx_vp_decompile_program(m, 1, 0, 1u, 0xFFFFu, 0, hlsl, sizeof hlsl) > 0, "decompile txl un");
        CHECK(strstr(hlsl, "return float2(256.0, 128.0);") && strstr(hlsl, ").xy / _rsx_vtex_size0(), 0.0)"),
              "UN vertex texture baked size");
        CHECK(compiles(hlsl), "txl un compiles");
        rsx_vp_set_vtex_unnorm(1u, NULL);
        CHECK(rsx_vp_decompile_program(m, 1, 0, 1u, 0xFFFFu, 0, hlsl, sizeof hlsl) > 0, "decompile txl un dyn");
        CHECK(strstr(hlsl, "rsx_vtex0.GetDimensions(w, h)"), "UN vertex texture queried size");
        CHECK(compiles(hlsl), "txl un dyn compiles");
        rsx_vp_set_vtex_3d_mask(1u);
        CHECK(rsx_vp_decompile_program(m, 1, 0, 1u, 0xFFFFu, 0, hlsl, sizeof hlsl) > 0, "decompile txl un 3d");
        CHECK(!strstr(hlsl, "_rsx_vtex_size0"), "UN ignored on 3D units");
        rsx_vp_set_vtex_3d_mask(0);
        rsx_vp_set_vtex_unnorm(0, NULL);
        CHECK(rsx_vp_decompile_program(m, 1, 0, 1u, 0xFFFFu, 0, hlsl, sizeof hlsl) > 0, "decompile txl norm");
        CHECK(!strstr(hlsl, "_rsx_vtex_size0"), "normalized vertex texture unchanged");
    }
}

/* ---- FP5: GPU vertex fetch --------------------------------------------- */
static u32 s_rng = 0x12345678u;
static u32 rnd(void) { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }

/* The fetch helpers of a fetch-mode decompile (everything before main), run
 * as a compute shader on WARP: out[(vid * 16 + i)] = vp_fetch(i, vid). */
#define VF_STREAM 8192u
#define VF_VERTS 48u
typedef struct {
    ID3D11Device* dev; ID3D11DeviceContext* ctx;
    ID3D11Buffer *s[2], *cb, *out, *rb;
    ID3D11ShaderResourceView* srv[2];
    ID3D11UnorderedAccessView* uav;
    ID3D11ComputeShader* cs;
} VfGpu;

static void vf_release(VfGpu* g)
{
#define VF_REL(x) do { if (x) { (x)->lpVtbl->Release(x); (x) = NULL; } } while (0)
    for (int k = 0; k < 2; k++) { VF_REL(g->srv[k]); VF_REL(g->s[k]); }
    VF_REL(g->cb); VF_REL(g->out); VF_REL(g->rb); VF_REL(g->uav); VF_REL(g->cs);
    VF_REL(g->ctx); VF_REL(g->dev);
#undef VF_REL
}

static int vf_gpu_init(VfGpu* g, int mode, const char* prefix, const u8* const streams[2])
{
    memset(g, 0, sizeof *g);
    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
                                 &g->dev, NULL, &g->ctx))) return 0;
    static char cs[64 * 1024];
    snprintf(cs, sizeof cs,
        "%s"
        "RWByteAddressBuffer vf_out : register(u0);\n"
        "[numthreads(1, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
        "    [loop] for (uint i = 0u; i < 16u; i++)\n"
        "        vf_out.Store4((id.x * 16u + i) * 16u, asuint(vp_fetch(i, id.x)));\n"
        "}\n", prefix);
    ID3DBlob *code = NULL, *err = NULL;
    if (FAILED(D3DCompile(cs, strlen(cs), "vf", NULL, NULL, "main", "cs_5_0", 0, 0, &code, &err))) {
        if (err) printf("%s\n", (const char*)err->lpVtbl->GetBufferPointer(err));
        if (err) err->lpVtbl->Release(err);
        return -1;
    }
    g->dev->lpVtbl->CreateComputeShader(g->dev, code->lpVtbl->GetBufferPointer(code),
                                        code->lpVtbl->GetBufferSize(code), NULL, &g->cs);
    code->lpVtbl->Release(code);
    for (int k = 0; k < 2; k++) {
        D3D11_BUFFER_DESC bd = { VF_STREAM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,
                                 mode == RSX_VP_FETCH_BYTEADDR ? D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS : 0, 0 };
        D3D11_SUBRESOURCE_DATA sd = { streams[k], 0, 0 };
        g->dev->lpVtbl->CreateBuffer(g->dev, &bd, &sd, &g->s[k]);
        D3D11_SHADER_RESOURCE_VIEW_DESC vd;
        memset(&vd, 0, sizeof vd);
        if (mode == RSX_VP_FETCH_BYTEADDR) {
            vd.Format = DXGI_FORMAT_R32_TYPELESS;
            vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            vd.BufferEx.NumElements = VF_STREAM / 4u;
            vd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        } else {
            vd.Format = DXGI_FORMAT_R32_UINT;
            vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            vd.Buffer.NumElements = VF_STREAM / 4u;
        }
        g->dev->lpVtbl->CreateShaderResourceView(g->dev, (ID3D11Resource*)g->s[k], &vd, &g->srv[k]);
    }
    D3D11_BUFFER_DESC cbd = { RSX_VP_FETCH_LAYOUT_BYTES, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    g->dev->lpVtbl->CreateBuffer(g->dev, &cbd, NULL, &g->cb);
    const u32 ob = VF_VERTS * 16u * 16u;
    D3D11_BUFFER_DESC obd = { ob, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0,
                              D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0 };
    g->dev->lpVtbl->CreateBuffer(g->dev, &obd, NULL, &g->out);
    D3D11_BUFFER_DESC rbd = { ob, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0 };
    g->dev->lpVtbl->CreateBuffer(g->dev, &rbd, NULL, &g->rb);
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud;
    memset(&ud, 0, sizeof ud);
    ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = ob / 4u;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    g->dev->lpVtbl->CreateUnorderedAccessView(g->dev, (ID3D11Resource*)g->out, &ud, &g->uav);
    if (!g->cs || !g->srv[0] || !g->srv[1] || !g->cb || !g->uav || !g->rb) return -1;
    return 1;
}

static int vf_gpu_run(VfGpu* g, const rsx_vp_fetch_layout* l, float* out)
{
    ID3D11DeviceContext* c = g->ctx;
    c->lpVtbl->UpdateSubresource(c, (ID3D11Resource*)g->cb, 0, NULL, l, 0, 0);
    /* t30/t31, b2 */
    c->lpVtbl->CSSetShaderResources(c, 30, 2, g->srv);
    c->lpVtbl->CSSetConstantBuffers(c, 2, 1, &g->cb);
    c->lpVtbl->CSSetUnorderedAccessViews(c, 0, 1, &g->uav, NULL);
    c->lpVtbl->CSSetShader(c, g->cs, NULL, 0);
    c->lpVtbl->Dispatch(c, VF_VERTS, 1, 1);
    c->lpVtbl->CopyResource(c, (ID3D11Resource*)g->rb, (ID3D11Resource*)g->out);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(c->lpVtbl->Map(c, (ID3D11Resource*)g->rb, 0, D3D11_MAP_READ, 0, &m))) return 0;
    memcpy(out, m.pData, VF_VERTS * 16u * 16u);
    c->lpVtbl->Unmap(c, (ID3D11Resource*)g->rb, 0);
    return 1;
}

static int vf_same(float a, float b)
{
    u32 ua, ub; memcpy(&ua, &a, 4); memcpy(&ub, &b, 4);
    if (ua == ub) return 1;
    if (a != a && b != b) return 1;
    const float d = a - b;
    const float m = (a < 0 ? -a : a) > (b < 0 ? -b : b) ? (a < 0 ? -a : a) : (b < 0 ? -b : b);
    return (d < 0 ? -d : d) <= m * 2e-7f;
}

static void test_gpu_fetch(void)
{
    u8 m[16];
    Ins i = mov_out(0, src_in()); i.input = 3; i.end = 1; enc(m, &i);

    /* Off (the default): the CPU-decoded VSInput, byte-identical to before. */
    CHECK(rsx_vp_gpu_fetch() == RSX_VP_FETCH_OFF, "fetch off by default");
    CHECK(decompile(m, 1, 0) > 0, "decompile off");
    static char off_text[256 * 1024];
    strcpy(off_text, hlsl);
    CHECK(strstr(hlsl, "VSOutput main(VSInput input) {") && !strstr(hlsl, "vp_fetch") &&
          !strstr(hlsl, "VPFetch"), "off: VSInput, no fetch helpers");

    static const int modes[2] = { RSX_VP_FETCH_BYTEADDR, RSX_VP_FETCH_TEXEL };
    for (int mi = 0; mi < 2; mi++) {
        const int mode = modes[mi];
        rsx_vp_set_gpu_fetch(mode);
        CHECK(decompile(m, 1, 0) > 0, "decompile fetch %d", mode);
        CHECK(strstr(hlsl, "VSOutput main(uint _vid : SV_VertexID) {") && !strstr(hlsl, "VSInput") &&
              !strstr(hlsl, "input."), "fetch %d: SV_VertexID main, no vertex inputs", mode);
        CHECK(strstr(hlsl, "v[0]=vp_fetch(0u, _vid);") && strstr(hlsl, "v[15]=vp_fetch(15u, _vid);"),
              "fetch %d: every attribute fetched", mode);
        CHECK(strstr(hlsl, "cbuffer VPFetch : register(b2)") &&
              strstr(hlsl, mode == RSX_VP_FETCH_TEXEL ? "Buffer<uint> vp_fstream0 : register(t30);"
                                                      : "ByteAddressBuffer vp_fstream0 : register(t30);") &&
              strstr(hlsl, "[[vk::binding(1, 2)]]"), "fetch %d: stream and layout declarations", mode);
        CHECK(compiles(hlsl), "fetch %d compiles", mode);
        /* Compact: only the attributes the program reads. */
        CHECK(rsx_vp_decompile_program(m, 1, 0, 0, 1u << 3, 1, hlsl, sizeof hlsl) > 0, "decompile compact %d", mode);
        CHECK(strstr(hlsl, "v[3]=vp_fetch(3u, _vid);") && !strstr(hlsl, "v[0]=vp_fetch") &&
              strstr(hlsl, "v[_k]=float4(0,0,0,1)"), "fetch %d compact: v[3] only", mode);
        CHECK(compiles(hlsl), "fetch %d compact compiles", mode);
    }
    rsx_vp_set_gpu_fetch(RSX_VP_FETCH_OFF);
    CHECK(decompile(m, 1, 0) > 0 && !strcmp(hlsl, off_text), "off again: same text");

    /* Layout fill (RPCS3 fill_vertex_layout_state rules). */
    rsx_vp_fetch_attr_src src[16];
    memset(src, 0, sizeof src);
    src[0].format = RSX_VP_FETCH_FORMAT(2, 3, 12, 0); src[0].offset = -4; src[0].big_endian = 1;
    src[1].format = RSX_VP_FETCH_FORMAT(6, 4, 4, 1);  src[1].offset = 8;
    src[2].format = RSX_VP_FETCH_FORMAT(4, 4, 0, 5);  src[2].offset = 16; src[2].stream = 1;
    src[3].format = RSX_VP_FETCH_FORMAT(1, 2, 8, 3);
    src[4].format = RSX_VP_FETCH_FORMAT(1, 2, 8, 3);
    src[5].format = RSX_VP_FETCH_FORMAT(3, 0, 8, 1);
    rsx_vp_fetch_layout l;
    rsx_vp_fetch_layout_fill(&l, src, 1u << 4, -7, 9, 1);
    CHECK(sizeof l == RSX_VP_FETCH_LAYOUT_BYTES, "layout is 144 bytes");
    CHECK(l.hdr[0] == (u32)-7 && l.hdr[1] == 9u && l.hdr[2] == (RSX_VP_FETCH_WRAP20 | 4u) && l.hdr[3] == 0,
          "header %x %x %x", l.hdr[0], l.hdr[1], l.hdr[2]);
    CHECK(l.attr[0][0] == (12u | 2u << 8 | 3u << 11 | 1u << 16 | RSX_VP_FETCH_SWAP) && l.attr[0][1] == (u32)-4,
          "F3 BE, frequency 0 divides by 1: %x", l.attr[0][0]);
    CHECK(l.attr[1][0] == (4u | 6u << 8 | 1u << 11 | 1u << 16), "CMP is size 1: %x", l.attr[1][0]);
    CHECK(l.attr[2][0] == (4u << 8 | 4u << 11), "stride 0 is frequency 0: %x", l.attr[2][0]);
    CHECK(l.attr[3][0] == (8u | 1u << 8 | 2u << 11 | 3u << 16), "divider: %x", l.attr[3][0]);
    CHECK(l.attr[4][0] == (8u | 1u << 8 | 2u << 11 | 3u << 16 | RSX_VP_FETCH_MODULO), "modulo: %x", l.attr[4][0]);
    CHECK(l.attr[5][0] == 0 && l.attr[6][0] == 0, "size 0 / type 0 absent");

    /* CPU reference against hand-decoded values (rsx_vertex_compact rules). */
    {
        static const u8 be[] = {
            0x3F, 0x80, 0x00, 0x00,  0xC0, 0x00, 0x00, 0x00,   /* F 1.0, -2.0 */
            0x80, 0x00, 0x7F, 0xFF,                             /* S1 -32768, 32767 */
            0x3C, 0x00, 0x00, 0x01,                             /* SF 1.0, 2^-24 */
            0xFF, 0x80,                                         /* UB */
            0xFF, 0xFE,                                         /* S32K -2 */
        };
        const u8* const st[2] = { be, be };
        rsx_vp_fetch_attr_src s2[16];
        memset(s2, 0, sizeof s2);
        s2[0].format = RSX_VP_FETCH_FORMAT(2, 2, 0, 0); s2[0].big_endian = 1;
        s2[1].format = RSX_VP_FETCH_FORMAT(1, 2, 0, 0); s2[1].offset = 8; s2[1].big_endian = 1;
        s2[2].format = RSX_VP_FETCH_FORMAT(3, 2, 0, 0); s2[2].offset = 12; s2[2].big_endian = 1;
        s2[3].format = RSX_VP_FETCH_FORMAT(4, 2, 0, 0); s2[3].offset = 16;
        s2[4].format = RSX_VP_FETCH_FORMAT(5, 1, 0, 0); s2[4].offset = 18; s2[4].big_endian = 1;
        s2[5].format = RSX_VP_FETCH_FORMAT(7, 4, 0, 0); s2[5].offset = 16;
        s2[6].format = RSX_VP_FETCH_FORMAT(6, 1, 0, 0); s2[6].big_endian = 0;
        s2[7].format = RSX_VP_FETCH_FORMAT(1, 1, 0, 0); s2[7].offset = 10; s2[7].big_endian = 1;
        rsx_vp_fetch_layout l2;
        rsx_vp_fetch_layout_fill(&l2, s2, 0, 0, 0, 0);
        float v[4];
        rsx_vp_fetch_reference(&l2, 0, 0, st, v);
        CHECK(v[0] == 1.0f && v[1] == -2.0f && v[2] == 0.0f && v[3] == 1.0f, "F2");
        rsx_vp_fetch_reference(&l2, 1, 0, st, v);
        CHECK(v[0] == (-32768.0f + 0.5f) / 32767.5f && v[1] == 1.0f && v[2] == 0.5f / 32767.5f && v[3] == 1.0f,
              "S1 %g %g %g", v[0], v[1], v[2]);
        rsx_vp_fetch_reference(&l2, 2, 0, st, v);
        CHECK(v[0] == 1.0f && v[1] == 1.0f / 16777216.0f && v[3] == 1.0f, "SF with a denormal");
        rsx_vp_fetch_reference(&l2, 3, 0, st, v);
        CHECK(v[0] == 1.0f && v[1] == 128.0f / 255.0f && v[2] == 0.0f && v[3] == 1.0f, "UB");
        rsx_vp_fetch_reference(&l2, 4, 0, st, v);
        CHECK(v[0] == -2.0f && v[1] == 0.0f && v[3] == 1.0f, "S32K");
        rsx_vp_fetch_reference(&l2, 5, 0, st, v);
        CHECK(v[0] == 255.0f && v[1] == 128.0f && v[2] == 255.0f && v[3] == 254.0f, "UB256 size 4 keeps w");
        /* CMP word 0x0000803F (LE bytes 3F 80 00 00): x = 0x03F, y = 0x010, z = 0 */
        rsx_vp_fetch_reference(&l2, 6, 0, st, v);
        CHECK(v[0] == (float)(0x3F << 5) / 32767.0f && v[1] == (float)(0x10 << 5) / 32767.0f &&
              v[2] == 0.0f && v[3] == 1.0f, "CMP %g %g %g", v[0], v[1], v[2]);
        rsx_vp_fetch_reference(&l2, 7, 0, st, v);
        CHECK(v[0] == 1.0f && v[1] == 0.5f / 32767.5f, "S1 size 1 pads 0.5/32767.5");
        rsx_vp_fetch_reference(&l2, 9, 0, st, v);
        CHECK(v[0] == 0.0f && v[3] == 1.0f, "absent attribute (0,0,0,1)");
    }

    /* The shader's decode against the reference, on WARP: random streams and
     * layouts (every type, size, swap, unaligned offsets and strides, both
     * streams, divide/modulo, wrap20), both stream forms. */
    static u8 sbytes[2][VF_STREAM];
    for (int k = 0; k < 2; k++) for (u32 b = 0; b < VF_STREAM; b++) sbytes[k][b] = (u8)rnd();
    /* Plenty of finite F / SF values, not only random bits. */
    for (u32 b = 0; b + 4 <= VF_STREAM; b += 8) { sbytes[0][b] = 0x3F; sbytes[1][b + 2] = 0x41; }
    const u8* const st[2] = { sbytes[0], sbytes[1] };
    static float gpu[VF_VERTS * 16 * 4];
    for (int mi = 0; mi < 2; mi++) {
        const int mode = modes[mi];
        rsx_vp_set_gpu_fetch(mode);
        decompile(m, 1, 0);
        rsx_vp_set_gpu_fetch(RSX_VP_FETCH_OFF);
        char* mainp = strstr(hlsl, "VSOutput main(");
        if (!mainp) { CHECK(0, "no main"); continue; }
        *mainp = 0;
        VfGpu g;
        const int ok = vf_gpu_init(&g, mode, hlsl, st);
        if (ok == 0) { printf("SKIP gpu fetch on WARP (no device)\n"); vf_release(&g); return; }
        CHECK(ok > 0, "fetch %d: WARP compute pipeline", mode);
        if (ok < 0) { vf_release(&g); continue; }
        int bad = 0, checked = 0;
        for (int round = 0; round < 24; round++) {
            rsx_vp_fetch_attr_src rs[16];
            memset(rs, 0, sizeof rs);
            for (u32 a = 0; a < 16; a++) {
                const u32 type = rnd() % 8u, size = 1u + rnd() % 4u;
                const u32 stride = (rnd() % 6u) ? 1u + rnd() % 40u : 0u;
                const u32 freq = (rnd() % 3u) ? rnd() % 2u : 2u + rnd() % 4u;
                rs[a].format = RSX_VP_FETCH_FORMAT(type, size, stride, freq);
                rs[a].offset = (s32)(rnd() % 64u);
                rs[a].stream = (u8)(rnd() & 1u);
                rs[a].big_endian = (u8)(rnd() & 1u);
            }
            const int wrap = round % 4 == 3;
            const s32 db = wrap ? (s32)(0x7FF00000u + rnd() % 12u) : (s32)(rnd() % 12u);
            const s32 mb = wrap ? (s32)(0x12300000u + rnd() % 12u) : (s32)(rnd() % 12u);
            rsx_vp_fetch_layout rl;
            rsx_vp_fetch_layout_fill(&rl, rs, rnd() & 0xFFFFu, db, mb, wrap);
            if (!vf_gpu_run(&g, &rl, gpu)) { CHECK(0, "gpu run"); break; }
            for (u32 vid = 0; vid < VF_VERTS; vid++)
                for (u32 a = 0; a < 16; a++) {
                    float ref[4];
                    rsx_vp_fetch_reference(&rl, a, vid, st, ref);
                    const float* gv = &gpu[(vid * 16u + a) * 4u];
                    checked++;
                    if (vf_same(ref[0], gv[0]) && vf_same(ref[1], gv[1]) &&
                        vf_same(ref[2], gv[2]) && vf_same(ref[3], gv[3])) continue;
                    if (++bad <= 4)
                        printf("  mode %d round %d vid %u attr %u (fmt %08X off %u): ref %g %g %g %g gpu %g %g %g %g\n",
                               mode, round, vid, a, rl.attr[a][0], rl.attr[a][1], ref[0], ref[1], ref[2], ref[3],
                               gv[0], gv[1], gv[2], gv[3]);
                }
        }
        CHECK(bad == 0, "fetch %d: shader decode == reference (%d/%d)", mode, checked - bad, checked);
        printf("gpu fetch mode %d: %d/%d attribute reads match the reference\n", mode, checked - bad, checked);
        vf_release(&g);
    }
}

static void test_corpus(void)
{
    static const char* dirs[] = {
        "..\\..\\shadercache\\rpcs3\\shaders_cache\\raw",
    };
    int total = 0, ok = 0;
    for (u32 d = 0; d < sizeof dirs / sizeof dirs[0]; d++) {
        char pat[512];
        snprintf(pat, sizeof pat, "%s\\*.vp", dirs[d]);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            char path[600];
            snprintf(path, sizeof path, "%s\\%s", dirs[d], fd.cFileName);
            FILE* f = fopen(path, "rb");
            if (!f) continue;
            static u8 buf[544 * 16];
            const u32 n = (u32)fread(buf, 1, sizeof buf, f);
            fclose(f);
            total++;
            rsx_vp_program_info info;
            const int c = rsx_vp_analyse_program(buf, n / 16u, 0, &info);
            if (c <= 0) { printf("FAIL analyse %s\n", fd.cFileName); s_fail++; continue; }
            /* The one branching program: BRB/BRI back edges, single end at 514. */
            if (!strcmp(fd.cFileName, "AD6A2788EE61A799.vp"))
                CHECK(c == 515 && info.base == 0 && info.has_branch, "ubershader range %d", c);
            if (decompile(buf, n / 16u, 0) <= 0) { printf("FAIL decompile %s\n", fd.cFileName); s_fail++; continue; }
            if (!compiles(hlsl)) { printf("FAIL compile %s\n", fd.cFileName); s_fail++; continue; }
            ok++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    printf("corpus: %d/%d vertex programs ok\n", ok, total);
}

int main(void)
{
    test_rules();
    test_gpu_fetch();
    test_corpus();
    printf(s_fail ? "%d FAIL\n" : "all passed\n", s_fail);
    return s_fail != 0;
}
