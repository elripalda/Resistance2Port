#!/usr/bin/env python3
"""v143: rewrite lifted SPU block hops as SPU_HOP (see runtime/spu/spu_context.h).

    { ctx->pc = 0x4218; g_spu_trampoline_fn = <img>_spu_func_00004218; return; }
 -> { SPU_HOP(ctx, 0x4218, <img>_spu_func_00004218); }

SPU_HOP jumps straight to the next block (a clang musttail call) when the drain
loop would do nothing but call it, and falls back to the trampoline otherwise.
Indirect branches (spu_indirect_branch) are left alone: the drain must see
them to recognise a return to its caller.

  tools/spu_directhop.py project/spu/*/spu_recomp.c      (in place, idempotent)
"""
import re, sys
# PIC lifts (spu_lifter.py --pic) re-publish the module base and bias the PC:
#   { ctx->ls_bias = _b; ctx->pc = 0x4218u + _b; g_spu_trampoline_fn = F; return; }
PAT = re.compile(r'\{ (ctx->ls_bias = _b; )?ctx->pc = (0x[0-9A-F]+(?:u \+ _b)?); '
                 r'g_spu_trampoline_fn = ([A-Za-z0-9_]+_spu_func_[0-9A-F]+); return; \}')
for f in sys.argv[1:]:
    s = open(f).read()
    s2, n = PAT.subn(lambda m: '{ %sSPU_HOP(ctx, %s, %s); }' % (m.group(1) or '', m.group(2), m.group(3)), s)
    if n:
        open(f, 'w').write(s2)
    print(f, n)
