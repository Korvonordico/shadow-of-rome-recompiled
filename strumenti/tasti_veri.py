#!/usr/bin/env python3
"""Prova la tastiera vera di sor.exe con SendInput, per pochi secondi.

La finestra del gioco resta nascosta finche' non serve: quando nel registro compare
"filmato saltato" (poco prima del titolo) la mostra, le da' il fuoco, preme i tasti e
la nasconde di nuovo, ridando il fuoco alla finestra di prima. Prima di ogni tasto
controlla che il fuoco sia ancora sul gioco: se no si ferma, cosi' i tasti non finiscono
mai in un altro programma.

Uso: python tasti_veri.py <registro _NOME.log> [secondi di attesa dopo il filmato]
"""
import ctypes
import ctypes.wintypes as w
import sys
import time

u = ctypes.windll.user32
TITOLO = "Shadow of Rome"
SW_HIDE, SW_SHOW = 0, 5
INPUT_KEYBOARD = 1
KEYUP, EXTENDED = 0x2, 0x1

# nome: (virtual key, tasto esteso)
TASTI = {
    "invio": (0x0D, False), "spazio": (0x20, False), "x": (0x58, False), "c": (0x43, False),
    "v": (0x56, False), "z": (0x5A, False), "q": (0x51, False), "e": (0x45, False),
    "esc": (0x1B, False), "su": (0x26, True), "giu": (0x28, True), "sx": (0x25, True), "dx": (0x27, True),
}


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", w.WORD), ("wScan", w.WORD), ("dwFlags", w.DWORD), ("time", w.DWORD),
                ("dwExtraInfo", ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("ki", KEYBDINPUT), ("pad", ctypes.c_byte * 32)]
    _anonymous_ = ("u",)
    _fields_ = [("type", w.DWORD), ("u", _U)]


def invia(vk, esteso, su):
    scan = u.MapVirtualKeyW(vk, 0)
    fl = (KEYUP if su else 0) | (EXTENDED if esteso else 0)
    i = INPUT(type=INPUT_KEYBOARD)
    i.ki = KEYBDINPUT(vk, scan, fl, 0, 0)
    return u.SendInput(1, ctypes.byref(i), ctypes.sizeof(INPUT))


def trova():
    trovate = []

    @ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    def cb(h, _):
        b = ctypes.create_unicode_buffer(128)
        u.GetWindowTextW(h, b, 128)
        if b.value == TITOLO:
            trovate.append(h)
        return True

    u.EnumWindows(cb, 0)
    return trovate[0] if trovate else None


def main():
    log = sys.argv[1]
    attesa = float(sys.argv[2]) if len(sys.argv) > 2 else 9.0
    # Sequenza: dal titolo a Options > Sound, cambia e rimette, poi torna indietro.
    sequenza = [("invio", 3.0), ("giu", 0.7), ("giu", 0.7), ("giu", 0.7), ("spazio", 3.5), ("giu", 0.7), ("x", 2.0),
                ("dx", 1.2), ("sx", 1.2), ("v", 2.0), ("v", 2.5)]
    fine = time.time() + 150
    while time.time() < fine:
        try:
            with open(log, "rb") as f:
                if b"filmato saltato" in f.read():
                    break
        except OSError:
            pass
        time.sleep(0.5)
    else:
        print("filmato mai visto nel registro")
        return 1
    time.sleep(attesa)
    h = trova()
    if not h:
        print("finestra non trovata")
        return 1
    prima = u.GetForegroundWindow()
    u.ShowWindow(h, SW_SHOW)
    # Windows da' il fuoco solo a chi ha ricevuto l'ultimo input: Alt premuto e rilasciato.
    invia(0x12, False, False)
    invia(0x12, False, True)
    u.SetForegroundWindow(h)
    time.sleep(0.4)
    premuti = 0
    esito = 0
    for nome, dopo in sequenza:
        if u.GetForegroundWindow() != h:
            print(f"fuoco perso prima di '{nome}': mi fermo")
            esito = 2
            break
        vk, est = TASTI[nome]
        invia(vk, est, False)
        time.sleep(0.25)
        invia(vk, est, True)
        premuti += 1
        print(f"{time.strftime('%H:%M:%S')} {nome}", flush=True)
        time.sleep(dopo)
    u.ShowWindow(h, SW_HIDE)
    if prima:
        u.SetForegroundWindow(prima)
    print(f"tasti premuti: {premuti} su {len(sequenza)}")
    return esito


if __name__ == "__main__":
    sys.exit(main())
