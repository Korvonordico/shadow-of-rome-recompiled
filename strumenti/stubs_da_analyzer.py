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
           "iRotateThreadReadyQueue", "StartThread", "InitTLB", "sceGsSetDefLoadImage"}
# InitAlarm tolta (sor-menu, 30/10): tradotta, chiamava SetSyscall(0xFC, codice
# copiato nella memoria del kernel) e il runtime rispondeva KE_ERROR a ogni SetAlarm.
# sceGsSetDefLoadImage tradotta (sor-tappa1, 30/10): il gioco (classVRAM::tex_trans_set)
# spedisce da solo il pacchetto che riempie, quindi deve essere quello vero di Sony.
ESCLUSI_PREFISSI = ("sceGifPk", "sceDmaRecv", "sceDmaWatch", "sceDmaPause", "sceDeci2",
                    "sceTty", "sceMpegGetPictureRAW8")


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
