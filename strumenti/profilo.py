#!/usr/bin/env python3
"""Riassume il profilo scritto da SOR_PROFILO usando la mappa del linker.
Uso: profilo.py _prof.txt _build/sor.map [righe]"""
import bisect
import re
import sys
from collections import Counter

sym = []
for l in open(sys.argv[2], errors='replace'):
    p = l.split()
    if len(p) >= 4:
        try:
            a = int(p[0], 16)
        except ValueError:
            continue
        nome = " ".join(p[3:])
        if nome.startswith('.') or ':(' in nome or nome.endswith('.obj') or '.obj:' in nome or '.a(' in nome:
            continue
        sym.append((a, nome))
sym.sort()
ks = [s[0] for s in sym]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 25
base = 0
per_thread = {}
cur = None
for l in open(sys.argv[1]):
    p = l.split()
    if not p or p[0] == 'module':
        continue
    if p[0] == 'base':
        base = int(p[1], 16)
    elif p[0] == 'thread':
        cur = per_thread.setdefault(p[1], Counter())
    else:
        cur[int(p[0], 16)] += int(p[1])
print('simboli', len(sym), 'base', hex(base))
for t, c in sorted(per_thread.items(), key=lambda kv: -sum(kv[1].values())):
    tot = sum(c.values())
    agg = Counter()
    for a, v in c.items():
        for b in (0, base):
            i = bisect.bisect_right(ks, a - b) - 1
            if i >= 0 and a - b - ks[i] < 0x100000:
                agg[sym[i][1]] += v
                break
        else:
            agg['?' + hex(a >> 16)] += v
    print(f'--- thread {t}: {tot} campioni')
    if tot < 200:
        continue
    for nome, v in agg.most_common(n):
        print(f'{100*v/tot:5.1f}% {nome[:90]}')
