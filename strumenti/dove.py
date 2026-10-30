#!/usr/bin/env python3
"""Filtro: aggiunge accanto a ogni indirizzo 0x1xxxxx-0x4bxxxx il nome della
funzione che lo contiene (da mappa/funzioni.csv).
Uso: ... | python3 dove.py mappa/funzioni.csv
"""
import sys, re, bisect

fn = []
for r in open(sys.argv[1]).readlines()[1:]:
    n, s, e, _ = r.strip().split(",")
    fn.append((int(s, 16), int(e, 16), n))
fn.sort()
starts = [f[0] for f in fn]


def nome(m):
    a = int(m.group(0), 16)
    if not (0x100000 <= a < 0x4b2000):
        return m.group(0)
    i = bisect.bisect_right(starts, a) - 1
    if i >= 0 and fn[i][0] <= a < fn[i][1]:
        return f"{m.group(0)}<{fn[i][2]}+{a - fn[i][0]:#x}>"
    return m.group(0)


for riga in sys.stdin:
    sys.stdout.write(re.sub(r"0x[0-9a-fA-F]{5,8}\b", nome, riga))
