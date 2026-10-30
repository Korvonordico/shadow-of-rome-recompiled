#!/usr/bin/env python3
"""Riassunto di un registro di prova: righe particolari (spie, sentinelle, salti falliti) e ultime
chiamate della traccia, con i nomi delle funzioni.
Uso: riassunto.py NOME [quante_chiamate] [filtro_regex_in_piu]  (registro in Temp/sor/_NOME.log)"""
import bisect, os, re, sys
QUI = os.path.dirname(os.path.abspath(__file__))
fn = []
for r in open(os.path.join(QUI, '..', 'mappa', 'funzioni.csv')).readlines()[1:]:
    n, s, e, _ = r.strip().split(',')
    fn.append((int(s, 16), int(e, 16), n))
fn.sort()
starts = [f[0] for f in fn]
def nome(m):
    a = int(m.group(0), 16)
    if not 0x100000 <= a < 0x4b2000:
        return m.group(0)
    i = bisect.bisect_right(starts, a) - 1
    if i >= 0 and fn[i][0] <= a < fn[i][1]:
        return '%s<%s+%#x>' % (m.group(0), fn[i][2].split('__')[0], a - fn[i][0])
    return m.group(0)
def bello(l):
    return re.sub(r'0x[0-9a-fA-F]{5,8}\b', nome, l)
temp = os.environ.get('SOR_T') or os.path.expandvars(r'%LOCALAPPDATA%\Temp\sor')
log = os.path.join(temp, '_%s.log' % sys.argv[1])
quante = int(sys.argv[2]) if len(sys.argv) > 2 else 40
extra = sys.argv[3] if len(sys.argv) > 3 else None
pat = re.compile(r'spia|SENTINELLA|sentinella|guest-branch|Unhandled|Failed|eccezione|requestStop|exit\(|sor\] !' + ('|' + extra if extra else ''))
visti = set()
ultime = []
ultimo_stato = []
for l in open(log, errors='replace'):
    l = l.rstrip()[:420]
    if l.startswith('[sor]  T '):
        ultime.append(l[9:])
        continue
    if '=== stato a' in l:
        ultimo_stato = [l]
    elif re.match(r'\[sor\] thread \d+:', l):
        ultimo_stato.append(l)
    if pat.search(l) and l not in visti:
        visti.add(l) if 'spia' not in l else None
        print(bello(l))
print('--- ultimo stato')
for l in ultimo_stato:
    print(bello(l))
if ultime:
    print('--- ultime chiamate')
    for l in ultime[-quante:]:
        print(bello(l))
