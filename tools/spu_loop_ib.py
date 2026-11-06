#!/usr/bin/env python3
"""Resistance port (v193): computed loop branches stay inside the function.

Software-pipelined SPU loops often close with a computed branch instead of a
conditional one:

    ila   $35, loop_head
    ceqi  $34, $6, 0
    selb  $39, $35, $0, $34       # loop head, or the return address when done
    bi    $39

The lifter lowers `bi $39` as an indirect dispatch: store the dirty registers,
return to the drain loop, resolve the PC in spu_indirect_branch, enter the
per-chunk stub, hop back into the merged group, reload every live register and
take the group-entry switch to the loop head -- every iteration. In the
Resistance 2 audio synth (jobmod_a8d79dd61f50_e80, func_00004800) that round
trip cost about as much as the loop body itself.

This pass, run after spu_group_entry.py, rewrites such a site as

    { _SWB; const uint32_t _it = S_39._u32[0];
      if ((_it & 0x3FFFF) == 0x4980 && spu_hop_direct_ok(ctx, 0x4980)) {
          ctx->pc = _it; ctx->steps++; goto loc_00004980; }
      ctx->pc = _it; g_spu_trampoline_fn = spu_indirect_branch; return; }

for every candidate address that is (a) a label of the same function that is
also a dispatch entry of it (the function's own start, or an inner entry from
the group-entry switch), and (b) loaded as an immediate (ila / il / ilhu+iohl)
somewhere in the function. That is exactly where the dispatcher would land:
the per-chunk stub hops into this group and the switch jumps to the label, so
the register state is identical (the stores still run; the reload is skipped
because the locals already hold the stored values). The guard is the one the
stub itself uses (spu_hop_direct_ok: no pending interrupt, direct hops on), so
interrupts and SPU_DIRECT_HOP=0 still take the dispatcher. Sites that also flip
the interrupt enable (bie/bid) are left alone.

  spu_loop_ib.py in.c out.c
"""
import re, sys

FHEAD = re.compile(r'^void (\w+_spu_func_)([0-9A-F]{8})\(spu_context\* ctx\) \{\n', re.M)
MARK = '/* spu loop ib v193 */'
SITE = re.compile(r'\{ ((?:_SWB\d*; )?)ctx->pc = (S_\d+)\._u32\[0\]; '
                  r'g_spu_trampoline_fn = spu_indirect_branch; return; \}')
SWITCH = re.compile(r'case 0x([0-9A-F]{8})u: goto loc_\1;')
LABEL = re.compile(r'^loc_([0-9A-F]{8}):$', re.M)
ILA = re.compile(r'spu_ila?\(0x([0-9A-Fa-f]+)\)|spu_ila?\((\d+)\)')
ILHU = re.compile(r'(S_\d+) = spu_ilhu\((0x[0-9A-Fa-f]+|\d+)\);')
IOHL = re.compile(r'(S_\d+) = spu_iohl\(\1, (0x[0-9A-Fa-f]+|\d+)\);')
LOAD = re.compile(r'^\s+(u128 S_\d+( = ctx->gpr\[\d+\])?;|const uint32_t _b = ctx->ls_bias; \(void\)_b;)$')
GE = 'if (__builtin_expect(g_spu_grp_entry != 0, 0))'


def consts(body):
    out = set()
    for m in ILA.finditer(body):
        out.add(int(m.group(1), 16) if m.group(1) else int(m.group(2)))
    hi = {}
    for m in ILHU.finditer(body):
        hi.setdefault(m.group(1), set()).add(int(m.group(2), 0) & 0xFFFF)
    for m in IOHL.finditer(body):
        for h in hi.get(m.group(1), ()):
            out.add((h << 16) | (int(m.group(2), 0) & 0xFFFF))
    return {c & 0x3FFFF for c in out}


def transform(body, addr, pic):
    """body = function text between '{\\n' and the closing '}'."""
    sites = list(SITE.finditer(body))
    if not sites:
        return body, 0
    ents = set(SWITCH.findall(body))
    labels = set(LABEL.findall(body))
    ents &= labels
    cand = consts(body)
    a0 = int(addr, 16)
    head = a0 in cand
    tg = sorted(int(e, 16) for e in ents if int(e, 16) in cand)
    # below 0x3000 sit the kernel / policy entry points the dispatcher treats
    # specially (and leaving the taskset task region resets resident_task)
    tg = [t for t in tg if 0x3000 <= t < 0x3FF00]
    head = head and 0x3000 <= a0 < 0x3FF00
    if head:
        tg = [a0] + tg
    if not tg:
        return body, 0
    if head and ('loc_%s:' % addr) not in labels:
        # the function's own start: a label after the register loads and the
        # group-entry dispatch (both run only on a real entry)
        lines = body.split('\n')
        k = 0
        while k < len(lines) and LOAD.match(lines[k]):
            k += 1
        if k < len(lines) and lines[k].lstrip().startswith(GE):
            k += 1
        lines.insert(k, 'loc_%s:' % addr)
        body = '\n'.join(lines)
    off = ' - _b' if pic else ''
    n = 0

    def rep(m):
        nonlocal n
        n += 1
        swb, reg = m.group(1), m.group(2)
        tests = ' '.join(
            'if (_iq == 0x%Xu && spu_hop_direct_ok(ctx, 0x%Xu)) { ctx->pc = _it; ctx->steps++; goto loc_%08X; }'
            % (t, t, t) for t in tg)
        return ('{ %sconst uint32_t _it = %s._u32[0]; const uint32_t _iq = (_it%s) & 0x3FFFFu; '
                'if (g_spu_ib_inline > 0) { %s } '
                'ctx->pc = _it; g_spu_trampoline_fn = spu_indirect_branch; return; }'
                % (swb, reg, off, tests))
    body = SITE.sub(rep, body)
    return body, n


def main(inp, outp):
    src = open(inp, encoding='utf-8', errors='surrogateescape').read()
    if MARK in src:
        print(inp, 'already transformed')
        if inp != outp:
            open(outp, 'w', encoding='utf-8', errors='surrogateescape').write(src)
        return
    pic = 'const uint32_t _b = ctx->ls_bias;' in src
    out = []; pos = 0; nsite = nfn = 0
    for m in FHEAD.finditer(src):
        end = src.find('\n}\n', m.end())
        if end < 0:
            break
        out.append(src[pos:m.end()])
        body, n = transform(src[m.end():end + 1], m.group(2), pic)
        out.append(body)
        pos = end + 1
        if n:
            nsite += n; nfn += 1
    out.append(src[pos:])
    txt = ''.join(out)
    if nsite:
        first_inc = txt.find('#include')
        eol = txt.find('\n', first_inc) + 1
        txt = txt[:eol] + MARK + '\nextern int g_spu_ib_inline;\n' + txt[eol:]
    open(outp, 'w', encoding='utf-8', errors='surrogateescape').write(txt)
    print(inp, {'functions': nfn, 'sites': nsite})


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
