#!/usr/bin/env python3
"""Trova le zone dell'eseguibile non coperte dalle funzioni del debug e
cerca dentro le funzioni senza nome (librerie senza debug).
Uso: python3 buchi.py ELF mappa/funzioni.tsv mappa/
Scrive mappa/buchi.tsv e mappa/funzioni_trovate.tsv
"""
import sys, struct

BASE = 0x100000


def main():
    elf, ftsv, outd = sys.argv[1], sys.argv[2], sys.argv[3]
    d = open(elf, "rb").read()
    testo = d[0x100:0x100 + 0x3b1b80]
    fn = {}
    for r in open(ftsv):
        c = r.rstrip("\n").split("\t")
        lo, hi = int(c[0], 16), int(c[1], 16)
        fn.setdefault(lo, (hi, c[3]))
    inizi = sorted(fn)
    fine_cod = max(fn[a][0] for a in inizi)
    print(f"prima funzione {inizi[0]:#x}, fine dell'ultima {fine_cod:#x}")

    def parola(a):
        return struct.unpack_from("<I", testo, a - BASE)[0]

    buchi = []
    cur = BASE
    for a in inizi:
        if a > cur:
            buchi.append((cur, a))
        cur = max(cur, fn[a][0])
    buchi.append((cur, BASE + len(testo)))
    trovate = []
    with open(outd + "/buchi.tsv", "w") as f:
        tot_cod = 0
        for lo, hi in buchi:
            n = hi - lo
            if n <= 0:
                continue
            jr = sum(1 for a in range(lo, hi - 3, 4) if parola(a) == 0x03e00008)
            zeri = sum(1 for a in range(lo, hi - 3, 4) if parola(a) == 0)
            # funzioni dentro il buco: dopo ogni "jr ra" + slot comincia una funzione
            starts = []
            if jr and lo < fine_cod:
                a = lo
                while a < hi and parola(a) == 0:
                    a += 4
                inizio = a
                while a < hi:
                    if parola(a) == 0x03e00008:
                        fine = a + 8
                        starts.append((inizio, fine))
                        a = fine
                        while a < hi and parola(a) == 0:
                            a += 4
                        inizio = a
                    else:
                        a += 4
                tot_cod += n
            f.write(f"{lo:08x}\t{hi:08x}\t{n}\tjr_ra={jr}\tzeri={zeri}\tstimate={len(starts)}\n")
            trovate += starts
    with open(outd + "/funzioni_trovate.tsv", "w") as f:
        for lo, hi in trovate:
            f.write(f"{lo:08x}\t{hi:08x}\t{hi - lo}\n")
    print(f"buchi: {len([b for b in buchi if b[1] > b[0]])}, nel codice: {sum(1 for b in buchi if b[0] < fine_cod)}")
    print(f"byte nei buchi prima della fine del codice: {sum(min(h, fine_cod) - l for l, h in buchi if l < fine_cod)}")
    print(f"funzioni senza nome stimate nei buchi (taglio a ogni jr ra): {len(trovate)}")


if __name__ == "__main__":
    main()
