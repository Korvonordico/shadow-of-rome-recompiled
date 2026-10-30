#!/usr/bin/env python3
"""Dal registro di una prova con SOR_TRACCIA: elenca funzioni chiamate e ultime chiamate con i nomi.
Uso: traccia_nomi.py _runN.log [filtro-regex] [ultime]"""
import re,sys
fn={}
for r in open('mappa/funzioni.csv').readlines()[1:]:
    n,s,e,_=r.strip().split(',')
    fn[int(s,16)]=n
L=open(sys.argv[1],errors='replace').read().splitlines()
filtro=re.compile(sys.argv[2]) if len(sys.argv)>2 and sys.argv[2] else None
ultime=int(sys.argv[3]) if len(sys.argv)>3 else 0
c=[]
t=[]
for l in L:
    m=re.search(r'\]  C (\d+) x (0x[0-9a-f]+)',l)
    if m: c.append((int(m.group(1)),fn.get(int(m.group(2),16),m.group(2))))
    m=re.search(r'\]  T (0x[0-9a-f]+) da (0x[0-9a-f]+) a0=(\S+) a1=(\S+)',l)
    if m: t.append((fn.get(int(m.group(1),16),m.group(1)),m.group(2),m.group(3),m.group(4)))
print(len(c),'funzioni diverse')
for n,x in c:
    if not filtro or filtro.search(x): print(n,x)
if ultime:
    prev=None
    for x in t[-ultime:]:
        if x[0]!=prev: print('T',*x)
        prev=x[0]
