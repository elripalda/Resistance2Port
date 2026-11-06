#!/usr/bin/env python3
"""Resistance port (v189): write-back register cache for the lifted SPU code.

Runs after spu_regcache.py (which keeps registers in locals S_n but writes
every result through to ctx->gpr[n] as well). Here the write-through goes
away inside a function: a register write updates only its local, and every
local the function writes is stored back to the context (_SWB) right before
anything that can look at the context -- a call that takes ctx (lifted
functions, channel ops, SPU_HOP / SPU_RET, stop/halt), a `return`, and the
end of the function. Local-store accesses (spu_ls_read128/write128 ...) only
read ctx->ls and need nothing. Statements that take ctx keep their own
write-through, so the reload after them (_SRC_RL) reads current values.

With --merge-chunks the hot job loops are whole C functions, so their inner
loops now run entirely in registers.

Per function it is all or nothing: a function whose ctx statements are not
one-per-line (hand edits spanning lines, unbalanced braces) is left as is.

  spu_writeback.py in.c out.c
"""
import re, sys

FHEAD = re.compile(r'^void (\w+)\(spu_context\* ctx\) \{\n', re.M)
PURE = ('spu_ls_read128', 'spu_ls_write128', 'spu_ls_read32', 'spu_ls_write32',
        'spu_trace_pc', 'spu_trace_rt')
MARK = '/* spu writeback v192 */'
REGC = '/* spu regcache v142 */'
CTXRE = re.compile(r'\bctx\b(?!->)')
WT = re.compile(r'ctx->gpr\[(\d+)\] = (S_\1) = ')

def call_name(s, i):
    depth = 0; j = i - 1
    while j >= 0:
        c = s[j]
        if c == ')': depth += 1
        elif c == '(':
            if depth == 0:
                k = j - 1
                while k >= 0 and s[k] == ' ': k -= 1
                e = k + 1
                while k >= 0 and (s[k].isalnum() or s[k] == '_'): k -= 1
                return s[k + 1:e]
            depth -= 1
        j -= 1
    return ''

def needs_ctx(line):
    for m in CTXRE.finditer(line):
        if call_name(line, m.start()) not in PURE:
            return True
    return False

LABEL = re.compile(r'^loc_([0-9A-F]{8}):$')
GOTO = re.compile(r'\bgoto loc_([0-9A-F]{8});')
SREF = re.compile(r'\bS_(\d+)\b(\s*=(?!=))?')
WTHR = re.compile(r'ctx->gpr\[(\d+)\] = S_\1 =(?!=)')
KILL = re.compile(r'^S_(\d+) = [^;]*;$')
DECL = re.compile(r'^u128 S_(\d+) = ctx->gpr\[\1\];$')


def match_close(s, i, o, c):
    """index of the bracket closing s[i] (== o), or -1"""
    d = 0
    for k in range(i, len(s)):
        if s[k] == o: d += 1
        elif s[k] == c:
            d -= 1
            if d == 0: return k
    return -1


def split_if(st):
    """`if (COND) { BODY }` -> (COND, BODY), else None"""
    if not st.startswith('if ('):
        return None
    p = match_close(st, 3, '(', ')')
    if p < 0:
        return None
    rest = st[p + 1:].strip()
    if not rest.startswith('{') or match_close(rest, 0, '{', '}') != len(rest) - 1:
        return None
    return st[4:p], rest[1:-1].strip()


def depth0(s):
    """s with every (...) and {...} group dropped"""
    out = []; d = 0
    for ch in s:
        if ch in '({': d += 1
        elif ch in ')}': d -= 1
        elif d == 0: out.append(ch)
    return ''.join(out)


def ends_flow(st):
    """the statement(s) never fall through to the next line"""
    while st.startswith('{') and match_close(st, 0, '{', '}') == len(st) - 1:
        st = st[1:-1].strip()
    d0 = depth0(st)
    if re.search(r'\b(if|else|switch|while|for|case)\b', d0):
        return False
    stmts = [x.strip() for x in d0.split(';') if x.strip()]
    if not stmts:
        return False
    last = stmts[-1]
    return (last == 'return' or last.startswith('goto loc_')
            or last in ('SPU_RET', 'SPU_RET_REG', 'SPU_HOP', 'SPU_TAILCALL'))


def bits(ns):
    m = 0
    for n in ns: m |= 1 << n
    return m


def unbits(m):
    out = []; n = 0
    while m:
        if m & 1: out.append(n)
        m >>= 1; n += 1
    return out


def flow(lines, kinds, entry_labels, W):
    """Per-site store sets and the live-in set at entry (bit masks).

    Dirty (forward, may): registers whose local was written since the last
    point where locals and context agree (entry, a store-back, a reload).
    Only those are stored back at a site. Live (backward, may): a register's
    local is needed if it is read -- or stored back -- before being written;
    only registers live at the entry (or at an inner label a merged-group
    entry may jump to) are loaded there. Edges are over-approximated (every
    line falls through unless it plainly cannot), which only grows the sets.
    Returns None when the body has a shape this does not model."""
    n = len(lines)
    END = n                     # the closing write-back
    labels = {}
    for i, ln in enumerate(lines):
        m = LABEL.match(ln.strip())
        if m: labels[m.group(1)] = i
    succ = [[] for _ in range(n + 1)]
    use = [0] * (n + 1); kill = [0] * (n + 1); gen = [0] * (n + 1)
    site = [False] * (n + 1); sync = [False] * (n + 1)
    allU = (1 << 128) - 1
    for i, ln in enumerate(lines):
        st = ln.strip(); k = kinds[i]
        nxt = i + 1 if i + 1 < n else END
        if k is None and (st.count('{') != st.count('}') or st.count('(') != st.count(')')):
            return None
        tg = []
        for g in GOTO.findall(st):
            if g not in labels: return None
            tg.append(labels[g])
        if k is None and (st.startswith('switch') or re.search(r'\bcase\b', st)):
            return None
        succ[i] = tg + ([] if ends_flow(st) else [nxt])
        u = 0; dset = 0
        wt = {int(x) for x in WTHR.findall(st)}
        for m in SREF.finditer(st):
            r = int(m.group(1))
            if m.group(2):
                if r not in wt: dset |= 1 << r
            else:
                u |= 1 << r
        # a write-through's S_n appears as `S_n =`: not a read, keeps ctx current
        use[i] = u; gen[i] = dset
        if k is None:
            km = KILL.match(st)
            if km and km.group(0).count(' = ') == 1:
                kill[i] = 1 << int(km.group(1))
        else:
            site[i] = True
            if k == 'u':
                sync[i] = True
                if '_SRC_RL' in st:
                    kill[i] = allU
    site[END] = True
    entry_succ = [0] + sorted(set(labels[l] for l in entry_labels if l in labels))
    if n == 0:
        entry_succ = [END]
    # forward: dirty-in per node
    din = [None] * (n + 1)
    work = []
    for s in entry_succ:
        din[s] = 0; work.append(s)
    while work:
        i = work.pop()
        d = din[i]
        if i == END: continue
        dout = gen[i] if sync[i] else (d | gen[i])
        for s in succ[i]:
            if din[s] is None or (din[s] | dout) != din[s]:
                din[s] = (din[s] or 0) | dout
                work.append(s)
    stores = [0] * (n + 1)
    for i in range(n + 1):
        if site[i] and din[i] is not None:
            stores[i] = din[i] & W
    # backward liveness
    pred = [[] for _ in range(n + 1)]
    for i in range(n):
        for s in succ[i]: pred[s].append(i)
    lin = [0] * (n + 1)
    lin[END] = stores[END]
    work = list(range(n))
    inq = [True] * n + [False]
    while work:
        i = work.pop(); inq[i] = False
        lo = 0
        for s in succ[i]: lo |= lin[s]
        v = use[i] | stores[i] | (lo & ~kill[i])
        if v != lin[i]:
            lin[i] = v
            for p in pred[i]:
                if not inq[p]: inq[p] = True; work.append(p)
    live = 0
    for s in entry_succ: live |= lin[s]
    return stores, live


def transform(body, entry_labels=()):
    if 'u128 S_' not in body:
        return None
    lines = body.split('\n')
    out = []; W = set(); wrap = []
    for ln in lines:
        st = ln.strip()
        if st.startswith('u128 S_'):
            out.append(ln); wrap.append(False); continue
        c = needs_ctx(ln) or re.search(r'\breturn\b', ln) is not None
        if c:
            if ln.count('{') != ln.count('}') or ln.count('(') != ln.count(')'):
                return None
            if not st.endswith(';') and not st.endswith('}'):
                return None
            out.append(ln); wrap.append(True)
        else:
            if 'ctx->gpr' in ln and not WT.search(ln):
                return None                     # some other register access
            n2 = WT.sub(lambda m: (W.add(int(m.group(1))), m.group(2) + ' = ')[1], ln)
            if 'ctx->gpr' in n2:
                return None
            out.append(n2); wrap.append(False)
    if not W:
        return None
    nd = 0
    while nd < len(out) and DECL.match(out[nd].strip()):
        nd += 1
    U = [int(DECL.match(x.strip()).group(1)) for x in out[:nd]]
    rest, rwrap = out[nd:], wrap[nd:]
    kinds = []; stmts = []
    for ln, w in zip(rest, rwrap):
        st = ln.strip()
        k = None
        if w:
            k = 'u'
            si = split_if(st)
            if si and not needs_ctx(si[0]) and not re.search(r'\bS_\d+\s*=(?!=)', si[0]):
                k = 'c'
        kinds.append(k); stmts.append(st)
    Wm = bits(W)
    fl = flow(stmts, kinds, entry_labels, Wm)
    if fl is None:
        stores = [Wm if (k or i == len(rest)) else 0 for i, k in enumerate(kinds + ['u'])]
        live = bits(U)
    else:
        stores, live = fl
    names = {}
    def swb(mask):
        if not mask: return ''
        if mask not in names:
            names[mask] = '_SWB' if not names else '_SWB%d' % len(names)
        return names[mask] + '; '
    res = []
    for i, (ln, k) in enumerate(zip(rest, kinds)):
        ind = ln[:len(ln) - len(ln.lstrip())]
        st = ln.strip()
        if k == 'c':
            cond, blk = split_if(st)
            res.append(ind + 'if (' + cond + ') { ' + swb(stores[i]) + blk + ' }')
        elif k == 'u':
            res.append(ind + '{ ' + swb(stores[i]) + st + ' }')
        else:
            res.append(ln)
    tail = swb(stores[len(rest)])
    decl = ['        u128 S_%d = ctx->gpr[%d];' % (r, r) for r in U if live >> r & 1]
    decl += ['        u128 S_%d;' % r for r in U if not live >> r & 1]
    macros = [(nm, ' '.join('ctx->gpr[%d] = S_%d;' % (r, r) for r in unbits(mask))) for mask, nm in names.items()]
    return '\n'.join(decl + res), macros, tail


def main(inp, outp):
    src = open(inp, encoding='utf-8', errors='surrogateescape').read()
    if MARK in src:
        print(inp, 'already transformed')
        if inp != outp: open(outp, 'w', encoding='utf-8', errors='surrogateescape').write(src)
        return
    if REGC not in src:
        sys.exit(inp + ': run spu_regcache.py first')
    # inner labels a merged group may be entered at (spu_group_entry.py)
    fnaddrs = set(re.findall(r'^void \w+_spu_func_([0-9A-F]{8})\(spu_context\* ctx\) \{$', src, re.M))
    res = []; pos = 0; ok = skip = 0; st_sites = st_regs = 0
    for m in FHEAD.finditer(src):
        if m.start() < pos: continue
        end = src.find('\n}\n', m.end())
        if end < 0: break
        body = src[m.end():end + 1]
        r = transform(body, fnaddrs)
        res.append(src[pos:m.start()])
        if r:
            nb, macros, tail = r
            for nm, s in macros:
                res.append('#define %s do { %s } while (0)\n' % (nm, s))
            res.append(m.group(0) + nb + ('        %s\n' % tail if tail else '') + '}\n')
            for nm, _ in macros:
                res.append('#undef %s\n' % nm)
            ok += 1
        else:
            res.append(src[m.start():end + 3])
            skip += 1
        pos = end + 3
    res.append(src[pos:])
    txt = ''.join(res)
    first_inc = txt.find('#include')
    eol = txt.find('\n', first_inc) + 1
    txt = txt[:eol] + MARK + '\n' + txt[eol:]
    open(outp, 'w', encoding='utf-8', errors='surrogateescape').write(txt)
    print(inp, {'write-back': ok, 'left as is': skip})

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
