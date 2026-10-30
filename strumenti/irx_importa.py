#!/usr/bin/env python3
"""Elenca le librerie IOP che ogni modulo .IRX importa (tabelle 0x41E00000).
Uso: python3 irx_importa.py FILE.IRX [...]
"""
import sys, struct

def importa(d):
    out = []
    for o in range(0, len(d) - 20, 4):
        if struct.unpack_from("<I", d, o)[0] == 0x41E00000 and struct.unpack_from("<I", d, o + 4)[0] == 0:
            ver = struct.unpack_from("<I", d, o + 8)[0]
            nome = d[o + 12:o + 20].split(b"\0")[0].decode("latin-1")
            # le voci: "jr ra" (0x03E00008) seguito da "addiu zero,zero,N" (0x2400NNNN)
            p = o + 20
            funz = []
            while p + 8 <= len(d) and struct.unpack_from("<I", d, p)[0] == 0x03E00008:
                funz.append(struct.unpack_from("<I", d, p + 4)[0] & 0xFFFF)
                p += 8
            out.append((nome, ver, funz))
    return out

for f in sys.argv[1:]:
    d = open(f, "rb").read()
    imp = importa(d)
    print(f.split("/")[-1] + ": " + ", ".join(f"{n}({len(fz)})" for n, v, fz in imp))
