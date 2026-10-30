#!/usr/bin/env python3
"""Funzioni nella zona dopo il codice con debug (0x40C24C-0x4B1B80), che il
debug non elenca: i costruttori statici della tabella usata da
__initialize_cpp_rts e il codice senza nome trovato da ps2_analyzer.
La fine di ogni funzione e' dopo l'ultimo "jr ra" prima dell'inizio seguente.
Uso: python3 funzioni_extra.py ELF analisi/analyzer.log mappa/funzioni_extra.tsv
"""
import sys, struct, re

B, FINE_COD, FINE_SEZ = 0x100000, 0x40C24C, 0x4B1B80
elf, log, out = sys.argv[1:4]
d = open(elf, "rb").read()
w = lambda a: struct.unpack_from("<I", d, 0x100 + a - B)[0]

# tabella dei costruttori statici: parole che puntano nella zona, finite da 0
inizi = set()
CTOR = (0x4B0930, 0x4B093C)
for a in range(CTOR[0], CTOR[1], 4):
    inizi.add(w(a))
# funzioni dell'analizzatore nella zona
for m in re.finditer(r"Function sub_([0-9A-F]{8}) signature", open(log).read()):
    a = int(m.group(1), 16)
    if FINE_COD <= a < FINE_SEZ:
        inizi.add(a)
jr = [a for a in range(FINE_COD, FINE_SEZ, 4) if w(a) == 0x03E00008]
ordinati = sorted(inizi)
righe = []
for i, s in enumerate(ordinati):
    lim = ordinati[i + 1] if i + 1 < len(ordinati) else FINE_SEZ
    ultimi = [a for a in jr if s <= a < lim]
    if not ultimi:
        print(f"{s:#x}: nessun jr ra prima di {lim:#x}, scartata (probabilmente dati)")
        continue
    e = ultimi[-1] + 8
    righe.append((s, e))
with open(out, "w") as f:
    for s, e in righe:
        f.write(f"{s:08x}\t{e:08x}\t{e - s}\n")
print(f"funzioni extra: {len(righe)}: " + ", ".join(f"{s:#x}-{e:#x}" for s, e in righe))
