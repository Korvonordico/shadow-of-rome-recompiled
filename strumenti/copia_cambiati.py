#!/usr/bin/env python3
"""Copia in gen/main solo i file tradotti che sono cambiati (cosi' la
compilazione rifa' solo i gruppi toccati, non tutto il gioco).
- NUOVA: cartella dove ps2_recomp ha appena scritto;
- MAIN: cartella che compila CMake;
- VECCHI: dove spostare (non cancellare) i file di MAIN che non esistono piu';
- DA: ora (secondi epoch) di inizio della traduzione: i file di NUOVA piu'
  vecchi sono resti di giri precedenti e non contano.
Uso: python3 copia_cambiati.py NUOVA MAIN VECCHI DA
"""
import os, sys, shutil

nuova, main, vecchi, da = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4])
os.makedirs(vecchi, exist_ok=True)
attuali = set()
copiati = []
for n in os.listdir(nuova):
    p = os.path.join(nuova, n)
    if os.path.getmtime(p) < da - 2:
        continue
    attuali.add(n)
    q = os.path.join(main, n)
    a = open(p, "rb").read()
    if os.path.exists(q) and open(q, "rb").read() == a:
        continue
    open(q, "wb").write(a)
    copiati.append(n)
spostati = []
for n in os.listdir(main):
    if n.endswith((".cpp", ".h")) and n not in attuali:
        shutil.move(os.path.join(main, n), os.path.join(vecchi, n))
        spostati.append(n)
print(f"file tradotti: {len(attuali)}, cambiati: {len(copiati)}, tolti: {len(spostati)}")
for n in (copiati + ["(tolto) " + s for s in spostati])[:20]:
    print("  ", n)
