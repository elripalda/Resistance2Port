#!/usr/bin/env python3
"""Shared steps of the code generator (tools/r2_regen.py): run the PPU lifter
and its post passes on an ELF, apply the port's hooks, and post-process lifted
SPU modules.
"""
import glob, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))

def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        sys.stderr.write(r.stdout[-2000:] + r.stderr[-4000:])
        raise SystemExit('FAILED: ' + ' '.join(cmd[:4]))
    return r.stdout

def ppu(port, tk, elf, outdir, name, chunks, post_passes, hooks, work):
    print('[%s] lifting PPU code...' % name, flush=True)
    od = os.path.join(work, name + '_an')
    run([sys.executable, os.path.join(tk, 'ppu_loader.py'), elf, '-o', od])
    stem = os.path.splitext(os.path.basename(elf))[0]
    tmp = os.path.join(work, name)
    if os.path.isdir(tmp): shutil.rmtree(tmp)
    run([sys.executable, os.path.join(tk, 'ppu_lifter.py'), elf, '--functions', os.path.join(od, stem + '.functions.json'),
         '--hle-stubs', os.path.join(od, stem + '.imports.json'), '--chunks', str(chunks), '--output', tmp])
    chunksf = sorted(glob.glob(os.path.join(tmp, 'ppu_recomp_0*.cpp')))
    if post_passes:
        print('[%s] register cache + fast memory passes...' % name, flush=True)
        for f in chunksf:
            run([sys.executable, os.path.join(HERE, 'regcache.py'), f, f])
            run([sys.executable, os.path.join(HERE, 'ppu_fastmem.py'), f, f])
        # the function table is its own file in this lifter version: give it the
        # same vm_base declaration guard ppu_fastmem.py gives the chunks
        tf = os.path.join(tmp, 'ppu_recomp_table.cpp')
        if os.path.exists(tf):
            s = open(tf, encoding='utf-8', errors='surrogateescape').read()
            s = s.replace('\nextern "C" uint8_t* vm_base;\n', '\n#ifndef PPU_FIXED_BOUNDS   /* v163: ppu_recomp.h declares it const */\nextern "C" uint8_t* vm_base;\n#endif\n', 1)
            open(tf, 'w', encoding='utf-8', errors='surrogateescape').write(s)
    run([sys.executable, os.path.join(HERE, 'ppu_hooks.py'), 'apply', tmp, hooks])
    shutil.copy(os.path.join(HERE, 'hooks', 'ppu_hle_nids.cpp'), os.path.join(tmp, 'ppu_hle_nids.cpp'))
    dst = os.path.join(port, outdir)
    if os.path.isdir(dst): shutil.rmtree(dst)
    shutil.copytree(tmp, dst)

def spu_post(f, hooks):
    run([sys.executable, os.path.join(HERE, 'spu_hooks.py'), 'apply', 'pre', f, hooks])
    run([sys.executable, os.path.join(HERE, 'spu_regcache.py'), f, f])
    run([sys.executable, os.path.join(HERE, 'spu_directhop.py'), f])
    run([sys.executable, os.path.join(HERE, 'spu_hooks.py'), 'apply', 'post', f, hooks])

def fix_includes(d):
    for f in glob.glob(os.path.join(d, 'spu_recomp.*')):
        s = open(f, encoding='utf-8', errors='surrogateescape').read().replace('"../../runtime/spu/', '"')
        open(f, 'w', encoding='utf-8', errors='surrogateescape').write(s)
