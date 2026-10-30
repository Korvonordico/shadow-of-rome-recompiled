#!/usr/bin/env python3
"""Legge la sezione .debug (DWARF versione 1, Metrowerks) dell'eseguibile PS2
e scrive le tabelle di funzioni, dati e file sorgente.

Uso: python3 dwarf1.py ELF CARTELLA_USCITA
Scrive: funzioni.tsv (inizio, fine, dimensione, nome, tipo, file),
        dati.tsv (indirizzo, dimensione se nota, nome, tipo, file),
        file.tsv (inizio, fine, nome del file sorgente, linguaggio, compilatore),
        riassunto.txt
"""
import sys, struct, os, collections

FORMS = {1: 4, 2: 4, 5: 2, 6: 4, 7: 8}  # dimensione fissa per forma
TAG = {0x01: "array", 0x02: "class", 0x03: "entry", 0x04: "enum", 0x05: "param",
       0x06: "global_sub", 0x07: "global_var", 0x0a: "label", 0x0b: "block",
       0x0c: "local_var", 0x0d: "member", 0x0f: "pointer", 0x10: "ref",
       0x11: "cu", 0x13: "struct", 0x14: "sub", 0x15: "sub_type",
       0x16: "typedef", 0x17: "union", 0x18: "unspec", 0x1c: "inherit",
       0x1d: "inlined_sub"}
AT_SIBLING, AT_LOCATION, AT_NAME = 0x01, 0x02, 0x03
AT_BYTE_SIZE, AT_LOW, AT_HIGH, AT_LANG, AT_PRODUCER = 0x0b, 0x11, 0x12, 0x13, 0x25


def leggi_elf(path):
    d = open(path, "rb").read()
    shoff, = struct.unpack_from("<I", d, 0x20)
    shnum, shstrndx = struct.unpack_from("<HH", d, 0x30)
    secs = []
    for i in range(shnum):
        s = struct.unpack_from("<IIIIIIIIII", d, shoff + 40 * i)
        secs.append(s)
    stro = secs[shstrndx][4]
    out = {}
    for s in secs:
        n = d[stro + s[0]:d.index(b"\0", stro + s[0])].decode()
        out[n] = d[s[4]:s[4] + s[5]]
    return out


def die_iter(dbg):
    """Restituisce (offset, livello, tag, attributi) per ogni DIE."""
    off = 0
    n = len(dbg)
    pila = []  # fine dei figli
    while off + 4 <= n:
        while pila and off >= pila[-1]:
            pila.pop()
        ln, = struct.unpack_from("<I", dbg, off)
        if ln < 8:
            off += max(ln, 4)
            continue
        fine = off + ln
        tag, = struct.unpack_from("<H", dbg, off + 4)
        p = off + 6
        attr = {}
        while p < fine:
            a, = struct.unpack_from("<H", dbg, p)
            p += 2
            form = a & 0xf
            nome = a >> 4
            if form in FORMS:
                sz = FORMS[form]
                if sz == 2:
                    v, = struct.unpack_from("<H", dbg, p)
                elif sz == 4:
                    v, = struct.unpack_from("<I", dbg, p)
                else:
                    v, = struct.unpack_from("<Q", dbg, p)
                p += sz
            elif form == 3:
                l, = struct.unpack_from("<H", dbg, p)
                v = dbg[p + 2:p + 2 + l]
                p += 2 + l
            elif form == 4:
                l, = struct.unpack_from("<I", dbg, p)
                v = dbg[p + 4:p + 4 + l]
                p += 4 + l
            elif form == 8:
                e = dbg.index(b"\0", p)
                v = dbg[p:e].decode("latin-1")
                p = e + 1
            else:
                raise ValueError(f"forma {form} sconosciuta a {p:#x} (DIE {off:#x})")
            attr.setdefault(nome, v)
            if nome not in attr or attr[nome] is not v:
                attr.setdefault(("extra", nome), []).append(v)
        yield off, len(pila), tag, attr
        sib = attr.get(AT_SIBLING)
        if sib is not None and sib > fine:
            pila.append(sib)
        off = fine


def indirizzo_da_location(blk):
    # OP_ADDR = 3 seguito da 4 byte
    if isinstance(blk, bytes) and len(blk) == 5 and blk[0] == 3:
        return struct.unpack_from("<I", blk, 1)[0]
    return None


def main():
    elf, outd = sys.argv[1], sys.argv[2]
    os.makedirs(outd, exist_ok=True)
    secs = leggi_elf(elf)
    dbg = secs[".debug"]
    funzioni, dati, cus = [], [], []
    tag_cont = collections.Counter()
    attr_cont = collections.Counter()
    cu = None
    for off, liv, tag, attr in die_iter(dbg):
        tag_cont[TAG.get(tag, hex(tag))] += 1
        for k in attr:
            if not isinstance(k, tuple):
                attr_cont[hex(k)] += 1
        if tag == 0x11:
            cu = attr.get(AT_NAME, "?")
            cus.append((attr.get(AT_LOW), attr.get(AT_HIGH), cu,
                        attr.get(AT_LANG), attr.get(AT_PRODUCER, "")))
        elif tag in (0x06, 0x14, 0x03):
            lo, hi = attr.get(AT_LOW), attr.get(AT_HIGH)
            nome = attr.get(AT_NAME, "")
            # nomi alternativi (es. nome C++ completo) in attributi utente
            altri = {hex(k): v for k, v in attr.items()
                     if not isinstance(k, tuple) and isinstance(v, str) and k != AT_NAME}
            funzioni.append((lo, hi, nome, TAG.get(tag), liv, cu, altri, off))
        elif tag in (0x07, 0x0c):
            a = indirizzo_da_location(attr.get(AT_LOCATION))
            if a is not None:
                dati.append((a, attr.get(AT_NAME, ""), TAG.get(tag), liv, cu))
    # scrive
    with open(os.path.join(outd, "file.tsv"), "w") as f:
        for lo, hi, n, lang, prod in cus:
            f.write(f"{lo or 0:08x}\t{hi or 0:08x}\t{n}\t{lang}\t{prod}\n")
    con_pc = [x for x in funzioni if x[0] is not None and x[1] is not None]
    with open(os.path.join(outd, "funzioni.tsv"), "w") as f:
        for lo, hi, n, t, liv, c, altri, off in sorted(con_pc):
            ex = ";".join(f"{k}={v}" for k, v in altri.items())
            f.write(f"{lo:08x}\t{hi:08x}\t{hi - lo}\t{n}\t{t}\t{liv}\t{c}\t{ex}\n")
    with open(os.path.join(outd, "dati.tsv"), "w") as f:
        for a, n, t, liv, c in sorted(dati):
            f.write(f"{a:08x}\t{n}\t{t}\t{liv}\t{c}\n")
    # riassunto
    uniche = {}
    for lo, hi, n, t, liv, c, altri, off in con_pc:
        if lo != hi:
            uniche.setdefault(lo, (hi, n, t))
    testo = secs.get("", b"")
    r = []
    r.append(f"DIE totali: {sum(tag_cont.values())}")
    r.append("tag: " + ", ".join(f"{k}={v}" for k, v in tag_cont.most_common()))
    r.append("attributi: " + ", ".join(f"{k}={v}" for k, v in attr_cont.most_common()))
    r.append(f"file sorgente (compile unit): {len(cus)}")
    r.append(f"voci di funzione: {len(funzioni)}, con indirizzi: {len(con_pc)}")
    r.append(f"funzioni con indirizzo di inizio diverso (non vuote): {len(uniche)}")
    r.append(f"  di cui con nome: {sum(1 for v in uniche.values() if v[1])}")
    r.append(f"dati con indirizzo fisso: {len(dati)}, nomi diversi: {len(set(d[1] for d in dati))}")
    tot = sum(hi - lo for lo, (hi, n, t) in uniche.items())
    r.append(f"byte di codice coperti dalle funzioni: {tot} ({tot / 0x3b1b80 * 100:.1f}% della sezione da 0x3b1b80)")
    open(os.path.join(outd, "riassunto.txt"), "w").write("\n".join(r) + "\n")
    print("\n".join(r))


if __name__ == "__main__":
    main()
