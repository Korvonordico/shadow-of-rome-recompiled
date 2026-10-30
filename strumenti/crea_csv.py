#!/usr/bin/env python3
"""Crea la mappa delle funzioni per PS2Recomp (CSV: Name,Start,End,Size).
Unisce: le funzioni del debug (mappa/funzioni.tsv), le funzioni della
libreria Sony riconosciute (opzionale, file nome<TAB>inizio esadecimale) e
le funzioni senza nome trovate nei buchi (mappa/funzioni_trovate.tsv).
Uso: python3 crea_csv.py mappa USCITA.csv [sdk_nomi.tsv]
"""
import sys, re

def demangle(nome, mangled):
    """Nome leggibile: Classe_metodo per i metodi C++ di Metrowerks."""
    if not mangled:
        return nome
    m = re.match(r'^(.+?)__(\d+)(.*)$', mangled)
    if m and not mangled.startswith("__"):
        n = int(m.group(2))
        cl = m.group(3)[:n]
        if len(cl) == n and re.match(r'^\w+$', cl):
            return f"{cl}_{m.group(1)}"
    m = re.match(r'^(__\w+?)__(\d+)(.*)$', mangled)  # __ct, __dt, operatori
    if m:
        n = int(m.group(2))
        cl = m.group(3)[:n]
        if len(cl) == n:
            return f"{cl}_{m.group(1).strip('_')}"
    return mangled


def pulisci(s):
    s = re.sub(r'[^A-Za-z0-9_]', '_', s)
    return s or "func"


def main():
    md, out = sys.argv[1], sys.argv[2]
    sdk = {}
    if len(sys.argv) > 3:
        for r in open(sys.argv[3]):
            c = r.split()
            if len(c) >= 2:
                sdk[int(c[1], 16)] = c[0]
    fn = {}
    for r in open(md + "/funzioni.tsv"):
        c = r.rstrip("\n").split("\t")
        lo, hi = int(c[0], 16), int(c[1], 16)
        mangled = ""
        for ex in c[7].split(";") if len(c) > 7 else []:
            if ex.startswith("0x200="):
                mangled = ex[6:]
        if lo not in fn and hi > lo:
            fn[lo] = (hi, pulisci(demangle(c[3], mangled)), "debug")
    n_debug = len(fn)
    n_sdk = n_senza = 0
    import os
    righe = open(md + "/funzioni_trovate.tsv").readlines()
    if os.path.exists(md + "/funzioni_extra.tsv"):  # zona dopo il codice con debug
        righe += open(md + "/funzioni_extra.tsv").readlines()
    # Pezzi dei buchi (taglio a ogni jr ra), poi gli inizi delle funzioni Sony
    # riconosciute da ps2_analyzer diventano confini: se un inizio cade dentro un
    # pezzo lo divide, se cade nei byte vuoti subito prima lo allunga indietro.
    pezzi = {}
    for r in righe:
        c = r.split()
        lo, hi = int(c[0], 16), int(c[1], 16)
        if lo in fn or hi <= lo:
            continue
        pezzi[lo] = hi
    for a in sorted(sdk):
        if a in fn or a in pezzi:
            continue
        dentro = [s for s, e in pezzi.items() if s < a < e]
        if dentro:
            s = dentro[0]
            e = pezzi[s]
            pezzi[s] = a
            if a == e - 4 and e in pezzi:
                # l'inizio e' lo slot di ritardo del "jr ra" che chiude il pezzo:
                # la funzione continua nel pezzo seguente
                pezzi[a] = pezzi.pop(e)
            else:
                pezzi[a] = e
            continue
        dopo = [s for s in pezzi if a < s <= a + 16]
        if dopo:
            s = min(dopo)
            pezzi[a] = pezzi.pop(s)
    for lo, hi in pezzi.items():
        if lo in sdk:
            fn[lo] = (hi, pulisci(sdk[lo]), "sdk")
            n_sdk += 1
        else:
            fn[lo] = (hi, f"sub_{lo:08x}", "buco")
            n_senza += 1
    # controllo sovrapposizioni
    inizi = sorted(fn)
    sovr = 0
    for a, b in zip(inizi, inizi[1:]):
        if fn[a][0] > b:
            sovr += 1
            fn[a] = (b, fn[a][1], fn[a][2])
    # Ogni nome finisce con il suo indirizzo (come rogue-galaxy-recomp): un nome
    # nudo uguale a una funzione del runtime (memcpy, sceCdRead...) verrebbe
    # legato da solo; cosi' si lega solo quello che dice "stubs" nel toml.
    # "sub_" e' riservato al recompiler: le funzioni senza nome sono "nonome_".
    with open(out, "w") as f:
        f.write("Name,Start,End,Size\n")
        for a in inizi:
            hi, n, _ = fn[a]
            if n.startswith("sub_"):
                n = "nonome"
            f.write(f"{n}_{a:x},0x{a:08X},0x{hi:08X},{hi - a}\n")
    print(f"funzioni: {len(fn)} (debug {n_debug}, sdk riconosciute {n_sdk}, senza nome {n_senza}); sovrapposizioni tagliate: {sovr}")


if __name__ == "__main__":
    main()
