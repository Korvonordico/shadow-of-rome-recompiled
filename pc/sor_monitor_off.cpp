// Stands in for sor_monitor.cpp when the monitor is not built in, as in the published build: every call does
// nothing. The monitor (a picture and a short report when the game freezes, crashes or slows down)
// is used only in my local test builds. Its licence is not the GPL, so it is not in this repository.
#include "ps2_runtime.h"
#include <cstdint>

void sorMonitorStart() {}
void sorMonitorSetRuntime(PS2Runtime *) {}
void sorMonitorInstall(PS2Runtime &) {}
void sorMonitorFrame(const uint8_t *, uint32_t, uint32_t) {}
void sorMonitorClosing() {}
bool sorMonitorClosedByUser() { return true; }
void sorMonitorFatal(const char *, const char *) {}
void sorMonitorCommand(const char *) {}
