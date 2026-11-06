# Extract WWS job modules (C0DEC0DE header) embedded in Resistance 2's EBOOT.elf.
# They live in the SPU data section (file 0xC19C00..0xCECB80) together with three
# embedded SPU ELFs and the job manager; a module ends at the next module, the
# next embedded ELF, or the job manager, whichever comes first.
import struct, re, hashlib, sys, os

d = open(sys.argv[1], 'rb').read(); out = sys.argv[2]; os.makedirs(out, exist_ok=True)
SEC_END = 0xCECB80                      # end of the SPU data section
PM_START = 0xCE9680                     # WWS job manager (policy module)
elfs = [m.start() for m in re.finditer(b'\x7fELF\x01\x02\x01', d) if struct.unpack_from('>H', d, m.start() + 18)[0] == 0x17]
# Two of the embedded SPU ELFs are job modules wrapped in an ELF (C0DEC0DE at
# ELF+0xA0): the job manager streams their segment into LS 0x4000 like any
# other module, so they are extracted too.
starts = [m.start() - 0x20 for m in re.finditer(b'\xC0\xDE\xC0\xDE', d)]
bounds = sorted(set(starts + elfs + [PM_START, SEC_END]))
for s in starts:
    ent, size = struct.unpack_from('>II', d, s + 0x10)
    nxt = next(b for b in bounds if b > s)
    raw = d[s:s + min(size, nxt - s)]
    open(f"{out}/jobmod_{hashlib.sha1(raw[:16]).hexdigest()[:12]}_e{ent:X}.bin", 'wb').write(raw)
print(f'{len(starts)} job modules, {len(elfs)} embedded SPU ELFs')
