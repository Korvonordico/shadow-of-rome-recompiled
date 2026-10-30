#!/usr/bin/env python3
"""Mostra l'assembly di una funzione tradotta (dai commenti del C++ generato).
Uso: asm.py nome_o_indirizzo [da_hex a_hex]"""
import sys,glob,re,os
q=sys.argv[1]
fs=[f for f in os.listdir('gen/main') if f.endswith('.cpp') and (q in f)]
if not fs: sys.exit('non trovato')
f=sorted(fs,key=len)[0]
print('#',f)
da=int(sys.argv[2],16) if len(sys.argv)>2 else 0
a=int(sys.argv[3],16) if len(sys.argv)>3 else 1<<40
for l in open('gen/main/'+f):
    m=re.match(r'\s*// 0x([0-9a-f]+): 0x[0-9a-f]+\s+(.*)',l)
    if m:
        ad=int(m.group(1),16)
        if da<=ad<a: print(m.group(1),re.sub(r'\s+',' ',m.group(2)))
