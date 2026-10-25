// Code Genie: the cheat code table and toggling codes on and off (cheat.h).

#include <string.h>

#include "cheat.h"

// Hexspeak, for players to puzzle out: 5AFE 5AFE is "safe safe", DE5CE 00N
// "descend N".
const CheatDef cheat_defs[CHEAT_COUNT] = {
    [CHEAT_GOD]      = { 0x5AFE5AFEu, "God mode" },
    [CHEAT_ONE_HIT]  = { 0xDEADBEEFu, "One-hit kills" },
    [CHEAT_UPGRADES] = { 0xACCE55EDu, "All upgrades" },
    [CHEAT_SEWERS]   = { 0xDE5CE002u, "Start: Sewers" },
    [CHEAT_CAVERNS]  = { 0xDE5CE003u, "Start: Caverns" },
    [CHEAT_DEPTHS]   = { 0xDE5CE004u, "Start: Depths" },
    [CHEAT_BLAST]    = { 0x0B1A57EDu, "Endless blast" },
    [CHEAT_STAMINA]  = { 0x00C0FFEEu, "Endless dash" },
    [CHEAT_DAGGERS]  = { 0x00DA66E2u, "Endless daggers" },
    [CHEAT_WIPE]     = { 0xC1EA2ED0u, "Room wipe" },
    [CHEAT_GOLD]     = { 0x601D601Du, "Gold x5" },
    [CHEAT_GIANT]    = { 0x00B16B16u, "Giant mode" },
    [CHEAT_HARD]     = { 0x0DD5BAD0u, "Hard mode" },
};

uint32_t cheat_active;

CodeResult cheat_enter(uint32_t *active, uint32_t code, CheatId *id)
{
    if (CODE_BUG_REPORT == code)
        return CODE_REPORT;
    for (int i = 0; i < CHEAT_COUNT; i++) {
        if (cheat_defs[i].code != code)
            continue;
        uint32_t bit = CHEAT_BIT(i);
        *id = (CheatId)i;
        if (*active & bit) {
            *active &= ~bit;
            return CODE_OFF;
        }
        if (bit & CHEAT_AREAS)
            *active &= ~CHEAT_AREAS;   // one start area at a time
        *active |= bit;
        return CODE_ON;
    }
    return CODE_UNKNOWN;
}

int cheat_start_area(uint32_t active)
{
    if (cheat_on(active, CHEAT_SEWERS))
        return 1;
    if (cheat_on(active, CHEAT_CAVERNS))
        return 2;
    if (cheat_on(active, CHEAT_DEPTHS))
        return 3;
    return 0;
}

uint32_t cheat_upgrades(uint32_t active, uint32_t owned)
{
    return cheat_on(active, CHEAT_UPGRADES) ? 0xFFFFFFFFu : owned;
}

void cheat_list(uint32_t mask, char *out, int size)
{
    if (size <= 0)
        return;
    out[0] = '\0';
    int len = 0;
    for (int i = 0; i < CHEAT_COUNT; i++) {
        if (!(mask & CHEAT_BIT(i)))
            continue;
        const char *name = cheat_defs[i].name;
        int need = (len ? 2 : 0) + (int)strlen(name);
        if (len + need >= size)
            break;
        if (len) {
            strcpy(out + len, ", ");
            len += 2;
        }
        strcpy(out + len, name);
        len += (int)strlen(name);
    }
}
