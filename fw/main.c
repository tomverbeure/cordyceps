#include <stdio.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "bdm.h"
#include "pins.h"
#include "rtc.h"
#include "scpi_uart.h"
#include "vxw_uart.h"

#define DEBUG_PB19  1
#define DEBUG_CS7   1

// FIXME: this code block handling of USB transfers.
// Convert to an FSM to allow cooperative multi-tasking?
// Leave the CPU halted so boot_setup() can arm the next boot watchpoint.
bool enable_boot_diagnostics()
{
    vxw_log(">>> enable_boot_diagnostics():\n\r");

    //============================================================
    // Set PBDAT watchpoint
    //============================================================
    vxw_log(">>> Setting PBDAT watchpoint.\n\r");

    // mpc860PBDAT: memcInit samples PB19 to select verbose boot diagnostics.
    const uint32_t PBDAT = 0xff000ac4;

    const char *error = "!!! BDM reset/halt failed\n\r";
    int ret = bdm_hreset_and_halt_cpu();
    if (!ret)
        goto fail;

    error = "!!! BDM watchpoint setup failed\n\r";
    ret = bdm_set_watchpoint(PBDAT);
    if (!ret)
        goto fail;

    error = "!!! BDM resume to boot watchpoint failed\n\r";
    ret = bdm_request_resume();
    if (!ret)
        goto fail;

    //============================================================
    // Watchpoint hit
    //============================================================
    vxw_log(">>> Waiting for PBDAT watchpoint hit.\n\r");

    error = "!!! BDM boot watchpoint wait failed\n\r";
    ret = bdm_wait_for_watchpoint(PBDAT, 1000);
    if (!ret)
        goto fail;

    // Disable the watchpoint before proceeding; later PBDAT reads must not halt.
    error = "!!! BDM watchpoint removal failed\n\r";
    ret = bdm_clear_watchpoint();
    if (!ret)
        goto fail;

    #if DEBUG_PB19==1
    {
        error = "!!! BDM R3 read failed\n\r";
        uint32_t r3;
        ret = bdm_read_gpr(3, &r3);
        if (!ret)
            goto fail;
    
        // PB19 uses PowerPC's MSB-first numbering: its mask is 0x00001000.
        char str[64];
        snprintf(str, sizeof(str), ">>> R3=0x%08lx, PB[19]=%u\n\r", (unsigned long)r3, (unsigned)((r3 >> 12) & 1));
        vxw_log(str);
    }
    #endif

    error = "!!! BDM boot diagnostic override failed\n\r";

    // RLWINM R3,R3,0,20,18
    ret = bdm_instruction(0x54630524);
    if (!ret)
        goto fail;

    // Check the execution status of RLWINM by shifting in a NOP
    ret = bdm_instruction(0x60000000);
    if (!ret)
        goto fail;

    vxw_log(">>> PB[19] successfully cleared!\n\r");
    return true;

fail:
    vxw_log(error);
    return false;
}

// Called while halted after the PB19 override; leaves the CPU halted again.
static bool override_boot_card_id(void)
{
    uint32_t value;

    // First check that a card is detected: CD on the 100-pin connector must
    // pulled down. This check should never fail if the board has been soldered
    // correctly.
    // CD goes to PC[5] of the MPC860 and can be read from the PCDAT register,
    // so set a breakpoint on that.
    const uint32_t PCDAT    = 0xff000966;

    const char *error = "!!! BDM CD read watchpoint setup failed\n\r";
    int ret = bdm_set_watchpoint(PCDAT);
    if (!ret)
        goto fail;

    error = "!!! BDM CD read watchpoing resume failed\n\r";
    ret = bdm_request_resume();
    if (!ret)
        goto fail;

    error = "!!! BDM CD read watchpoint wait failed\n\r";
    ret = bdm_wait_for_watchpoint(PCDAT, 1000);
    if (!ret)
        goto fail;

    error = "!!! BDM CD read watchpoint removal failed\n\r";
    ret = bdm_clear_watchpoint();
    if (!ret)
        goto fail;

    error = "!!! BDM CD read R3 read failed\n\r";
    ret = bdm_read_gpr(3, &value);
    if (!ret)
        goto fail;

    error = "!!! No card present: skip CS7 read\n\r";
    // The instruction to read the PCDAT register is LHZ R3,0x0966(R4).
    // LHZ is a 16-bit read. PC[5] for a big-endian PowerPC is bit 11 for the RP2350 -> 0x0400
    // The scope will assume a card ID value of 0xFF and report "unsupported firmware."
    if (value & 0x0400) {
        // CD is high -> no pullup on the card
        goto fail;
    }

    // CS7 of the MPC860 is routed to CS1 (or is it CS2?) of the expansion bus,
    // and mapped to address 0x06000000.
    // When asserted, the expansion board is supposed to return an 8-bit card ID number.
    // This *could* have been implemented on the RP2350, but why bother?
    // Just like overriding the value of PB[19] for enabling the diagnostics, put
    // a breakpoint on the read from address 0x0600000 and change the value to 
    // 0x00, which maps to TDS3VM: an extension card with VGA and RS232 only.

    error = "!!! BDM CS7 watchpoint setup failed\n\r";
    const uint32_t CS7_ADDR  = 0x06000000;
    ret = bdm_set_watchpoint(CS7_ADDR);
    if (!ret)
        goto fail;

    error = "!!! BDM resume to CS7 read failed\n\r";
    ret = bdm_request_resume();
    if (!ret)
        goto fail;

    error = "!!! BDM CS7 watchpoint wait failed\n\r";
    ret = bdm_wait_for_watchpoint(CS7_ADDR, 1000);
    if (!ret)
        goto fail;

    error = "!!! BDM CS7 watchpoint removal failed\n\r";
    ret = bdm_clear_watchpoint();
    if (!ret)
        goto fail;

    #if DEBUG_CS7 == 1
    {
        // There is no need to read card ID from register R2: we will
        // force a different value (0x00) anyway and the RP2350 didn't
        // returned a defined value because database GPIOs are still set to
        // input. But it's interesting to see what gets returned...

        error = "!!! BDM card ID R2 read failed\n\r";
        ret = bdm_read_gpr(2, &value);
        if (!ret)
            goto fail;
    
        char str[64];
        snprintf(str, sizeof(str), ">>> CS7 card ID read: 0x%02x\n\r", (value & 0xff));
        vxw_log(str);
    }
    #endif

    // LI R2,0
    error = "!!! BDM card ID override to 0x00 failed\n\r";
    ret = bdm_instruction(0x38400000);
    if (!ret)
        goto fail;

    // Check the execution status of LI by shifting in a NOP.
    ret = bdm_instruction(0x60000000);
    if (!ret)
        goto fail;

    vxw_log(">>> Card ID overridden to 0x00 (TDS3VM).\n\r");
    return true;

fail:
    vxw_log(error);
    return false;
}

// Dump value of BRGCx registers
// See section 20.4
void uart_brgc_dump(int uart_nr)
{
    vxw_log("Halting CPU...\n\r");
    int ret = bdm_halt_cpu();
    if (ret == false){
        vxw_log("bdm_halt_cpu() failed\n\r");
        return;
    }

    vxw_log("Resuming CPU...\n\r");
    ret = bdm_resume_cpu();
    if (ret == false){
        vxw_log("bdm_resume_cpu() failed\n\r");
        return;
    }
}


int boot_setup()
{
    int ret = enable_boot_diagnostics();
    if (!ret)
        return false;

    ret = override_boot_card_id();
    if (!ret)
        return false;

    ret = bdm_resume_cpu();
    if (!ret)
        vxw_log("!!! BDM resume failed\n\r");
    return ret;
}

int main(void)
{
    tusb_init(0);
    scpi_uart_init();
    vxw_uart_init();
    bdm_init();
    rtc_init();

    boot_setup();

    while(true){
        tud_task();

        // VXW commands
        if (tud_cdc_n_available(VXW_USB_ID)){
            uint8_t c = tud_cdc_n_read_char(VXW_USB_ID);
            vxw_log("\n\r");
            switch(c){
                case 'r': {
                    boot_setup();
                    break;
                }
                case 't': {
                    bool time_valid;
                    struct tm rtc_time;
                    rtc_test(&time_valid, &rtc_time);

                    char buf[64];
                    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S\n\r", &rtc_time);
                    vxw_log(buf);
                    break;
                }
                case 'u': {
                    uart_brgc_dump(0);
                    //uart_brgc_dump(1);
                    break;
                }
            }
        }

        vxw_uart_process();
        scpi_uart_process();
    }
}

