#!/usr/bin/env python3
"""Resistance 2 port -- regenerate all game-derived code from the player's own
decrypted EBOOT.elf (Resistance 2 is one program: EBOOT.BIN == game.self).

    python tools/r2_regen.py --eboot <EBOOT.elf> [--port <port dir>] [--only ppu|spu]

Writes, inside the port folder:
    recompiled/          PPU code
    project/spu/<mod>/   SPU job modules, job manager, raw SPU programs
Nothing here is taken from the game except through the lifters run on the
player's own file; tools/hooks/r2.json holds the port's own edits.
"""
import argparse, glob, json, os, re, shutil, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from regen_common import run, ppu, fix_includes, spu_post

# Resistance 2 (BCES00226 v1.00) layout of the SPU data section
PM = dict(off=0xCE9680, size=0x3100, base=0xA00, entry=0xA00)
TEXT_END = 0xBBD2D8
# Raw SPU programs (Bink video). The PPU proxy-loads a 0x140-byte loader at LS
# 0x10; it reads a descriptor EA from its mailbox, DMAs the program it names to
# LS 0x200 and branches to 0x230. The loader is matched by fingerprint like any
# raw SPU program; the two programs it streams in are code overlays keyed by
# their source EA, so dispatch picks the one actually resident.
RAW = [
    dict(n='rawldr',  va=0xED2330, size=0x140,   base=0x10,  entry=0x20,  img=310, funcs='0x20,0x104'),
    dict(n='binkdec', va=0xEB6580, size=0x18010, base=0x200, entry=0x230, img=311, ovl=True),
    dict(n='spuinit', va=0xEB6300, size=0x210,   base=0x200, entry=0x230, img=312, ovl=True),
    # Two more programs the same loader streams in during gameplay (raw SPU 0
    # and 1 in the first level); unlifted, they ran wholly interpreted and
    # starved the frame (3-5 fps). Spans run to the next program in the image.
    dict(n='rawprogA', va=0xE9B400, size=0x7180,  base=0x200, entry=0x230, img=313, ovl=True),
    dict(n='rawprogB', va=0xEA2580, size=0x13D80, base=0x200, entry=0x230, img=314, ovl=True),
]

# Entry points seen executing in RPCS3 (tools/rpcs3_spu_map.py over its SPU
# cache): addresses only, no code. They are the indirect-branch targets static
# discovery misses, and they locate the position-dependent WWS plugin modules.
ENTRIES = os.path.join(HERE, 'r2_spu_entries.json')
PLACED_IMG = 400

def load_entries():
    return json.load(open(ENTRIES)) if os.path.exists(ENTRIES) else {}

def placed_modules(ents):
    """(name, va, file, size, LS base, entries, image id) for each observed
    placement of a DEADDEAD-layout module. Placements below 0x2000 sit in the
    job manager's own code and are cross-matches of shared code, not loads."""
    out = []
    for va, p in sorted(ents.items()):
        if p['kind'] != 'dead': continue
        for pl, e in sorted(p['placements'].items(), key=lambda x: int(x[0], 16)):
            b = int(pl, 16)
            if b < 0x2000: continue
            out.append(('wwsplug_%s_%05X' % (va, b), int(va, 16), p['file'], p['size'], b, e, PLACED_IMG + len(out)))
    return out

PIC_IMG = 600

def pic_modules(d, ents):
    """(name, va, file, size, entry offsets, image id) for every DEADDEAD-layout
    WWS plugin in the EBOOT, each lifted ONCE position-independently
    (spu_lifter.py --pic): the guest copies them to heap-dependent LS addresses,
    so per-address lifts never caught up. Entry offsets: every observed
    placement's entries rebased to the module start, plus runtime `entry pic`
    offsets (ents[va]['pic'])."""
    sys.path.insert(0, HERE)
    from rpcs3_spu_map import programs
    progs, _ = programs(d)
    out = []
    for k, s, e in progs:
        if k != 'dead': continue
        va = s + 0x10000
        p = ents.get('%X' % va, {})
        offs = {0x30}
        for pl, es in p.get('placements', {}).items():
            b = int(pl, 16)
            offs |= {a - b for a in es if 0 <= a - b < e - s}
        offs |= {o for o in p.get('pic', []) if 0 <= o < e - s}
        out.append(('wwspic_%X' % va, va, s, e - s, sorted(offs), PIC_IMG + len(out)))
    return out

def fixed_entries(ents, kind):
    return sorted({a for p in ents.values() if p['kind'] == kind for e in p['placements'].values() for a in e})

FULL = False   # --full: whole-image entry discovery (tools/spu_full_entries.py)

def lift_args(pe, base=None, lo=None, extra=(), observed=(), rel=False, code_end=None):
    """--observed-entries/--gap-funcs for one lift. Without --full: just the
    observed entries. With --full: plus every call return, block leader and
    gap-swept code run of the whole image (lists go through @files, they can
    be long)."""
    observed = sorted(set(observed))
    gaps = []
    if FULL:
        from spu_full_entries import discover_elf
        r = discover_elf(open(pe, 'rb').read(), base=base, lo=lo, hi=code_end, rel=rel,
                         extra=list(extra) + list(observed))
        observed = sorted(set(observed) | r['observed'])
        gaps = r['gaps']
        st = r['stats']
        print('      full: +%d entries, +%d gap funcs (0x%X bytes), %d leaders, %d returns'
              % (st['new_splits'], st['gaps'], st['gap_bytes'], st['leaders'], st['returns']), flush=True)
    out = []
    if observed:
        f = pe + '.observed.txt'; open(f, 'w').write(','.join(hex(a) for a in observed))
        out += ['--observed-entries', '@' + f]
    if gaps:
        f = pe + '.gaps.txt'; open(f, 'w').write(','.join('%X:%X' % g for g in gaps))
        out += ['--gap-funcs', '@' + f]
    return out

def va2off(d, va):
    phoff, phn = struct.unpack_from('>Q', d, 0x20)[0], struct.unpack_from('>H', d, 0x38)[0]
    for i in range(phn):
        t, _, off, v, _, fs = struct.unpack_from('>IIQQQQ', d, phoff + i * 56)
        if t == 1 and v <= va < v + fs: return off + va - v
    raise SystemExit('VA 0x%X not in a PT_LOAD segment' % va)

def fnv1a64(b):
    h = 1469598103934665603
    for x in b: h = ((h ^ x) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h

def elf_len(d, o):
    """SPU ELF file length as the runtime measures it (end of the section headers)."""
    shoff = struct.unpack_from('>I', d, o + 0x20)[0]
    shentsz, shnum = struct.unpack_from('>HH', d, o + 0x2E)
    return shoff + shentsz * shnum

def write_workloads(path, d, elfs, placed=(), pics=()):
    """Registration TU: each lifted image keyed by the FNV-1a-64 fingerprint the
    runtime computes over the image the guest hands it (spu_workload.c / spu_raw.c)."""
    L = ['/* spu_workloads.c -- GENERATED by tools/r2_regen.py from the player\'s EBOOT.elf.',
         ' * Registers Resistance 2\'s lifted SPU images by content fingerprint. */',
         '#include "spu_workload.h"', '', 'extern void spu_begin_image(int image_id);',
         'extern void spu_overlay_register_region(unsigned content_ea, unsigned span, int image_id);', '',
         'extern void pm_wwsjob_spu_func_%08X(spu_context*);' % PM['entry'],
         'extern void pm_wwsjob_spu_recomp_register(void);']
    ents = []
    for i, o in enumerate(elfs):
        e = struct.unpack_from('>I', d, o + 0x18)[0]
        ents.append((i, e, fnv1a64(d[o:o + elf_len(d, o)])))
        L += ['extern void spuelf%d_spu_func_%08X(spu_context*);' % (i, e), 'extern void spuelf%d_spu_recomp_register(void);' % i]
    pm_fp = fnv1a64(d[PM['off']:PM['off'] + PM['size']])
    L += ['', 'void r2_spu_register_all(void)', '{',
          '    spu_begin_image(1); pm_wwsjob_spu_recomp_register();',
          '    spu_workload_register_img(0x%016XULL, pm_wwsjob_spu_func_%08X, 1, "pm_wwsjob.text");' % (pm_fp, PM['entry'])]
    for i, e, fp in ents:
        L += ['    spu_begin_image(%d); spuelf%d_spu_recomp_register();' % (300 + i, i),
              '    spu_workload_register_img(0x%016XULL, spuelf%d_spu_func_%08X, %d, "spuelf%d");' % (fp, i, e, 300 + i, i)]
    for r in RAW:
        o = va2off(d, r['va'])
        L.insert(L.index('void r2_spu_register_all(void)') - 1,
                 'extern void %s_spu_recomp_register(void);\nextern void %s_spu_func_%08X(spu_context*);' % (r['n'], r['n'], r['entry']))
        L += ['    spu_begin_image(%d); %s_spu_recomp_register();' % (r['img'], r['n'])]
        if r.get('ovl'):
            L += ['    spu_overlay_register_region(0x%08Xu, 0x%Xu, %d);' % (r['va'], r['size'], r['img'])]
        else:
            L += ['    spu_workload_register_img(0x%016XULL, %s_spu_func_%08X, %d, "%s");'
                  % (fnv1a64(d[o:o + r['size']]), r['n'], r['entry'], r['img'], r['n'])]
    if placed:
        L.insert(L.index('void r2_spu_register_all(void)') - 1,
                 'extern void spu_overlay_register_placed(const unsigned char sig[16], unsigned lsa, unsigned span, unsigned src_ea, int image_id);')
    for n, va, fo, size, b, _, img in placed:
        L.insert(L.index('void r2_spu_register_all(void)') - 1, 'extern void %s_spu_recomp_register(void);' % n)
        sig = ', '.join('0x%02X' % x for x in d[fo:fo + 16])
        L += ['    spu_begin_image(%d); %s_spu_recomp_register();' % (img, n),
              '    { static const unsigned char s[16] = { %s };' % sig,
              '      spu_overlay_register_placed(s, 0x%05Xu, 0x%Xu, 0x%08Xu, %d); }' % (b, size, va, img)]
    if pics:
        L.insert(L.index('void r2_spu_register_all(void)') - 1,
                 'extern void spu_overlay_register_pic(unsigned src_ea, unsigned span, int image_id);')
    for n, va, fo, size, _, img in pics:
        L.insert(L.index('void r2_spu_register_all(void)') - 1, 'extern void %s_spu_recomp_register(void);' % n)
        L += ['    spu_begin_image(%d); %s_spu_recomp_register(); spu_overlay_register_pic(0x%08Xu, 0x%Xu, %d);'
              % (img, n, va, size, img)]
    L += ['    spu_begin_image(0);', '}', '',
          '__attribute__((constructor)) static void r2_spu_register_all_ctor(void)', '{', '    r2_spu_register_all();', '}', '']
    open(path, 'w').write('\n'.join(L))

def spu(port, tk, eboot, work, stage_raw=None):
    sys.path.insert(0, tk)
    from wrap_spu_elf import wrap
    spudir = os.path.join(port, 'project', 'spu')
    hk = os.path.join(HERE, 'hooks', 'spu')
    d = open(eboot, 'rb').read()
    print('[spu] extracting + lifting job modules...', flush=True)
    jm = os.path.join(work, 'jobmods'); out = os.path.join(work, 'spu')
    for x in (jm, out):
        if os.path.isdir(x): shutil.rmtree(x)
    run([sys.executable, os.path.join(HERE, 'extract_jobmods_r2.py'), eboot, jm])
    env = dict(os.environ, PYTHONPATH=tk)
    ents = load_entries()
    import hashlib
    obs = {}
    for p in ents.values():
        if p['kind'] == 'c0de':
            obs[hashlib.sha1(d[p['file']:p['file'] + 16]).hexdigest()[:12]] = p['placements']
    obs_json = os.path.join(work, 'jobmod_entries.json'); json.dump(obs, open(obs_json, 'w'))
    run([sys.executable, os.path.join(HERE, 'lift_jobmods_base.py'), '--jobmods', jm, '--out', out,
         '--register', os.path.join(work, 'jobmods_register.c'), '--entries', obs_json]
        + (['--full'] if FULL else []), env=env)
    mods = sorted(os.path.basename(x) for x in glob.glob(os.path.join(out, 'jobmod_*')))
    os.makedirs(spudir, exist_ok=True)
    shutil.copy(os.path.join(work, 'jobmods_register.c'), os.path.join(spudir, 'jobmods_register.c'))
    # job manager (policy module)
    print('[spu] lifting pm_wwsjob...', flush=True)
    n = 'pm_wwsjob'; os.makedirs(os.path.join(work, n), exist_ok=True)
    pe = os.path.join(work, n, 'p.elf')
    open(pe, 'wb').write(wrap(d[PM['off']:PM['off'] + PM['size']], base=PM['base'], entry=PM['entry']))
    # 0xA2C: the PM's SPU interrupt handler (it writes "br 0xA2C" to LS 0 at
    # start-up). Nothing branches to it statically, so --auto-functions misses
    # it, and without it a stalled DMA list (job loading) is never serviced.
    run([sys.executable, os.path.join(tk, 'spu_lifter.py'), '--auto-functions', pe, '--base', hex(PM['base']),
         '--symbol-prefix', n + '_', '-o', os.path.join(out, n), '--source-name', 'spu_recomp.c',
         '--header-name', 'spu_recomp.h', '--return-entries', '--extra-funcs', '0xa2c']
        + lift_args(pe, PM['base'], PM['base'], [0xA2C], fixed_entries(ents, 'pm')), env=env)
    mods.append(n)
    # embedded SPU ELF programs (raw SPU / SPURS task images)
    elfs = [m.start() for m in re.finditer(b'\x7fELF\x01\x02\x01', d) if struct.unpack_from('>H', d, m.start() + 18)[0] == 0x17]
    for i, o in enumerate(elfs):
        phoff, shoff = struct.unpack_from('>I', d, o + 28)[0], struct.unpack_from('>I', d, o + 32)[0]
        phnum, shnum = struct.unpack_from('>H', d, o + 44)[0], struct.unpack_from('>H', d, o + 48)[0]
        end = max([shoff + shnum * 40] + [struct.unpack_from('>I', d, o + phoff + k * 32 + 4)[0] +
                                         struct.unpack_from('>I', d, o + phoff + k * 32 + 16)[0] for k in range(phnum)])
        n = 'spuelf%d' % i
        print('[spu] lifting %s (file 0x%X, 0x%X bytes)...' % (n, o, end), flush=True)
        os.makedirs(os.path.join(work, n), exist_ok=True)
        pe = os.path.join(work, n, 'p.elf'); open(pe, 'wb').write(d[o:o + end])
        run([sys.executable, os.path.join(tk, 'spu_lifter.py'), '--auto-functions', pe,
             '--symbol-prefix', n + '_', '-o', os.path.join(out, n), '--source-name', 'spu_recomp.c',
             '--header-name', 'spu_recomp.h']
            + lift_args(pe, observed=fixed_entries(ents, 'elf%d' % i)), env=env)
        mods.append(n)
    for r in RAW:
        n = r['n']; o = va2off(d, r['va'])
        print('[spu] lifting raw SPU program %s (VA 0x%X, 0x%X bytes at LS 0x%X)...' % (n, r['va'], r['size'], r['base']), flush=True)
        os.makedirs(os.path.join(work, n), exist_ok=True)
        pe = os.path.join(work, n, 'p.elf')
        open(pe, 'wb').write(wrap(d[o:o + r['size']], base=r['base'], entry=r['entry']))
        cmd = [sys.executable, os.path.join(tk, 'spu_lifter.py'), '--auto-functions', pe, '--base', hex(r['base']),
               '--symbol-prefix', n + '_', '-o', os.path.join(out, n), '--source-name', 'spu_recomp.c',
               '--header-name', 'spu_recomp.h', '--return-entries']
        if r.get('funcs'): cmd += ['--extra-funcs', r['funcs']]
        cmd += lift_args(pe, r['base'], r['base'], [int(x, 0) for x in r.get('funcs', '').split(',') if x])
        run(cmd, env=env)
        mods.append(n)
    # Fixed per-address plugin lifts are superseded by the PIC lifts below;
    # R2_FIXED_PLACED=1 brings them back.
    placed = placed_modules(ents) if os.environ.get('R2_FIXED_PLACED') == '1' else []
    pics = [] if os.environ.get('R2_NO_PIC') == '1' else pic_modules(d, ents)
    for n, va, fo, size, offs, img in pics:
        print('[spu] lifting PIC module %s (0x%X bytes, %d observed entries)...' % (n, size, len(offs)), flush=True)
        os.makedirs(os.path.join(work, n), exist_ok=True)
        pe = os.path.join(work, n, 'p.elf')
        open(pe, 'wb').write(wrap(d[fo:fo + size], base=0, entry=0x30))
        run([sys.executable, os.path.join(tk, 'spu_lifter.py'), '--auto-functions', pe, '--base', '0', '--pic',
             '--symbol-prefix', n + '_', '-o', os.path.join(out, n), '--source-name', 'spu_recomp.c',
             '--header-name', 'spu_recomp.h', '--return-entries', '--merge-chunks',
             '--extra-funcs', '0x30'] + lift_args(pe, 0, 0x30, [0x30], offs, rel=True), env=env)
        mods.append(n)
    for n, va, fo, size, b, e, img in placed:
        print('[spu] lifting placed module %s (0x%X bytes at LS 0x%X, %d observed entries)...' % (n, size, b, len(e)), flush=True)
        os.makedirs(os.path.join(work, n), exist_ok=True)
        pe = os.path.join(work, n, 'p.elf')
        open(pe, 'wb').write(wrap(d[fo:fo + size], base=b, entry=b + 0x30))
        ex = sorted({b + 0x30} | set(e))
        run([sys.executable, os.path.join(tk, 'spu_lifter.py'), '--auto-functions', pe, '--base', hex(b),
             '--symbol-prefix', n + '_', '-o', os.path.join(out, n), '--source-name', 'spu_recomp.c',
             '--header-name', 'spu_recomp.h', '--return-entries', '--merge-chunks',
             '--extra-funcs', hex(b + 0x30), '--observed-entries', ','.join(hex(a) for a in ex)], env=env)
        mods.append(n)
    write_workloads(os.path.join(spudir, 'spu_workloads.c'), d, elfs, placed, pics)
    if not placed:
        for x in glob.glob(os.path.join(spudir, 'wwsplug_*')): shutil.rmtree(x)
    print('[spu] port edits + register cache...', flush=True)
    for n in mods:
        x = os.path.join(out, n); fix_includes(x)
        if stage_raw is not None: shutil.copytree(x, os.path.join(stage_raw, n))
        hp = os.path.join(hk, 'r2_' + n + '.json')
        f = os.path.join(x, 'spu_recomp.c')
        if os.path.exists(hp): spu_post(f, hp)
        else:
            run([sys.executable, os.path.join(HERE, 'spu_regcache.py'), f, f]); run([sys.executable, os.path.join(HERE, 'spu_directhop.py'), f])
            if n.startswith(('jobmod_', 'wwsplug_', 'wwspic_')):
                run([sys.executable, os.path.join(HERE, 'spu_writeback.py'), f, f])
                run([sys.executable, os.path.join(HERE, 'spu_group_entry.py'), f, f])
                run([sys.executable, os.path.join(HERE, 'spu_loop_ib.py'), f, f])   # v193: same-function bi loops
        dst = os.path.join(spudir, n)
        if os.path.isdir(dst): shutil.rmtree(dst)
        shutil.copytree(x, dst)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--eboot', required=True)
    ap.add_argument('--port', default=os.path.dirname(HERE)); ap.add_argument('--work', default=None)
    ap.add_argument('--only', choices=['ppu', 'spu'], default=None)
    ap.add_argument('--full', action='store_true',
                    help='SPU: whole-image coverage -- every call return, block leader and '
                         'gap-swept code run becomes a lifted entry (tools/spu_full_entries.py), '
                         'so the interpreter is left as a fallback only')
    ap.add_argument('--stage-raw', default=None, help=argparse.SUPPRESS)
    a = ap.parse_args()
    global FULL
    FULL = a.full
    port = os.path.abspath(a.port); tk = os.path.join(port, 'ps3recomp', 'tools')
    work = os.path.abspath(a.work or os.path.join(port, 'build_regen'))
    os.makedirs(work, exist_ok=True)
    if a.only in (None, 'ppu'):
        ppu(port, tk, a.eboot, 'recompiled', 'r2', 24, True, os.path.join(HERE, 'hooks', 'r2.json'), work)
    if a.only in (None, 'spu'):
        spu(port, tk, a.eboot, work, a.stage_raw)
    print('done: game code regenerated.')

if __name__ == '__main__':
    main()
