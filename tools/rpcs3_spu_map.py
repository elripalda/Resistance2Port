#!/usr/bin/env python3
"""Map RPCS3's SPU cache for Resistance 2 onto the EBOOT's SPU programs.

    python tools/rpcs3_spu_map.py --eboot game/EBOOT.elf --cache <spu-*.dat> -o tools/r2_spu_entries.json

RPCS3 records every SPU entry point it actually executed (LS address + the code
words from there). Each record is located in the player's EBOOT; the program
that contains it and the LS address it was loaded at follow from the offset.
The output lists, per program, every observed load address and the executed
entry points there, which the lift uses as extra function entries.

Cache record layout (spu-*-v1-tane.dat): be16 tag, be16 word count, be32 LS
address, then the words.
"""
import argparse, json, re, struct

def is_hdr(d, o):
    w = struct.unpack_from('>4I', d, o)
    return all((x >> 24) in (0x42, 0x43) for x in w) and [x & 0xFF for x in w] == [2, 0x82, 2, 0x82]

def programs(d):
    """Every WWS job module (C0DEC0DE and the older DEADDEAD layout), the job
    manager and the embedded SPU ELF segments, as (kind, file start, file end)."""
    hs = [o for o in range(0xB00000, len(d) - 0x40, 16) if is_hdr(d, o)]
    elfs = [m.start() for m in re.finditer(b'\x7fELF\x01\x02\x01', d)
            if struct.unpack_from('>H', d, m.start() + 18)[0] == 0x17]
    out = []
    for i, o in enumerate(hs):
        f = struct.unpack_from('>8I', d, o + 0x10)
        if f[4] == 0xC0DEC0DE: kind = 'c0de'
        elif f[6] == 0xDEADDEAD: kind = 'dead'
        else: continue
        nxt = min([x for x in hs[i + 1:i + 2] + elfs if x > o] + [len(d)])
        out.append((kind, o, nxt))
    # fixed-placement programs: (kind, file start, file end, LS address of start)
    fixed = [('pm', 0xCE9680, 0xCE9680 + 0x3100, 0xA00)]
    for i, e in enumerate(elfs):
        phoff, phn = struct.unpack_from('>I', d, e + 28)[0], struct.unpack_from('>H', d, e + 44)[0]
        for k in range(phn):
            t, off, va, _, fs = struct.unpack_from('>5I', d, e + phoff + k * 32)
            if t == 1 and fs: fixed.append(('elf%d' % i, e + off, e + off + fs, va))
    return out, fixed

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--eboot', required=True); ap.add_argument('--cache', required=True)
    ap.add_argument('-o', required=True)
    a = ap.parse_args()
    d = open(a.eboot, 'rb').read(); c = open(a.cache, 'rb').read()
    recs, o = [], 0
    while o + 8 <= len(c):
        _, n, ls = struct.unpack_from('>HHI', c, o)
        recs.append((ls, c[o + 8:o + 8 + 4 * n])); o += 8 + 4 * n
    progs, fixed = programs(d)
    res, miss = {}, 0
    def add(kind, s, e, hdr_ls, ls):
        p = res.setdefault('%X' % (s + 0x10000), dict(kind=kind, file=s, size=e - s, placements={}))
        p['placements'].setdefault('%X' % hdr_ls, set()).add(ls)
    def cands(h, ls):
        out = []
        for kind, s, e, fls in fixed:
            if s <= h < e and ls - (h - s) == fls and not any(ps <= h < pe for _, ps, pe in progs):
                out.append((kind, s, e, fls))
        for kind, s, e in progs:
            if s <= h < e and 0 <= ls - (h - s) and ls - (h - s) + (e - s) <= 0x40000:
                out.append((kind, s, e, ls - (h - s)))
        return out
    ambiguous = []
    for ls, w in recs:
        key = w[:min(len(w), 64)]
        cs = [c for m in re.finditer(re.escape(key), d) for c in cands(m.start(), ls)]
        if len(cs) == 1: add(*cs[0], ls)
        elif cs: ambiguous.append((ls, cs))
        else: miss += 1
    # code shared by several modules: credit every copy whose placement is
    # already confirmed by a unique record (each lift needs its own entry)
    for ls, cs in ambiguous:
        ok = [c for c in cs if '%X' % (c[1] + 0x10000) in res and '%X' % c[3] in res['%X' % (c[1] + 0x10000)]['placements']]
        if ok:
            for c in ok: add(*c, ls)
        else: miss += 1
    for p in res.values():
        p['placements'] = {k: sorted(v) for k, v in p['placements'].items()}
    json.dump(res, open(a.o, 'w'), indent=1, sort_keys=True)
    n_pl = sum(len(p['placements']) for p in res.values())
    print('%d records, %d unattributed; %d programs, %d placements' % (len(recs), miss, len(res), n_pl))
    for k in sorted(set(p['kind'] for p in res.values())):
        ps = [p for p in res.values() if p['kind'] == k]
        print('  %s: %d programs, %d placements' % (k, len(ps), sum(len(p['placements']) for p in ps)))

if __name__ == '__main__':
    main()
