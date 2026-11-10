/*
 * ps3recomp - RSX Vertex Program (NV40 ISA) → HLSL decompiler
 *
 * Translates an RSX vertex shader (NVIDIA NV40 "nvfx" vertex program, a VLIW
 * that co-issues one vector ALU op and one scalar ALU op per instruction) into
 * an HLSL vertex shader.
 *
 * Encoding reference: RPCS3 RSXVertexProgram.h (D0..D3 bitfields) + Mesa
 * nouveau nvfx_shader.h opcode tables. Words are stored little-endian (RPCS3
 * native order), 4 words / 16 bytes per instruction, no inline constants
 * (VP constants live in a separate bank), program ends at the D3.end bit.
 *
 * Self-contained: no D3D12 dependency, build/test standalone.
 */
#ifndef PS3RECOMP_RSX_VP_DECOMPILER_H
#define PS3RECOMP_RSX_VP_DECOMPILER_H

#include "ps3emu/ps3types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Number of instructions in the program at `ucode` (each 16 bytes), up to and
 * including the one with the D3.end bit, bounded by max_bytes. 0 if none. */
u32 rsx_vp_program_size_instrs(const u8* ucode, u32 max_bytes);

typedef struct rsx_vp_input_analysis {
    u32 input_mask;
    int exact;
} rsx_vp_input_analysis;

/* Report the guest input registers statically read by the program.  `exact`
 * is zero and input_mask is 0xFFFF when an unsupported opcode/control-flow
 * construct makes the result unsafe to narrow.  Returns the instruction
 * count, or -1 for invalid/non-terminated input. */
int rsx_vp_analyze_inputs(const u8* ucode, u32 max_bytes,
                          rsx_vp_input_analysis* analysis);
/* RPCS3 VertexProgramDecompiler constant-range analysis (COV-P9): sets
 * *const_count to one past the highest transform constant the program reads
 * directly, or 468 (the whole bank) when it indexes constants, uses flow
 * control or does not terminate. An engine may then upload only
 * vp_c[0 .. const_count). Returns the instruction count, or -1. */
int rsx_vp_analyze_constants(const u8* ucode, u32 max_bytes, u32* const_count);

/* Decompile an NV40 vertex program into an HLSL vertex shader.
 *   ucode    : VP bytecode (little-endian words, as in RPCS3's shader cache).
 *   max_bytes: safety bound.
 *   out      : caller buffer for generated HLSL (NUL-terminated).
 *   out_size : size of out in bytes.
 * Returns instruction count (>=0), or -1 on error (null args / overflow).
 * Emits `VSOutput main(VSInput input)`: 16 float4 inputs (ATTR0..15), a
 * `cbuffer VPConst : register(b0)` with 512 vec4 constants + vp_posscale/
 * vp_posoffset (the RSX viewport transform mapped to D3D clip space; the
 * caller computes them per draw), SV_Position + COLOR0/1 + FOG + TEXCOORD0..7
 * varyings routed per the NV40 output register map (o0/o1/o2/o5/o7..o14).
 * Condition codes and flow control follow RPCS3's VertexProgramDecompiler. TXL
 * is stubbed to zero unless rsx_vp_decompile_ex receives a bound-unit mask. */
int rsx_vp_decompile(const u8* ucode, u32 max_bytes, char* out, u32 out_size);

/* The VP slot the next program decompiled starts at (NV4097 transform
 * program start). Branch targets are absolute slots; this makes them relative
 * to the ucode pointer handed in. Defaults to 0. */
void rsx_vp_set_branch_base(u32 start_slot);

/* Emit SV_ClipDistance0..5 for the user clip planes (RPCS3 reg_table UC0-5).
 * Off by default: a Vulkan backend turns it on only once the device has
 * shaderClipDistance enabled. */
void rsx_vp_set_clip_distances(int enabled);
/* Vertex texture units (bits 0-3) whose dimension is 1: TXL reads them at
 * float2(coord.x, 0.5) (RPCS3 VERTEX_TEXTURE_FETCH1D). Default 0. */
void rsx_vp_set_vtex_1d_mask(u32 mask);
/* Vertex texture units whose dimension is 3 / cubemap: declared Texture3D /
 * TextureCube and sampled at coord.xyz (RPCS3 VERTEX_TEXTURE_FETCH3D/CUBE).
 * Default 0 (every unit a Texture2D). The 1D mask wins over both. */
void rsx_vp_set_vtex_3d_mask(u32 mask);
void rsx_vp_set_vtex_cube_mask(u32 mask);
/* RPCS3 VKVertexProgram.cpp:367: CMP_FIXUP around SLT..SNE only on NVIDIA
 * hosts. Default 1 (always on); pass vendorID == 0x10DE. RSX_VP_CMP_FIXUP=0
 * still disables it everywhere. */
void rsx_vp_set_low_precision_compare(int enabled);
/* CELL_GCM_TEXTURE_UN vertex units (bits 0-3; RPCS3 VKDraw.cpp:593-619
 * unnormalizedCoordinates). 1D/2D units in the mask sample at coord / size:
 * dims[u] = {width, height} of the guest texture baked into the shader, or
 * {0,0} / dims NULL to use the bound image's GetDimensions. 3D/cube units
 * ignore it. Default 0. RSX_VP_VTEX_UN=0 disables. */
void rsx_vp_set_vtex_unnorm(u32 mask, const u32 dims[4][2]);

/* vp_branch, the uint4 after vp_posoffset:
 *   x = TRANSFORM_BRANCH_BITS (0x1FF8)
 *   y = VERTEX_ATTRIB_OUTPUT_MASK (0x1FF4)
 *   z = USER_CLIP_PLANE_CONTROL (0x1478) | RSX_VP_BRANCH_ENV_VALID
 *   w = POINT_SIZE (0x1EE0) float bits
 * Without RSX_VP_BRANCH_ENV_VALID in z (callers that fill only x), every
 * output stays enabled, no plane clips and the point size is 1. */
#define RSX_VP_BRANCH_ENV_VALID 0x80000000u
/* vp_posscale.w (otherwise unused): a multiplier for the point size the
 * program writes itself, the backend's resolution scale; 0 or less means 1. */

/* Bit N in vtex_mask declares NV40 2D vertex-texture unit N at t(16+N),
 * sampler sN, and turns TXL for that unit into SampleLevel(..., LOD 0).
 * Unmasked units retain the defined-zero fallback. */
int rsx_vp_decompile_ex(const u8* ucode, u32 max_bytes, u32 vtex_mask,
                        char* out, u32 out_size);

/* Compact-input variant.  The generated VSInput declares only the original
 * ATTRn semantics selected by input_mask.  The mask is checked against a
 * fresh static analysis; any uncertainty or missing referenced bit falls
 * back to all 16 inputs. */
int rsx_vp_decompile_compact_ex(
    const u8* ucode, u32 max_bytes, u32 vtex_mask, u32 input_mask,
    char* out, u32 out_size);

/* RPCS3 vertex_program_utils::analyse_vertex_program. Walks the program from
 * `entry` through transform program memory (vp_mem, total_slots 16-byte
 * slots, at most 544): static BRI and CAL/CLI/CLB/RET are followed,
 * conditional BRI/BRB targets extend the range and are walked afterwards, and
 * an end bit stops the walk only once no branch reaches past it. The result
 * is the touched slot range [base, base + count), the entry relative to base
 * and a live-instruction mask (bit i = slot base + i). Returns count or -1. */
#define RSX_VP_MASK_WORDS 17
typedef struct rsx_vp_program_info {
    u32 base;
    u32 count;
    u32 entry;
    int has_branch;
    u32 mask[RSX_VP_MASK_WORDS];
} rsx_vp_program_info;
int rsx_vp_analyse_program(const u8* vp_mem, u32 total_slots, u32 entry,
                           rsx_vp_program_info* info);

/* Analyse (above) and decompile the program that starts at entry_slot, with
 * branch targets relative to the analysed base and dead slots skipped; a
 * program end does not stop decoding, it returns. compact_inputs as for
 * rsx_vp_decompile_compact_ex (branching programs keep all 16 inputs). */
int rsx_vp_decompile_program(const u8* vp_mem, u32 total_slots, u32 entry_slot,
                             u32 vtex_mask, u32 input_mask, int compact_inputs,
                             char* out, u32 out_size);

/* ---- GPU vertex fetch (RPCS3 RSXVertexFetch.glsl fetch_attribute) ----
 *
 * Off by default (mode RSX_VP_FETCH_OFF): the shader takes 16 float4 vertex
 * inputs (ATTR0..15) the engine decoded on the CPU, exactly as before. With
 * rsx_vp_set_gpu_fetch(RSX_VP_FETCH_BYTEADDR or RSX_VP_FETCH_TEXEL) every
 * decompile entry point instead emits `VSOutput main(uint _vid : SV_VertexID)`
 * with no vertex inputs; each attribute the program reads is fetched from raw
 * (guest big-endian) bytes and decoded in the shader:
 *
 *   streams      persistent (0) and volatile (1), each 32-bit words:
 *     BYTEADDR   ByteAddressBuffer vp_fstream0 : register(t30)  -> SPIR-V
 *                StorageBuffer   set 2 binding 0 (stream 1: t31 / binding 1)
 *     TEXEL      Buffer<uint>      vp_fstream0 : register(t30)  -> SPIR-V
 *                UniformTexelBuffer (R32_UINT view) set 2 binding 0 / 1
 *   layout       cbuffer VPFetch : register(b2) -> set 0 binding 2, 144 bytes,
 *                exactly struct rsx_vp_fetch_layout below.
 *
 * The streams must hold at least 8 bytes past the last element read (an
 * unaligned element reads the following aligned word). */
#define RSX_VP_FETCH_OFF      0
#define RSX_VP_FETCH_BYTEADDR 1
#define RSX_VP_FETCH_TEXEL    2
void rsx_vp_set_gpu_fetch(int mode);
int rsx_vp_gpu_fetch(void);

/* The per-draw layout block (std140-compatible, 144 bytes):
 *   hdr[0]  div_bias (s32): element = (vid + div_bias) / frequency
 *   hdr[1]  mod_bias (s32): element = (vid + mod_bias) % frequency
 *   hdr[2]  bits 0-15: attribute i reads the volatile stream (1);
 *           bit 16 (RSX_VP_FETCH_WRAP20): (vid + bias) &= 0xFFFFF first
 *   hdr[3]  reserved, 0
 *   attr[i][0]  stride[0:8) | type[8:11) | size[11:14) | modulo[14] |
 *               swap[15] | frequency[16:32)   (type 0: attribute absent,
 *               reads (0,0,0,1); frequency 0: every vertex reads element 0)
 *   attr[i][1]  byte offset of element 0 in its stream (s32)
 * The shader reads attr[i] as vp_fattr[i >> 1].xy (even i) / .zw (odd i).
 * type is the RSX vertex type (1 S1, 2 F, 3 SF, 4 UB, 5 S32K, 6 CMP,
 * 7 UB256), size 1..4 (CMP: 1), swap 1 = big-endian data. */
#define RSX_VP_FETCH_WRAP20       0x10000u
#define RSX_VP_FETCH_MODULO       0x4000u
#define RSX_VP_FETCH_SWAP         0x8000u
#define RSX_VP_FETCH_LAYOUT_BYTES 144u
typedef struct rsx_vp_fetch_layout {
    u32 hdr[4];
    u32 attr[16][2];
} rsx_vp_fetch_layout;

/* One attribute's source, as the engine knows it. `format` is the
 * NV4097_SET_VERTEX_DATA_ARRAY_FORMAT word (type[3:0], size[7:4],
 * stride[15:8], frequency[31:16]); size 0 or type 0 = no data. For a
 * constant register (NV4097_SET_VERTEX_DATA4F_M etc.) pass stride 0 and the
 * register's 16 bytes in the volatile stream (host-order floats: big_endian
 * 0). `offset` is element 0's byte offset in the chosen stream. */
typedef struct rsx_vp_fetch_attr_src {
    u32 format;
    s32 offset;
    u8  stream;       /* 0 persistent, 1 volatile */
    u8  big_endian;   /* 1: guest byte order (swap in the shader) */
    u8  pad[2];
} rsx_vp_fetch_attr_src;
#define RSX_VP_FETCH_FORMAT(type, size, stride, freq) \
    ((u32)(type) | ((u32)(size) << 4) | ((u32)(stride) << 8) | ((u32)(freq) << 16))

/* RPCS3 draw_command_processor::fill_vertex_layout_state: stride 0 is a
 * single element (frequency 0); frequency 0/1 divides by 1; frequency > 1
 * divides, or (bit set in modulo_mask, NV4097_SET_FREQUENCY_DIVIDER_OPERATION)
 * takes the modulo; CMP is one 4-byte element. Biases and wrap20 go to hdr:
 * the hardware rule (rsx_vertex_element_index) is div_bias = mod_bias =
 * VERTEX_DATA_BASE_INDEX with wrap20 = 1; RPCS3's is div_bias =
 * -vertex_base_index, mod_bias = vertex_index_offset, wrap20 = 0. */
void rsx_vp_fetch_layout_fill(rsx_vp_fetch_layout* out, const rsx_vp_fetch_attr_src src[16],
                              u32 modulo_mask, s32 div_bias, s32 mod_bias, int wrap20);

/* CPU reference of the shader's decode, for tests and engine fallbacks:
 * attribute i of vertex `vid` from streams s[0], s[1] (byte arrays). */
void rsx_vp_fetch_reference(const rsx_vp_fetch_layout* l, u32 i, u32 vid,
                            const u8* const s[2], float out[4]);

/* Mnemonics for the vector / scalar opcode fields ("?" if unknown). */
const char* rsx_vp_vec_name(u32 op);
const char* rsx_vp_sca_name(u32 op);

#ifdef __cplusplus
}
#endif
#endif /* PS3RECOMP_RSX_VP_DECOMPILER_H */
