// The crash screen: a CPU exception handler that reports on the bottom
// screen (crash.h).

#include <stdbool.h>
#include <stdint.h>

#include <nds.h>

#include "config.h"
#include "crash.h"

static PrintConsole crash_console;

static void put(const char *str)
{
    consolePrintString(str);
}

static void put_hex(uint32_t v)
{
    // consolePrintUnsigned() has no zero padding; registers read better
    // aligned.
    for (int shift = 28; shift >= 0; shift -= 4)
        consolePrintChar("0123456789ABCDEF"[(v >> shift) & 15]);
}

static void put_reg(const char *name, int index)
{
    put(name);
    put_hex((uint32_t)exceptionRegisters[index]);
}

// Runs in exception mode on libnds' exception stack, with the registers of
// the faulting code in exceptionRegisters[] (see nds/exceptions.h). Keeps to
// register writes and the libnds console print functions: printf() takes
// picolibc locks that are not safe here (libnds source/arm9/system/
// exceptions.c), and the rest of the game state can no longer be trusted.
static void crash_handler(void)
{
    REG_IME = 0;

    // Same test as libnds guruMeditationDump(): the exception mode tells
    // the two causes apart.
    bool undefined = CPSR_MODE_UNDEFINED == (getCPSR() & CPSR_MODE_MASK);

    // Top screen: plain dark red backdrop, no 3D or BG layers.
    REG_MASTER_BRIGHT = 0;
    REG_DISPCNT = MODE_0_2D;
    BG_PALETTE[0] = RGB15(8, 0, 2);

    // Bottom screen: a fresh text console over the game's sub BG.
    REG_MASTER_BRIGHT_SUB = 0;
    videoSetModeSub(MODE_0_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&crash_console, 0, BgType_Text4bpp, BgSize_T_256x256,
                SUB_TXT_MAP_BASE, SUB_TXT_TILE_BASE, false, true);
    REG_DISPCNT_SUB = MODE_0_2D | DISPLAY_BG0_ACTIVE;
    BG_PALETTE_SUB[0] = RGB15(8, 0, 2);
    consoleSelect(&crash_console);
    consoleClear();

    consoleSetCursor(NULL, 1, 1);
    put("DELVE CRASHED: ");
    put(undefined ? "bad instruction" : "data abort");
    consoleSetCursor(NULL, 1, 3);
    put("v" DELVE_VERSION_STRING " " DELVE_BUILD_TYPE "  " DELVE_GIT_HASH);

    consoleSetCursor(NULL, 1, 5);
    put_reg("pc ", 15);
    put_reg("  lr ", 14);
    consoleSetCursor(NULL, 1, 6);
    put_reg("sp ", 13);
    static const char *const names[13] = {
        "r0  ", "r1  ", "r2  ", "r3  ", "r4  ", "r5  ", "r6  ",
        "r7  ", "r8  ", "r9  ", "r10 ", "r11 ", "r12 ",
    };
    for (int i = 0; i < 13; i++) {
        consoleSetCursor(NULL, i & 1 ? 16 : 1, 8 + i / 2);
        put_reg(names[i], i);
    }

    consoleSetCursor(NULL, 1, 17);
    put("Please report this screen at");
    consoleSetCursor(NULL, 1, 18);
    put("github.com/pandymic/delve-nds");
    consoleSetCursor(NULL, 1, 20);
    put("Saved progress from earlier");
    consoleSetCursor(NULL, 1, 21);
    put("runs is kept. Power off now.");

    while (1)
        ;
}

void crash_install(void)
{
    setExceptionHandler(crash_handler);
}

void crash_test(void)
{
    // Permanently undefined ARM encoding (UDF #0).
    __asm__ volatile(".word 0xE7F000F0");
    __builtin_unreachable();
}
