// Addresses of the game, for the European executable (SLES_529.50, sor.exe) and the American one (SLUS_209.02,
// sor-usa.exe). The port's code writes every fixed address of the game as the European one inside SA(): in the
// European build SA(x) is x; in the American build (SOR_REGION_USA, pc/compila_usa.sh) it is the same place in the
// American executable, from the table pc/sor_indirizzi_usa.h made by strumenti/rimappa_usa.py (by the name of the
// function or of the data, and only when the code around it is the same in both). An address that is not in the
// table stops the American build here, at compile time: nothing is guessed.
#pragma once
#include <cstdint>

#if defined(SOR_REGION_USA)
#include "sor_indirizzi_usa.h"
inline void sor_indirizzo_non_mappato_per_la_versione_americana() {} // not constexpr: reaching it is a compile error
consteval uint32_t SA(uint32_t eu)
{
    for (const SorIndirizzo &p : kSorIndirizziUsa)
        if (p.eu == eu)
            return p.usa;
    sor_indirizzo_non_mappato_per_la_versione_americana(); // see strumenti/rimappa_usa.py --header
    return 0;
}
#define SOR_DISC_SERIAL "SLUS-20902"
#else
consteval uint32_t SA(uint32_t eu) { return eu; }
#define SOR_DISC_SERIAL "SLES-52950"
#endif
