#!/usr/bin/env python3
"""Disassembla il microcodice VU1 da una copia fatta con SOR_VU1DUMP e mostra registri e dati.
Uso: vudis.py file [da_hex a_hex]   (senza indirizzi: registri e 64 istruzioni prima del PC)
     vudis.py file dati da_qw n_qw  (memoria dati come float ed esadecimale)"""
import struct, sys
d = open(sys.argv[1], 'rb').read()
DS = 0x4000
vf = struct.unpack('<128f', d[DS:DS + 512])
vi = struct.unpack('<16i', d[DS + 512:DS + 576])
pc, kick = struct.unpack('<II', d[DS + 576:DS + 584])
code = d[DS + 584:]
BC = 'xyzw'
def dest(w):
    return ''.join(c for c, b in zip('xyzw', (24, 23, 22, 21)) if w >> b & 1)
def upper(w):
    fl = ''.join(n for n, b in zip('IEMDT', (31, 30, 29, 28, 27)) if w >> b & 1)
    ft, fs, fd, op = w >> 16 & 31, w >> 11 & 31, w >> 6 & 31, w & 63
    D = dest(w)
    bcop = {0: 'ADD', 4: 'SUB', 8: 'MADD', 12: 'MSUB', 16: 'MAX', 20: 'MINI', 24: 'MUL'}
    r = None
    if op < 0x1c:
        n = bcop[op & ~3]
        r = '%s%s.%s vf%d,vf%d,vf%d%s' % (n, BC[op & 3], D, fd, fs, ft, BC[op & 3])
    elif op < 0x30:
        names = {0x1c: 'MULq', 0x1d: 'MAXi', 0x1e: 'MULi', 0x1f: 'MINIi', 0x20: 'ADDq', 0x21: 'MADDq', 0x22: 'ADDi', 0x23: 'MADDi', 0x24: 'SUBq', 0x25: 'MSUBq', 0x26: 'SUBi', 0x27: 'MSUBi', 0x28: 'ADD', 0x29: 'MADD', 0x2a: 'MUL', 0x2b: 'MAX', 0x2c: 'SUB', 0x2d: 'MSUB', 0x2e: 'OPMSUB', 0x2f: 'MINI'}
        r = '%s.%s vf%d,vf%d,vf%d' % (names[op], D, fd, fs, ft)
    elif op >= 0x3c:
        s = fd << 2 | op & 3
        ac = {0: 'ADDA', 4: 'SUBA', 8: 'MADDA', 12: 'MSUBA', 0x18: 'MULA'}
        if s & ~3 in ac:
            r = '%s%s.%s ACC,vf%d,vf%d%s' % (ac[s & ~3], BC[s & 3], D, fs, ft, BC[s & 3])
        else:
            nm = {0x10: 'ITOF0', 0x11: 'ITOF4', 0x12: 'ITOF12', 0x13: 'ITOF15', 0x14: 'FTOI0', 0x15: 'FTOI4', 0x16: 'FTOI12', 0x17: 'FTOI15', 0x1c: 'MULAq', 0x1d: 'ABS', 0x1e: 'MULAi', 0x1f: 'CLIP', 0x20: 'ADDAq', 0x21: 'MADDAq', 0x22: 'ADDAi', 0x23: 'MADDAi', 0x24: 'SUBAq', 0x25: 'MSUBAq', 0x26: 'SUBAi', 0x27: 'MSUBAi', 0x28: 'ADDA', 0x29: 'MADDA', 0x2a: 'MULA', 0x2c: 'SUBA', 0x2d: 'MSUBA', 0x2e: 'OPMULA', 0x2f: 'NOP'}
            n = nm.get(s, 'U?%x' % s)
            r = 'NOP' if n == 'NOP' else '%s.%s vf%d,vf%d' % (n, D, ft, fs)
    else:
        r = 'U?%x' % op
    return ('[%s] ' % fl if fl else '') + r
def sim(v, n):
    return v - (1 << n) if v >> (n - 1) & 1 else v
def lower(w, a):
    op = w >> 25
    ft, fs, fd = w >> 16 & 31, w >> 11 & 31, w >> 6 & 31
    D = dest(w)
    i11 = sim(w & 0x7ff, 11)
    t = (a + 8 + i11 * 8) & 0x3fff
    if op == 0x40:
        o = w & 63
        if o < 0x3c:
            n = {0x30: 'IADD', 0x31: 'ISUB', 0x32: 'IADDI', 0x34: 'IAND', 0x35: 'IOR'}.get(o)
            if o == 0x32:
                return 'IADDI vi%d,vi%d,%d' % (ft, fs, sim(fd, 5))
            return '%s vi%d,vi%d,vi%d' % (n or 'L?%x' % o, fd, fs, ft)
        s = fd << 2 | o & 3
        m = {0x30: 'MOVE.%s vf%d,vf%d' % (D, ft, fs), 0x31: 'MR32.%s vf%d,vf%d' % (D, ft, fs), 0x34: 'LQI.%s vf%d,(vi%d++)' % (D, ft, fs), 0x35: 'SQI.%s vf%d,(vi%d++)' % (D, fs, ft), 0x36: 'LQD.%s vf%d,(--vi%d)' % (D, ft, fs), 0x37: 'SQD.%s vf%d,(--vi%d)' % (D, fs, ft), 0x38: 'DIV Q,vf%d%s,vf%d%s' % (fs, BC[w >> 21 & 3], ft, BC[w >> 23 & 3]), 0x39: 'SQRT Q,vf%d%s' % (ft, BC[w >> 23 & 3]), 0x3a: 'RSQRT Q,vf%d%s,vf%d%s' % (fs, BC[w >> 21 & 3], ft, BC[w >> 23 & 3]), 0x3b: 'WAITQ', 0x3c: 'MTIR vi%d,vf%d%s' % (ft, fs, BC[w >> 21 & 3]), 0x3d: 'MFIR.%s vf%d,vi%d' % (D, ft, fs), 0x3e: 'ILWR.%s vi%d,(vi%d)' % (D, ft, fs), 0x3f: 'ISWR.%s vi%d,(vi%d)' % (D, ft, fs), 0x64: 'MFP.%s vf%d,P' % (D, ft), 0x68: 'XTOP vi%d' % ft, 0x69: 'XITOP vi%d' % ft, 0x6c: 'XGKICK vi%d' % fs, 0x7b: 'WAITP'}
        if s in m:
            return m[s]
        if s == 0x0b or s == 0x2f or s == 0x33:
            return 'NOP'
        return 'L?%x' % s
    sim11 = i11
    if op == 0: return 'LQ.%s vf%d,%d(vi%d)' % (D, ft, sim11, fs)
    if op == 1: return 'SQ.%s vf%d,%d(vi%d)' % (D, fs, sim11, ft)
    if op == 4: return 'ILW.%s vi%d,%d(vi%d)' % (D, ft, sim11, fs)
    if op == 5: return 'ISW.%s vi%d,%d(vi%d)' % (D, ft, sim11, fs)
    u15 = (w >> 10 & 0x7800) | w & 0x7ff
    if op == 8: return 'IADDIU vi%d,vi%d,%d' % (ft, fs, u15)
    if op == 9: return 'ISUBIU vi%d,vi%d,%d' % (ft, fs, u15)
    if 0x10 <= op <= 0x1c:
        n = {0x10: 'FCEQ', 0x11: 'FCSET', 0x12: 'FCAND', 0x13: 'FCOR', 0x14: 'FSEQ', 0x15: 'FSSET', 0x16: 'FSAND', 0x17: 'FSOR', 0x18: 'FMEQ', 0x1a: 'FMAND', 0x1b: 'FMOR', 0x1c: 'FCGET'}.get(op, 'F?')
        return '%s vi%d,vi%d,%x' % (n, ft, fs, w & 0xffffff)
    if op == 0x20: return 'B %04x' % t
    if op == 0x21: return 'BAL vi%d,%04x' % (ft, t)
    if op == 0x24: return 'JR vi%d' % fs
    if op == 0x25: return 'JALR vi%d,vi%d' % (ft, fs)
    if 0x28 <= op <= 0x2f:
        n = {0x28: 'IBEQ', 0x29: 'IBNE', 0x2c: 'IBLTZ', 0x2d: 'IBGTZ', 0x2e: 'IBLEZ', 0x2f: 'IBGEZ'}.get(op, 'B?')
        return '%s vi%d,vi%d,%04x' % (n, ft, fs, t)
    return 'L?op%x' % op
if len(sys.argv) > 2 and sys.argv[2] == 'dati':
    a, n = int(sys.argv[3], 16), int(sys.argv[4], 16)
    for q in range(a, a + n):
        o = q * 16
        f = struct.unpack('<4f', d[o:o + 16]); h = struct.unpack('<4I', d[o:o + 16])
        print('%03x: %08x %08x %08x %08x | %12.5g %12.5g %12.5g %12.5g' % ((q,) + h + f))
    sys.exit()
if len(sys.argv) > 3:
    da, a = int(sys.argv[2], 16), int(sys.argv[3], 16)
else:
    print('pc=%04x kick=%04x (qw %03x)' % (pc, kick, kick // 16))
    print('vi', ' '.join('%d:%x' % (i, v & 0xffff) for i, v in enumerate(vi)))
    for i in range(32):
        print('vf%-2d %12.5g %12.5g %12.5g %12.5g' % ((i,) + vf[i * 4:i * 4 + 4]))
    da, a = max(0, pc - 64 * 8), pc + 16
for x in range(da & ~7, a, 8):
    lo, up = struct.unpack('<II', code[x:x + 8])
    print('%04x %-34s %s' % (x, upper(up), ('LOI 0x%08x' % lo) if up >> 31 else lower(lo, x)))
