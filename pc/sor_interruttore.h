// Shadow of Rome PC port - TOGGLE: one PS2 button held down with one press (the launcher's CONTROLS > TOGGLE).
//
// the author, 9/11/2026: "il tasto che lo clicchi una volta e viene considerato premuto", to stay crouched without
// holding the key. One press holds the button down, the next press lets it go.
//
// pc/sor_tasti.cpp (sorTastiLeggi) runs it on the final buttons, after the keyboard, the mouse buttons and the
// controller, so whatever does that action toggles it. Pure, with no raylib: the test bench builds it alone.
#pragma once

#include <cstdint>

struct SorInterruttore
{
    uint16_t bit = 0;    // the PS2 pad bit that toggles (0: none, the buttons pass as they are)
    bool prima = false;  // was it pressed at the last read
    bool tenuto = false; // held down by the toggle now

    // btns: the PS2 buttons, 1 = up (a pressed button clears its bit). Only a new press (up -> down) switches, so
    // reading the same buttons again (the game reads both pad ports) changes nothing.
    uint16_t applica(uint16_t btns)
    {
        if (!bit)
        {
            prima = tenuto = false;
            return btns;
        }
        const bool giu = (btns & bit) == 0;
        if (giu && !prima)
            tenuto = !tenuto;
        prima = giu;
        return tenuto ? static_cast<uint16_t>(btns & ~bit) : static_cast<uint16_t>(btns | bit);
    }
};
