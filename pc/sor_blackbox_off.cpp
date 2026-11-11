// Stands in for sor_blackbox.cpp when the black box is not built in, as in the published build: every call does
// nothing. The black box (Iris Insight: a picture and a short report when the game freezes, crashes or slows down)
// is used only in my local test builds. Its licence is not the GPL, so it is not in this repository.
#include "ps2_runtime.h"
#include <cstdint>

void sorBlackboxStart() {}
void sorBlackboxSetRuntime(PS2Runtime *) {}
bool sorBlackboxOn() { return false; }
void sorBlackboxInstall(PS2Runtime &) {}
void sorBlackboxFrame(const uint8_t *, uint32_t, uint32_t) {}
void sorBlackboxClosing() {}
bool sorBlackboxClosedByUser() { return true; }
void sorBlackboxFatal(const char *, const char *) {}
void sorBlackboxCommand(const char *) {}
