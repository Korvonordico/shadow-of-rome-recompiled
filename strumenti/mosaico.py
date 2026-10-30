#!/usr/bin/env python3
"""Mette insieme le foto di una prova in un'unica immagine (solo libreria standard).
Uso: mosaico.py prefisso uscita.png [colonne] [da] [a]
Rimpicciolisce a meta' prendendo un pixel su due."""
import glob
import struct
import sys
import zlib


def leggi_png(nome):
    d = open(nome, 'rb').read()
    p = 8
    idat = b''
    w = h = ct = 0
    while p < len(d):
        n, t = struct.unpack('>I4s', d[p:p + 8])
        c = d[p + 8:p + 8 + n]
        if t == b'IHDR':
            w, h, bd, ct = struct.unpack('>IIBB', c[:10])
        elif t == b'IDAT':
            idat += c
        p += 12 + n
    bpp = {6: 4, 2: 3}[ct]
    raw = zlib.decompress(idat)
    st = w * bpp
    righe = []
    prev = bytearray(st)
    q = 0
    for y in range(h):
        f = raw[q]
        r = bytearray(raw[q + 1:q + 1 + st])
        q += 1 + st
        if f == 1:
            for i in range(bpp, st):
                r[i] = (r[i] + r[i - bpp]) & 255
        elif f == 2:
            for i in range(st):
                r[i] = (r[i] + prev[i]) & 255
        elif f == 3:
            for i in range(st):
                a = r[i - bpp] if i >= bpp else 0
                r[i] = (r[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(st):
                a = r[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                r[i] = (r[i] + pr) & 255
        righe.append(r)
        prev = r
    return w, h, bpp, righe


def scrivi_png(nome, w, h, righe):
    def blocco(t, c):
        return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c))
    raw = b''.join(b'\0' + bytes(r) for r in righe)
    open(nome, 'wb').write(b'\x89PNG\r\n\x1a\n' + blocco(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           blocco(b'IDAT', zlib.compress(raw, 6)) + blocco(b'IEND', b''))


def main():
    fs = sorted(glob.glob(sys.argv[1] + '_*.png'), key=lambda f: (len(f), f))  # 2 prima di 10 e 100
    col = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    da = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    a = int(sys.argv[5]) if len(sys.argv) > 5 else len(fs)
    fs = fs[da:a]
    W, H = 256, 256
    n_righe = (len(fs) + col - 1) // col
    out = [bytearray(W * col * 3) for _ in range(H * n_righe)]
    for i, f in enumerate(fs):
        w, h, bpp, rr = leggi_png(f)
        ox, oy = (i % col) * W, (i // col) * H
        for y in range(min(H, h // 2)):
            sr = rr[y * 2]
            dr = out[oy + y]
            for x in range(min(W, w // 2)):
                s = x * 2 * bpp
                d = (ox + x) * 3
                dr[d:d + 3] = sr[s:s + 3]
    scrivi_png(sys.argv[2], W * col, H * n_righe, out)
    print(len(fs), 'foto')


main()
