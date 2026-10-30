#!/usr/bin/env python3
"""Copia dell'eseguibile per il runtime di PS2Recomp: disattiva (PT_NULL) i
segmenti LOAD senza dati che stanno fuori dai 32 MB della RAM PS2 (Metrowerks
ne mette uno vuoto a 0x05000000; il caricatore del runtime lo rifiuta).
Conta anche le istruzioni "lui reg, 0x0500" (uso di quell'indirizzo nel codice).
Uso: python3 elf_per_pc.py ELF ELF_PC
"""
import sys, struct

src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, "rb").read())
phoff, = struct.unpack_from("<I", d, 0x1c)
phentsize, phnum = struct.unpack_from("<HH", d, 0x2a)
for i in range(phnum):
    o = phoff + i * phentsize
    p_type, p_off, p_vaddr, p_paddr, p_filesz, p_memsz = struct.unpack_from("<6I", d, o)
    if p_type == 1 and p_filesz == 0 and p_vaddr + p_memsz > 0x2000000:
        struct.pack_into("<I", d, o, 0)
        print(f"segmento {i} a {p_vaddr:#x} (memsz {p_memsz:#x}, filesz 0) disattivato")
testo = d[0x100:0x100 + 0x30C24C]
lui = sum(1 for a in range(0, len(testo), 4)
          if (struct.unpack_from("<I", testo, a)[0] & 0xFFE0FFFF) == 0x3C000500)
print(f"istruzioni 'lui reg,0x0500' nel codice: {lui}")
open(dst, "wb").write(d)
