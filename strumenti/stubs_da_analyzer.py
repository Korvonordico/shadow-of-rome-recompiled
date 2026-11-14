#!/usr/bin/env python3
"""Legge il TOML scritto da ps2_analyzer e ne ricava:
- mappa/sdk_nomi.tsv: nome<TAB>indirizzo delle funzioni Sony riconosciute
  (da stubs "nome@0x..." e untracked_stubs), per crea_csv.py;
- l'elenco "stubs" (solo handler che il runtime conosce) da mettere in config/main.toml.
Uso: python3 stubs_da_analyzer.py analyzer.toml mappa/sdk_nomi.tsv config/main.toml
"""
import sys, re, tomllib

# Come rogue-galaxy-recomp (stesso PS2Recomp): restano tradotti dal codice PS2
# l'allocatore, fflush, i passaggi al kernel (syscall), sceGifPk* e i gestori
# del runtime ancora da fare (lanciano un errore).
ESCLUSI = {"_calloc_r", "_free_r", "_malloc_trim_r", "malloc_extend_top", "_malloc_r",
           "__malloc_lock", "__malloc_unlock", "fflush", "InitThread", "iWakeupThread",
           "iRotateThreadReadyQueue", "StartThread", "InitTLB", "sceGsSetDefLoadImage",
           "cos", "fabs", "floor", "rand", "srand"}
# cos/fabs/floor translated (sor-correzioni, 10/11/2026): in the game they are the software DOUBLE libm
# (argument in $a0, result in $v0); the runtime handlers are float ($f12 -> $f0), so the caller got its own
# argument back (cos is used by om05_gate_open, fabs/floor by the double pio2 reduction). Same bug as the
# "sin(x)=x" camera in KIexster/god-of-war-recomp, which rejects stubs on the double libm.
# rand/srand translated (sor-correzioni, 10/11/2026): the game's newlib rand (64-bit LCG, state in _reent)
# returns 0..0x7FFFFFFF; the runtime handler returned 0..0x7FFF, so effects that divide by 0x7FFFFFFF got
# almost the same value every time (cxldsakeb/GoW2-nativo-PS2, blocker 25).
# InitAlarm tolta (sor-menu, 30/10): tradotta, chiamava SetSyscall(0xFC, codice
# copiato nella memoria del kernel) e il runtime rispondeva KE_ERROR a ogni SetAlarm.
# sceGsSetDefLoadImage tradotta (sor-tappa1, 30/10): il gioco (classVRAM::tex_trans_set)
# spedisce da solo il pacchetto che riempie, quindi deve essere quello vero di Sony.
# sceVu0* tradotte (sor-tappa2, 30/10): la versione del runtime di sceVu0MulMatrix moltiplica
# nell'ordine opposto all'originale (a1*a2 invece di a2*a1, controllato sui byte del gioco):
# matrici di vista sbagliate e niente 3D. Il codice vero di Sony non puo' sbagliare ordine.
ESCLUSI_PREFISSI = ("sceGifPk", "sceDmaRecv", "sceDmaWatch", "sceDmaPause", "sceDeci2",
                    "sceTty", "sceMpegGetPictureRAW8", "sceVu0", "_sceVu0")


def escluso(n):
    return n in ESCLUSI or n.startswith(ESCLUSI_PREFISSI)


def main():
    src, tsv, cfg = sys.argv[1], sys.argv[2], sys.argv[3]
    d = tomllib.load(open(src, "rb"))
    g = d.get("general", d)
    stubs = g.get("stubs", d.get("stubs", []))
    untracked = (g.get("untracked_stubs", d.get("untracked_stubs", [])) +
                 g.get("entry_points", d.get("entry_points", [])))
    nomi = {}
    legati = []
    for s in stubs:
        m = re.match(r'^([\w.$]+)@(0x[0-9A-Fa-f]+)$', s)
        if m:
            nomi[int(m.group(2), 16)] = m.group(1)
            if not escluso(m.group(1)):
                legati.append(s)
    for s in untracked:
        m = re.match(r'^([\w.$]+)@(0x[0-9A-Fa-f]+)$', s)
        if m:
            nomi.setdefault(int(m.group(2), 16), m.group(1))
    with open(tsv, "w") as f:
        for a in sorted(nomi):
            f.write(f"{nomi[a]}\t{a:08x}\n")
    testo = open(cfg).read()
    blocco = "stubs = [\n" + "".join(f'    "{s}",\n' for s in legati) + "]"
    testo = re.sub(r'(?ms)^stubs = \[.*?\]', blocco, testo, count=1)
    open(cfg, "w").write(testo)
    print(f"stubs legati: {len(legati)}, nomi sdk (con quelli senza handler): {len(nomi)}")

if __name__ == "__main__":
    main()
