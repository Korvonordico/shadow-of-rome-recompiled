#!/usr/bin/env python3
"""Disassembla un modulo IOP (.IRX) con i nomi delle funzioni importate.
Usa il disassemblatore di rogue-galaxy-recomp (tools/mipsdis.py, non incluso qui): la
variabile RG_TOOLS dice dov'e' la sua cartella tools. Indirizzi relativi all'inizio del
modulo (come nel registro: base+offset).
Uso: python3 irx_dis.py FILE.IRX [INIZIO FINE]   (senza intervallo: entry e importazioni)
     python3 irx_dis.py FILE.IRX stringhe          (stringhe con il loro indirizzo)
"""
import os, sys, struct
sys.path.insert(0, os.environ.get("RG_TOOLS", "."))
import mipsdis

NOMI = {
    "sifman": {5: "sceSifInit", 7: "sceSifSetDma", 8: "sceSifDmaStat", 29: "sceSifCheckInit"},
    "sifcmd": {4: "sceSifInitCmd", 6: "sceSifGetSreg", 7: "sceSifSetSreg", 8: "sceSifSetCmdBuffer",
               10: "sceSifAddCmdHandler", 11: "sceSifRemoveCmdHandler", 12: "sceSifSendCmd",
               13: "isceSifSendCmd", 14: "sceSifInitRpc", 15: "sceSifBindRpc", 16: "sceSifCallRpc",
               17: "sceSifRegisterRpc", 18: "sceSifCheckStatRpc", 19: "sceSifSetRpcQueue",
               20: "sceSifGetNextRequest", 21: "sceSifExecRequest", 22: "sceSifRpcLoop",
               23: "sceSifGetOtherData", 24: "sceSifRemoveRpc", 25: "sceSifRemoveRpcQueue"},
    "thbase": {4: "CreateThread", 5: "DeleteThread", 6: "StartThread", 7: "StartThreadArgs",
               8: "ExitThread", 9: "ExitDeleteThread", 10: "TerminateThread", 14: "ChangeThreadPriority",
               20: "GetThreadId", 24: "SleepThread", 25: "WakeupThread", 26: "iWakeupThread",
               33: "DelayThread", 34: "GetSystemTime", 35: "SetAlarm", 36: "iSetAlarm",
               37: "CancelAlarm", 38: "iCancelAlarm", 39: "USec2SysClock", 40: "SysClock2USec",
               41: "GetSystemStatusFlag", 42: "GetThreadCurrentPriority", 43: "GetSystemTimeLow"},
    "thsemap": {4: "CreateSema", 5: "DeleteSema", 6: "SignalSema", 7: "iSignalSema", 8: "WaitSema",
                9: "PollSema", 11: "ReferSemaStatus"},
    "sysmem": {4: "AllocSysMemory", 5: "FreeSysMemory", 6: "QueryMemSize", 7: "QueryMaxFreeMemSize",
               8: "QueryTotalFreeMemSize", 14: "Kprintf"},
    "loadcore": {6: "RegisterLibraryEntries", 7: "ReleaseLibraryEntries"},
    "intrman": {4: "RegisterIntrHandler", 5: "ReleaseIntrHandler", 6: "EnableIntr", 7: "DisableIntr",
                8: "CpuDisableIntr", 9: "CpuEnableIntr", 17: "CpuSuspendIntr", 18: "CpuResumeIntr",
                23: "QueryIntrContext"},
    "timrman": {4: "AllocHardTimer", 5: "ReferHardTimer", 6: "FreeHardTimer", 7: "SetTimerMode",
                8: "GetTimerStatus", 9: "SetTimerCounter", 10: "GetTimerCounter", 11: "SetTimerCompare",
                12: "GetTimerCompare", 13: "SetHoldMode", 14: "GetHoldMode", 15: "GetHoldReg",
                16: "GetHardTimerIntrCode", 20: "SetTimerHandler", 22: "SetOverflowHandler",
                23: "SetupHardTimer", 24: "StartHardTimer", 25: "StopHardTimer"},
    "stdio": {4: "printf"},
    "ioman": {4: "open", 5: "close", 6: "read", 7: "write", 8: "lseek", 9: "ioctl"},
    "sysclib": {4: "setjmp", 5: "longjmp", 12: "memcmp", 13: "memcpy", 14: "memmove", 15: "memset",
                16: "bcmp", 17: "bcopy", 18: "bzero", 19: "prnt", 20: "sprintf", 22: "strcat",
                23: "strchr", 24: "strcmp", 25: "strcpy", 26: "strcspn", 27: "index", 28: "rindex",
                29: "strlen", 30: "strncat", 31: "strncmp", 32: "strncpy", 36: "strtol", 37: "atob",
                38: "strstr", 39: "strtok", 40: "strtoul", 42: "vsprintf"},
}


def main():
    d = open(sys.argv[1], "rb").read()
    entry = struct.unpack_from("<I", d, 24)[0]
    shoff = struct.unpack_from("<I", d, 32)[0]
    shnum = struct.unpack_from("<H", d, 48)[0]
    shstr = struct.unpack_from("<H", d, 50)[0]
    secs = []
    for i in range(shnum):
        s = struct.unpack_from("<10I", d, shoff + i * 40)
        secs.append(s)
    so = secs[shstr][4]
    testo = None
    for s in secs:
        nome = d[so + s[0]:].split(b"\0")[0].decode()
        if nome == ".text":
            testo = s
    off, size, addr = testo[4], testo[5], testo[3]
    mipsdis.BASE = addr
    mipsdis.FILEOFF = off
    syms = []
    # tabelle di importazione: 0x41E00000, 0, versione, nome[8], poi coppie jr ra / addiu zero,N
    for o in range(off, off + size - 20, 4):
        if struct.unpack_from("<I", d, o)[0] == 0x41E00000 and struct.unpack_from("<I", d, o + 4)[0] == 0:
            lib = d[o + 12:o + 20].split(b"\0")[0].decode("latin-1")
            p = o + 20
            while struct.unpack_from("<I", d, p)[0] == 0x03E00008:
                n = struct.unpack_from("<I", d, p + 4)[0] & 0xFFFF
                a = p - off + addr
                syms.append((a, a + 8, f"{lib}:{NOMI.get(lib, {}).get(n, n)}"))
                p += 8
    syms.sort()
    if len(sys.argv) > 2 and sys.argv[2] == "stringhe":
        cur = b""
        for i in range(off, len(d)):
            c = d[i]
            if 32 <= c < 127 or c in (9, 10):
                cur += bytes([c])
            else:
                if len(cur) >= 5:
                    print(f"{i - len(cur) - off + addr:#07x} {cur.decode()!r}")
                cur = b""
        return
    if len(sys.argv) > 3:
        a, b = int(sys.argv[2], 16), int(sys.argv[3], 16)
    else:
        print(f"entry {entry:#x}, .text {addr:#x}+{size:#x}")
        for s in syms:
            print(f"  {s[0]:#07x} {s[2]}")
        a, b = entry, entry + 0x100
    mipsdis.disrange(d, syms, a, b)


if __name__ == "__main__":
    main()
