#include <stdio.h>

#include "pico/stdlib.h"

#include "nvsram_emu.h"

#include "bdm.h"
#include "vxw_uart.h"

// By default, CS2 is assigned the range form 0x0280_0000 to 0x0280_7FFF.
// CS2 enables the Dallas DS1742W.
// Disable CS2 and assigned this address range to CS7, which goes to
// CS1 of the expansion connector and RP2350.
// This way, the RP2350 will see all memory transactions that are supposed
// to go to DS1742W and it can emulate it.
bool nvsram_emu_patch_cs()
{
    // This code assumes that the CPU is already halted...
    // Programming of the memory map registers is done by the boot loader, so
    // this code can be called when the main application starts at address
    // 0x6000.

    const uint32_t BR2 = 0xFF000110;
    const uint32_t OR2 = 0xFF000114;

    const uint32_t BR7 = 0xFF000138;
    const uint32_t OR7 = 0xFF00013C;

    // On my TRDS3052, the programmed values of these 2 registers are:
    // BR2 = 0x02800401
    // OR2 = 0xFFFF8D24
    // But we can't rely on that: other B scopes have different wait cycles.
    // This doesn't matter for CS2 because that one simply need to be disabled.

    // Simply disable bit 31 (bit 0 for little endian) - Valid
    // The other bits don't matter...
    vxw_log("Disabling CS2...\n\r");

    const char *error = "!!! Disabling CS2 failed";
    bool ret = bdm_write_word(BR2, 0x02800400);
    if (!ret)
        goto fail;

    vxw_log("Enabling CS7...\n\r");
    error = "!!! Reprogramming CS7 failed";
    // Use 0xFFFF8DF4 instead of 0xFFFF8D24. 
    // SCY=15:  Number of wait cycles 
    // TRLX=1:  Timing relax -> double the number of wait states and increase setup and hold times for CS and OE/WE.
    // So right now, we're lookig at 30 wait cycles or 150 CPU cycles to handle NVSRAM accesses.
    ret =        bdm_write_word(OR7, 0xFFFF8DF4);
    ret = ret && bdm_write_word(BR7, 0x02800401);
    if (!ret)
        goto fail;

    return true;

fail:
    return false;

}


