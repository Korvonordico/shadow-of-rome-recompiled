#!/usr/bin/env python3
"""Map Shadow of Rome addresses from the European executable (SLES_529.50) to the American one (SLUS_209.02).

Both executables carry the same DWARF debug info (tables made by strumenti/dwarf1.py: mappa/ for the European,
mappa-usa/ for the American). 5004 of the 5045 functions are the same code, but the American code is 2,688 bytes
shorter, so every address moves. An address is mapped only when it can be proved, never guessed:
- a function start: by the function's name (C++ mangled name when there is one, else name + source file);
- an address inside a function: name + offset, only if the function is the same code in both (masked compare:
  jal/j targets and the immediates of lui and of the address-carrying I-type instructions are ignored, because
  they hold addresses that moved); inside one of the differing functions it is NOT mapped;
- an address in code without debug info (the Sony libraries, the functions after the code): offset from the
  nearest debug function before it, only if the whole stretch between two debug functions has the same length
  and the same masked code in both;
- a data address: the data symbol at or before it + offset, only if the distance from that symbol to the next
  symbol is the same in both (so the offset stays inside an object of the same size).
Anything else goes to the report as NOT MAPPED, with the reason.

Use:
  python3 rimappa_usa.py 0x4be2e8 0x146aa0 ...       print each mapping
  python3 rimappa_usa.py --header OUT.h FILE...       collect every SA(0x...) in the source files, write the
                                                      table for the American build (pc/sor_indirizzi_usa.h) and
                                                      print the report
  python3 rimappa_usa.py --diverse                    list the functions that differ (name, sizes EU/USA)
As a module: R = Rimappa(); R.mappa(0x4be2e8) -> (usa_address or None, how_or_why).
"""
import bisect
import collections
import os
import re
import struct
import sys

QUI = os.path.dirname(os.path.abspath(__file__))
PROGETTO = os.path.dirname(QUI)
ELF_EU = os.path.join(PROGETTO, "disco", "SLES_529.50")
ELF_USA = os.path.join(PROGETTO, "disco-usa", "SLUS_209.02")
MAPPA_EU = os.path.join(PROGETTO, "mappa")
MAPPA_USA = os.path.join(PROGETTO, "mappa-usa")


class Elf:
    """The loadable bytes of a PS2 ELF, read by virtual address."""

    def __init__(self, path):
        d = open(path, "rb").read()
        phoff, = struct.unpack_from("<I", d, 0x1C)
        phentsize, phnum = struct.unpack_from("<HH", d, 0x2A)
        self.seg = []
        self.fine = 0
        for i in range(phnum):
            t, off, va, pa, fsz, msz = struct.unpack_from("<6I", d, phoff + i * phentsize)
            if t == 1 and fsz:
                self.seg.append((va, va + fsz, d[off:off + fsz]))
            if t == 1 and va < 0x2000000:
                self.fine = max(self.fine, va + msz)

    def parola(self, a):
        for lo, hi, b in self.seg:
            if lo <= a and a + 4 <= hi:
                return struct.unpack_from("<I", b, a - lo)[0]
        return None


# Opcodes whose 16-bit immediate can be the low half of an address (paired with a lui): addi, addiu, slti, sltiu,
# andi, ori, xori, daddi, daddiu, ldl, ldr, lq, sq, lb, lh, lwl, lw, lbu, lhu, lwr, lwu, sb, sh, swl, sw, sdl, sdr,
# swr, lwc1, lqc2, ld, swc1, sqc2, sd.
OP_IMM = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x18, 0x19, 0x1A, 0x1B, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,
          0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x31, 0x36, 0x37, 0x39, 0x3E, 0x3F}


def maschera(w):
    """The instruction word with the parts that hold moved addresses blanked."""
    op = w >> 26
    if op in (0x02, 0x03):  # j, jal
        return w & 0xFC000000
    if op == 0x0F:  # lui
        return w & 0xFFFF0000
    # Not relative to sp: stack offsets never move. Except "addiu sp, sp, %lo(stack)" after a "lui sp" (the Sony
    # library sets a thread's stack to a fixed address that moved, 0x118b50): an addiu sp,sp is masked too (a
    # function's frame size is still checked by the function's size and by its other instructions).
    if op in OP_IMM and (((w >> 21) & 31) != 29 or (op == 0x09 and ((w >> 16) & 31) == 29)):
        return w & 0xFFFF0000
    return w


def chiave_funzione(c):
    mangled = ""
    for ex in c[7].split(";") if len(c) > 7 else []:
        if ex.startswith("0x200="):
            mangled = ex[6:]
    return (mangled or c[3], c[6])


def leggi_funzioni(md):
    """{key: (lo, hi)}; keys made unique by the order of address when a name repeats in one file."""
    righe = []
    for r in open(os.path.join(md, "funzioni.tsv"), encoding="utf-8", errors="replace"):
        c = r.rstrip("\n").split("\t")
        lo, hi = int(c[0], 16), int(c[1], 16)
        if hi > lo:
            righe.append((lo, hi, chiave_funzione(c), c[3]))
    righe.sort()
    visti = collections.Counter()
    fn = {}
    nomi = {}
    for lo, hi, k, nome in righe:
        visti[k] += 1
        kk = k + (visti[k],)
        fn[kk] = (lo, hi)
        nomi[kk] = nome
    return fn, nomi


def leggi_dati(md):
    """{key: address}; global names alone, file-local ones with their file; repeated keys -> lowest address first."""
    per = collections.defaultdict(set)
    for r in open(os.path.join(md, "dati.tsv"), encoding="utf-8", errors="replace"):
        c = r.rstrip("\n").split("\t")
        a = int(c[0], 16)
        if a == 0:
            continue
        k = (c[1],) if c[2] == "global_var" else (c[1], c[4])
        per[k].add(a)
    d = {}
    for k, s in per.items():
        for i, a in enumerate(sorted(s)):
            d[k + (i + 1,)] = a
    return d


class Rimappa:
    def __init__(self):
        self.eu = Elf(ELF_EU)
        self.usa = Elf(ELF_USA)
        self.fe, self.nomi = leggi_funzioni(MAPPA_EU)
        self.fu, _ = leggi_funzioni(MAPPA_USA)
        self.comuni = {k for k in self.fe if k in self.fu}
        # functions of the European code, sorted by start
        self.inizi = sorted((lo, hi, k) for k, (lo, hi) in self.fe.items())
        self.inizi_lo = [x[0] for x in self.inizi]
        self.fe_per_lo = {lo: k for k, (lo, hi) in self.fe.items()}
        self._uguale = {}
        self._tratti = {}
        de, du = leggi_dati(MAPPA_EU), leggi_dati(MAPPA_USA)
        self.dati = sorted((a, k) for k, a in de.items() if k in du)
        self.dati_a = [x[0] for x in self.dati]
        self.dati_usa = du
        # the whole list of European symbol addresses (also the ones without an American twin) for the gaps
        self.tutti_dati_eu = sorted(set(de.values()))
        self.tutti_dati_usa = sorted(set(du.values()))
        self.fine_codice_eu = max(hi for lo, hi in self.fe.values())
        # the European map given to PS2Recomp (debug functions + Sony library pieces + pieces without a name)
        self.pezzi = []
        for r in list(open(os.path.join(MAPPA_EU, "funzioni.csv")))[1:]:
            c = r.strip().split(",")
            self.pezzi.append((int(c[1], 16), int(c[2], 16), c[0]))
        self.pezzi.sort()
        self.pezzi_lo = [x[0] for x in self.pezzi]

    def pezzo_di(self, a):
        i = bisect.bisect_right(self.pezzi_lo, a) - 1
        if i >= 0 and self.pezzi[i][0] <= a < self.pezzi[i][1]:
            return self.pezzi[i]
        return None

    def cerca_pezzo(self, lo, hi):
        """American start of the European code lo..hi found by its masked words, only if found exactly once
        within 64 KB before and 4 KB after the same address."""
        mod = [maschera(self.eu.parola(a)) for a in range(lo, hi, 4)]
        trovati = []
        for u in range(lo - 0x10000, lo + 0x1000, 4):
            if all(self.usa.parola(u + 4 * i) is not None and maschera(self.usa.parola(u + 4 * i)) == m
                   for i, m in enumerate(mod)):
                trovati.append(u)
                if len(trovati) > 1:
                    break
        return trovati[0] if len(trovati) == 1 else None

    def stesso_codice(self, a_eu, a_usa, n):
        for i in range(0, n, 4):
            we, wu = self.eu.parola(a_eu + i), self.usa.parola(a_usa + i)
            if we is None or wu is None or maschera(we) != maschera(wu):
                return False
        return True

    def funzione_uguale(self, k):
        if k not in self._uguale:
            (le, he), (lu, hu) = self.fe[k], self.fu[k]
            self._uguale[k] = he - le == hu - lu and self.stesso_codice(le, lu, he - le)
        return self._uguale[k]

    def funzione_di(self, a):
        i = bisect.bisect_right(self.inizi_lo, a) - 1
        if i >= 0 and self.inizi[i][0] <= a < self.inizi[i][1]:
            return self.inizi[i]
        return None

    def nome(self, k):
        return self.nomi.get(k, k[0])

    def mappa(self, a):
        """(American address, how) or (None, why not)."""
        f = self.funzione_di(a)
        if f:
            lo, hi, k = f
            if k not in self.comuni:
                return None, f"inside {self.nome(k)}, which has no twin in the American code"
            ulo = self.fu[k][0]
            if a == lo:
                return ulo, f"start of {self.nome(k)}"
            if self.funzione_uguale(k):
                return ulo + (a - lo), f"{self.nome(k)}+0x{a - lo:x} (same code)"
            return None, (f"inside {self.nome(k)}+0x{a - lo:x}, which differs between the two versions "
                          f"(sizes 0x{hi - lo:x} / 0x{self.fu[k][1] - self.fu[k][0]:x})")
        if 0x100000 <= a < self.fine_codice_eu + 0x20000 and not self._in_dati(a):
            u, come = self._buco(a)
            if u is None:
                p = self.pezzo_di(a)
                if p and p[1] - p[0] >= 16:
                    ulo = self.cerca_pezzo(p[0], p[1])
                    if ulo is not None:
                        return ulo + (a - p[0]), f"{p[2]}+0x{a - p[0]:x} (code without debug info, found once by its code)"
            return u, come
        p = self.pezzo_di(a)
        if p:  # functions without debug info after the code (static constructors...)
            ulo = self.cerca_pezzo(p[0], p[1])
            if ulo is not None:
                return ulo + (a - p[0]), f"{p[2]}+0x{a - p[0]:x} (code without debug info, found once by its code)"
            return None, f"inside {p[2]}, code without debug info not found once in the American code"
        return self._dato(a)

    def _in_dati(self, a):
        # a data symbol at or just before the address (within 64 KB) and no function after it before a
        i = bisect.bisect_right(self.dati_a, a) - 1
        if i < 0 or a - self.dati_a[i] > 0x10000:
            return False
        j = bisect.bisect_right(self.inizi_lo, a) - 1
        return j < 0 or self.inizi[j][1] <= self.dati_a[i]

    def _buco(self, a):
        """Code without debug info: the stretch between the debug function before and the one after."""
        j = bisect.bisect_right(self.inizi_lo, a) - 1
        if j < 0 or j + 1 >= len(self.inizi):
            return None, "code without debug info, outside the functions"
        prima, dopo = self.inizi[j], self.inizi[j + 1]
        if prima[2] not in self.comuni or dopo[2] not in self.comuni:
            return None, "code without debug info next to a function that has no twin"
        e_lo, e_hi = prima[1], dopo[0]
        u_lo, u_hi = self.fu[prima[2]][1], self.fu[dopo[2]][0]
        if e_hi - e_lo != u_hi - u_lo:
            return None, (f"code without debug info between {self.nome(prima[2])} and {self.nome(dopo[2])}: "
                          f"the stretch has a different length (0x{e_hi - e_lo:x} / 0x{u_hi - u_lo:x})")
        if (e_lo, u_lo) not in self._tratti:
            self._tratti[(e_lo, u_lo)] = self.stesso_codice(e_lo, u_lo, e_hi - e_lo)
        if not self._tratti[(e_lo, u_lo)]:
            return None, (f"code without debug info between {self.nome(prima[2])} and {self.nome(dopo[2])}: "
                          f"different code")
        return u_lo + (a - e_lo), f"after {self.nome(prima[2])}+0x{a - e_lo:x} (code without debug info, same code)"

    def _dato(self, a):
        i = bisect.bisect_right(self.dati_a, a) - 1
        if i < 0:
            return None, "before every data symbol"
        sa, k = self.dati[i]
        off = a - sa
        if off > 0x40000:
            return None, f"0x{off:x} past the nearest data symbol {k[0]}: too far to be sure"
        ua = self.dati_usa[k]
        # distance to the next symbol (any symbol) in both executables
        ne = bisect.bisect_right(self.tutti_dati_eu, sa)
        nu = bisect.bisect_right(self.tutti_dati_usa, ua)
        gap_e = self.tutti_dati_eu[ne] - sa if ne < len(self.tutti_dati_eu) else None
        gap_u = self.tutti_dati_usa[nu] - ua if nu < len(self.tutti_dati_usa) else None
        if off and gap_e != gap_u:
            return None, (f"{k[0]}+0x{off:x}: the object's room differs between the two versions "
                          f"(0x{gap_e or 0:x} / 0x{gap_u or 0:x})")
        if off and gap_e is not None and off >= gap_e:
            return None, f"{k[0]}+0x{off:x}: past the end of the object"
        return ua + off, f"data {k[0]}+0x{off:x}" if off else f"data {k[0]}"


def diverse(R):
    for k in sorted(R.comuni, key=lambda k: R.fe[k][0]):
        if not R.funzione_uguale(k):
            (le, he), (lu, hu) = R.fe[k], R.fu[k]
            print(f"{R.nome(k)}\t{he - le}\t{hu - lu}\t{le:08x}\t{lu:08x}")


def csv_usa(R, out):
    """mappa-usa/funzioni.csv for PS2Recomp, from the European one: debug functions from the American debug
    table (by name), every other piece moved with mappa(); names end with the American address."""
    righe, no = [], []
    usati = set()
    for lo, hi, nome in R.pezzi:
        base = re.sub(r"_[0-9a-f]+$", "", nome)
        k = R.fe_per_lo.get(lo)
        if k is not None and k in R.fu:
            ulo, uhi = R.fu[k]
            if hi != R.fe[k][1]:  # the European piece was cut short (overlap): same cut in the American one
                u_end, _ = R.mappa(hi) if hi < R.fe[k][1] else (None, "")
                if u_end is None:
                    no.append((lo, nome, "cut piece, end not mapped"))
                    continue
                uhi = u_end
            usati.add(k)
        elif k is not None:
            no.append((lo, nome, "debug function with no American twin (dropped)"))
            continue
        else:
            ulo, come = R.mappa(lo)
            ulast, come2 = R.mappa(hi - 4)
            if ulo is None or ulast is None or ulast + 4 - ulo != hi - lo:
                no.append((lo, nome, come if ulo is None else come2))
                continue
            uhi = ulast + 4
        righe.append((ulo, uhi, base))
    # American debug functions with no European twin
    agg = []
    for k, (ulo, uhi) in R.fu.items():
        if k not in usati and k not in R.fe:
            agg.append((ulo, uhi, k[0]))
    nomi_usa = {}
    for r in open(os.path.join(MAPPA_USA, "funzioni.tsv"), encoding="utf-8", errors="replace"):
        c = r.rstrip("\n").split("\t")
        nomi_usa[int(c[0], 16)] = c
    sys.path.insert(0, QUI)
    from crea_csv import demangle, pulisci
    for ulo, uhi, _ in agg:
        c = nomi_usa[ulo]
        k = chiave_funzione(c)
        righe.append((ulo, uhi, pulisci(demangle(c[3], k[0] if k[0] != c[3] else ""))))
    righe.sort()
    sovr = [(a, b) for a, b in zip(righe, righe[1:]) if a[1] > b[0]]
    with open(out, "w", newline="\n") as f:
        f.write("Name,Start,End,Size\n")
        for ulo, uhi, base in righe:
            f.write(f"{base}_{ulo:x},0x{ulo:08X},0x{uhi:08X},{uhi - ulo}\n")
    print(f"functions: {len(righe)} (European {len(R.pezzi)}), added American-only: {len(agg)} "
          f"{[x[2] for x in agg]}, not mapped: {len(no)}, overlaps: {len(sovr)}")
    for x in no:
        print(f"  NO  0x{x[0]:08x} {x[1]}: {x[2]}")
    for a, b in sovr[:10]:
        print(f"  OVERLAP {a} {b}")
    # every American debug function must be in the map
    starts = {r[0] for r in righe}
    manca = [k for k, (l, h) in R.fu.items() if l not in starts]
    print(f"American debug functions missing from the map: {len(manca)} {manca[:5]}")
    return no


def stubs_usa(R, cfg_eu, cfg_usa):
    """config for the American executable: the stubs "name@0x..." moved with mappa()."""
    testo = open(cfg_eu, encoding="utf-8").read()
    no = []

    def sost(m):
        a = int(m.group(2), 16)
        u, come = R.mappa(a)
        if u is None:
            no.append((m.group(1), a, come))
            return m.group(0)
        return f'"{m.group(1)}@0x{u:08X}"'
    testo2 = re.sub(r'"([\w.$]+)@0x([0-9A-Fa-f]+)"', sost, testo)
    testo2 = testo2.replace('input = "disco/SLES_529.50"', 'input = "disco-usa/SLUS_209.02"')
    testo2 = ("# GENERATED by strumenti/rimappa_usa.py --config from config/main.toml: the American executable\n"
              "# (SLUS_209.02), every stub address moved to its place in it. Do not edit: edit config/main.toml.\n"
              + testo2)
    open(cfg_usa, "w", encoding="utf-8", newline="\n").write(testo2)
    print(f"stubs moved: {len(re.findall(r'@0x', testo))}, not mapped: {len(no)}")
    for x in no:
        print(f"  NO  {x[0]}@0x{x[1]:08x}: {x[2]}")
    return no


# SA(0x...) in C++ and Python; PC(0x...) and F("stem", 0x...) in pc/widescreen_gen.py
SA_RE = re.compile(r"(?:\b(?:SA|PC)\(\s*|\bF\(\"\w+\",\s*)0x([0-9A-Fa-f]+)u?\s*\)")


def header(R, out, files):
    trovati = collections.OrderedDict()
    for f in files:
        for n, riga in enumerate(open(f, encoding="utf-8", errors="replace"), 1):
            for m in SA_RE.finditer(riga):
                a = int(m.group(1), 16)
                trovati.setdefault(a, []).append(f"{os.path.basename(f)}:{n}")
    ok, no = [], []
    for a in sorted(trovati):
        u, come = R.mappa(a)
        (ok if u is not None else no).append((a, u, come, trovati[a]))
    with open(out, "w", encoding="utf-8", newline="\n") as h:
        h.write("// Generated by strumenti/rimappa_usa.py: do not edit. Every SA(0x...) address of the port's own code\n"
                "// (European executable SLES_529.50) and its place in the American executable (SLUS_209.02).\n"
                "// Addresses that could not be proved are left out: SA() of one of them stops the American build.\n"
                "#pragma once\n#include <cstdint>\n"
                "struct SorIndirizzo { uint32_t eu, usa; };\n"
                "inline constexpr SorIndirizzo kSorIndirizziUsa[] = {\n")
        for a, u, come, dove in ok:
            h.write(f"    {{0x{a:08X}u, 0x{u:08X}u}}, // {come}\n")
        h.write("};\n")
    with open(os.path.splitext(out)[0] + ".tsv", "w", encoding="utf-8", newline="\n") as t:
        t.write("# Generated by strumenti/rimappa_usa.py (the same table as the .h, for pc/widescreen_gen.py --usa)\n")
        for a, u, come, dove in ok:
            t.write(f"{a:08x}\t{u:08x}\t{come}\n")
    print(f"addresses found: {len(trovati)}, mapped: {len(ok)}, NOT mapped: {len(no)}")
    for a, u, come, dove in ok:
        print(f"  ok  0x{a:08x} -> 0x{u:08x}  {come}  [{', '.join(dove[:3])}]")
    for a, u, come, dove in no:
        print(f"  NO  0x{a:08x}  {come}  [{', '.join(dove[:3])}]")
    return no


def main():
    R = Rimappa()
    args = sys.argv[1:]
    if args and args[0] == "--diverse":
        diverse(R)
        return
    if args and args[0] == "--header":
        header(R, args[1], args[2:])
        return
    if args and args[0] == "--csv":
        csv_usa(R, args[1] if len(args) > 1 else os.path.join(MAPPA_USA, "funzioni.csv"))
        return
    if args and args[0] == "--config":
        stubs_usa(R, os.path.join(PROGETTO, "config", "main.toml"),
                  args[1] if len(args) > 1 else os.path.join(PROGETTO, "config", "main-usa.toml"))
        return
    for x in args:
        a = int(x, 16)
        u, come = R.mappa(a)
        print(f"0x{a:08x} -> " + (f"0x{u:08x}  {come}" if u is not None else f"NOT MAPPED: {come}"))


if __name__ == "__main__":
    main()
