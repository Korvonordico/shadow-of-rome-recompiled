#!/usr/bin/env python3
"""Legge una ISO9660 (DVD PS2) ed elenca o estrae file.
Uso:
  python3 iso_estrai.py ISO elenca
  python3 iso_estrai.py ISO estrai NOME_FILE DESTINAZIONE
"""
import sys, struct, os

SETT = 2048

def leggi(f, lba, n):
    f.seek(lba * SETT)
    return f.read(n)

def voci(f, lba, size, prefisso=""):
    dati = leggi(f, lba, size)
    i = 0
    while i < len(dati):
        l = dati[i]
        if l == 0:
            i = (i // SETT + 1) * SETT
            continue
        r = dati[i:i + l]
        elba = struct.unpack_from("<I", r, 2)[0]
        esize = struct.unpack_from("<I", r, 10)[0]
        flags = r[25]
        nl = r[32]
        nome = r[33:33 + nl]
        i += l
        if nome in (b"\x00", b"\x01"):
            continue
        nome = nome.decode("ascii", "replace").split(";")[0]
        percorso = prefisso + "/" + nome
        if flags & 2:
            yield from voci(f, elba, esize, percorso)
        else:
            yield percorso, elba, esize

def main():
    iso = sys.argv[1]
    cmd = sys.argv[2]
    with open(iso, "rb") as f:
        pvd = leggi(f, 16, SETT)
        assert pvd[1:6] == b"CD001", "non ISO9660"
        root = pvd[156:156 + 34]
        rlba = struct.unpack_from("<I", root, 2)[0]
        rsize = struct.unpack_from("<I", root, 10)[0]
        elenco = list(voci(f, rlba, rsize))
        if cmd == "elenca":
            for p, lba, s in elenco:
                print(f"{p}\t{lba}\t{s}")
        elif cmd == "estrai":
            nome, dest = sys.argv[3], sys.argv[4]
            for p, lba, s in elenco:
                if p.lstrip("/").upper() == nome.lstrip("/").upper():
                    f.seek(lba * SETT)
                    with open(dest, "wb") as o:
                        rest = s
                        while rest > 0:
                            b = f.read(min(rest, 1 << 22))
                            o.write(b)
                            rest -= len(b)
                    print(f"estratto {p} ({s} byte) in {dest}")
                    return
            sys.exit("non trovato: " + nome)

if __name__ == "__main__":
    main()
