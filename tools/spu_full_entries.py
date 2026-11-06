#!/usr/bin/env python3
"""Whole-image SPU entry discovery for the static lift (r2_regen.py --full).

find_spu_functions.detect_functions seeds functions from the entry, symbols,
direct branch targets and recognised indirect idioms. Anything reached only
through a pointer computed at run time (PIC module bases, function tables built
by the PPU, continuation addresses spilled to LS, task resume addresses) falls
outside the lift and runs in the interpreter until the runtime worklist
(logs/spu_lift_todo.txt) is merged and the image re-lifted.

This pass closes that loop ahead of time by covering the whole code range:

  A. call returns     every brsl/brasl/bisl/bisled return address
  B. block leaders    the first instruction after every unconditional
                      terminator (br/bra/bi*/iret/stop), skipping lnop/nop
                      padding -- where every computed jump/case/continuation
                      that is not a call return lands
  C. gap fill         uncovered stretches between detected functions, swept
                      linearly run by run (leader .. terminator); a run is
                      accepted as code only when every word decodes, every
                      direct branch / pc-relative operand stays inside the
                      image, and it is not padding -- so rodata/const tails
                      (which decode as `.word` or out-of-image branches) stay
                      un-disassembled, like --code-end does for known tails
  D. immediates       il/ila/ilhu+iohl values (absolute, and module-relative
                      for images placed at a run-time base) that land on a
                      leader/return/gap entry -- counted for the report; they
                      are a subset of A-C by construction

A and B (and D) are passed to spu_lifter.py as --observed-entries: they split
existing functions without starting new --merge-chunks groups, so straight-line
speed is kept. C is passed as --gap-funcs start:end bounds.

    python tools/spu_full_entries.py <wrapped SPU ELF> [--base X] [--lo X] [--code-end X] [--rel]
"""
import argparse, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
TK = os.path.join(HERE, '..', 'ps3recomp', 'tools')
if TK not in sys.path:
    sys.path.insert(0, TK)
from spu_disasm import disassemble_spu  # noqa: E402

PAD = {0x00200000, 0x40200000, 0x00000000}         # lnop, nop, stop 0 / zero fill
TERM = {'br', 'bra', 'bi', 'iret', 'stop', 'stopd'}
CALL = {'brsl', 'brasl', 'bisl', 'bisled'}
DIRECT = {'br', 'brsl', 'brz', 'brnz', 'brhz', 'brhnz', 'bra', 'brasl'}
PCREL = {'lqr', 'stqr'}
HINT = {'hbr', 'hbra', 'hbrr'}


def _ops(ins):
    return [t.strip() for t in ins.operands.split(',') if t.strip()]


def _imm(tok):
    try:
        return int(tok, 16) if tok.lower().startswith(('0x', '-0x')) else int(tok, 10)
    except ValueError:
        return None


def _target(ins):
    for t in _ops(ins):
        if t.lower().startswith('0x'):
            return _imm(t)
    return None


def discover(code, base, bounds, lo=None, hi=None, rel=False, min_run=3):
    """code: image bytes at LS `base`; bounds: [(start, end)] already lifted
    (detect_functions + the caller's extra seeds). lo/hi: code range (default
    the whole image; jobmod/plugin headers start code at +0x30). rel: the image
    is placed at a run-time base, so immediates are tried module-relative too.
    Returns dict(observed=set, gaps=[(s, e)], stats=dict)."""
    end = base + len(code)
    lo = base if lo is None else lo
    hi = end if hi is None else min(hi, end)
    insns = disassemble_spu(code, base)
    by = {i.addr: i for i in insns}
    valid = {i.addr for i in insns if i.mnemonic != '.word'}

    def is_term(i):
        if i.mnemonic not in TERM:
            return False
        if i.mnemonic == 'stop' and (i.raw & 0x3FFF) in (0x100, 0x110, 0x111):
            return False       # lv2 resuming stop: execution continues after it
        return True

    def skip_pad(a):
        while a < hi and a in by and by[a].raw in PAD:
            a += 4
        return a

    bset = sorted(set((s, e) for s, e in bounds if s < e))
    starts = {s for s, _ in bset}
    cov = bytearray(len(code) // 4 + 1)
    for s, e in bset:
        for a in range(max(s, base), min(e, end), 4):
            cov[(a - base) >> 2] = 1

    def covered(a):
        return base <= a < end and cov[(a - base) >> 2]

    # ---- C. gap fill: linear sweep of every uncovered stretch ----
    def anchored(i):
        """An instruction data rarely decodes to: control flow, the stack
        pointer as a base/target, or a channel access."""
        m = i.mnemonic
        if m in DIRECT or m in CALL or m in ('bi', 'iret', 'rdch', 'wrch', 'rchcnt'):
            return True
        o = _ops(i)
        if m in ('lqd', 'stqd') and o and o[-1].endswith('($r1)'):
            return True
        return m == 'ai' and o[:2] == ['$r1', '$r1']

    def run_ok(s, e, falls_into_code):
        n = 0
        words = []
        anchor = False
        last = None
        for a in range(s, e, 4):
            i = by.get(a)
            if i is None or i.mnemonic == '.word':
                return False
            if i.raw in PAD:
                continue
            n += 1
            words.append(i.raw)
            last = i
            anchor = anchor or anchored(i)
            m = i.mnemonic
            if m in DIRECT or m in PCREL or m in HINT:
                t = _target(i)
                if m in HINT:
                    # hbr* carry two addresses; reject only a wild branch address
                    ts = [_imm(x) for x in _ops(i) if x.lower().startswith('0x')]
                    if any(x is not None and not (base <= x < end) for x in ts):
                        return False
                    continue
                # lqr/stqr may address the module's bss just past the image
                if t is None or not (base <= t < (end + 0x10000 if m in PCREL else end)):
                    return False
                if m not in PCREL and (t & 3 or t not in valid):
                    return False
        if n < min_run or not anchor:
            return False
        # shuffle-mask / constant tables decode as runs of repeated words
        if len(set(words)) < 0.75 * len(words):
            return False
        if last.mnemonic in ('stop', 'stopd'):
            # data words 0x0000xxxx decode as `stop xxxx`; only the lv2
            # exit stops really end code
            return (last.raw & 0x3FFF) in (0x101, 0x102)
        if is_term(last):
            return True
        return falls_into_code

    gaps = []
    a = lo
    rejected = 0
    while a < hi:
        if covered(a):
            a += 4
            continue
        g0 = a
        while a < hi and not covered(a):
            a += 4
        g1 = a
        p = g0
        while p < g1:
            while p < g1 and by.get(p) is not None and by[p].raw == 0:
                p += 4                                   # zero fill (not lnop/nop: those are entries too)
            if p >= g1:
                break
            s = p
            while p < g1:
                i = by.get(p)
                p += 4
                if i is None or i.mnemonic == '.word' or is_term(i):
                    break
            if run_ok(s, p, p >= g1 and g1 < hi and covered(g1)):
                if gaps and gaps[-1][1] == s:
                    gaps[-1] = (gaps[-1][0], p)          # fall-through continuation
                else:
                    gaps.append((s, p))
            else:
                rejected += 1
    gap_starts = {s for s, _ in gaps}
    for s, e in gaps:
        for x in range(s, e, 4):
            cov[(x - base) >> 2] = 1
    code_ok = lambda x: lo <= x < hi and x in valid and covered(x)

    # ---- A. call returns, B. block leaders ----
    rets, leaders = set(), set()
    for i in insns:
        if not (lo <= i.addr < hi) or not covered(i.addr):
            continue
        if i.mnemonic in CALL:
            r = i.addr + 4
            if code_ok(r):
                rets.add(r)
        elif is_term(i):
            # the pad slot right after a terminator is itself a common
            # (8-byte aligned) entry, as is the first real instruction past it
            for r in {i.addr + 4, skip_pad(i.addr + 4)}:
                if code_ok(r):
                    leaders.add(r)
    entry_like = rets | leaders | starts | gap_starts

    # ---- D. immediates landing on an entry-like address ----
    imms = set()
    hi16 = {}
    for i in insns:
        if not (lo <= i.addr < hi) or not covered(i.addr):
            continue
        m = i.mnemonic
        o = _ops(i)
        v = None
        if m in ('il', 'ila') and len(o) == 2:
            v = _imm(o[1])
        elif m == 'ilhu' and len(o) == 2:
            x = _imm(o[1])
            if x is not None:
                hi16[o[0]] = (x & 0xFFFF) << 16
            continue
        elif m == 'iohl' and len(o) == 2 and o[0] in hi16:
            x = _imm(o[1])
            v = None if x is None else hi16.pop(o[0]) | (x & 0xFFFF)
        if v is None or v <= 0 or v & 3:
            continue
        for t in ((v, v + base) if rel and base else (v,)):
            if t in entry_like and code_ok(t):
                imms.add(t)

    observed = (rets | leaders | imms) - starts - gap_starts
    stats = dict(bounds=len(bset), returns=len(rets), leaders=len(leaders), imms=len(imms),
                 new_splits=len(observed), gaps=len(gaps), gap_bytes=sum(e - s for s, e in gaps),
                 rejected_runs=rejected,
                 covered_before=sum(min(e, hi) - max(s, lo) for s, e in bset if s < hi and e > lo),
                 code_bytes=hi - lo)
    return dict(observed=observed, gaps=gaps, stats=stats)


def discover_elf(elf_bytes, base=None, lo=None, hi=None, rel=False, extra=()):
    """Convenience: run detect_functions on a (wrapped) SPU ELF, add `extra`
    seeds as split points, then discover()."""
    from find_spu_functions import detect_functions, parse_elf, text_image
    import io, contextlib
    with contextlib.redirect_stdout(io.StringIO()):
        funcs, (_, b, _) = detect_functions(elf_bytes)
    code, _ = text_image(elf_bytes, parse_elf(elf_bytes)['phs'])
    b = b if base is None else base
    bounds = list(funcs)
    if extra:
        cuts = sorted(set(extra))
        nb = []
        for s, e in bounds:
            prev = s
            for c in cuts:
                if s < c < e:
                    nb.append((prev, c)); prev = c
            nb.append((prev, e))
        bounds = nb
    return discover(code, b, bounds, lo=lo, hi=hi, rel=rel)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('elf')
    ap.add_argument('--base', type=lambda x: int(x, 0), default=None)
    ap.add_argument('--lo', type=lambda x: int(x, 0), default=None)
    ap.add_argument('--code-end', type=lambda x: int(x, 0), default=None)
    ap.add_argument('--rel', action='store_true')
    a = ap.parse_args()
    r = discover_elf(open(a.elf, 'rb').read(), a.base, a.lo, a.code_end, a.rel)
    print(r['stats'])
    print('observed:', ','.join(hex(x) for x in sorted(r['observed'])))
    print('gaps:', ','.join('%X:%X' % g for g in r['gaps']))


if __name__ == '__main__':
    main()
