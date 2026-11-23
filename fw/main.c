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

        unsigned pb19 = (r3 >> 12) & 1;
    
        // PB19 uses PowerPC's MSB-first numbering: its mask is 0x00001000.
        char str[256];
        snprintf(str, sizeof(str), ">>> R3=0x%08x, PB[19]=%u\n\r", r3, pb19);
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
    
        char str[256];
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

// SCC UART
void uart_brgc_dump(int uart_nr)
{
    vxw_log("Halting CPU...\n\r");
    int ret = bdm_halt_cpu();
    if (ret == false){
        vxw_log("bdm_halt_cpu() failed\n\r");
        return;
    }

    // SCC register banks have a 0x20-byte stride.
    const uint32_t IMMR     = 0xff000000;
    const uint32_t GSMR_L   = IMMR + 0x0a00 + 32 * uart_nr;
    const uint32_t GSMR_H   = GSMR_L + 4;
    const uint32_t SICR     = IMMR + 0x0aec;

    uint32_t gsmr_l;
    bdm_read_word(GSMR_L, &gsmr_l);

    uint32_t gsmr_h;
    bdm_read_word(GSMR_H, &gsmr_h);

    uint32_t sicr;
    bdm_read_word(SICR, &sicr);

    unsigned route      = (sicr >> (8 * uart_nr)) & 0xff;
    unsigned tcs        = route & 7;
    unsigned rcs        = (route >> 3) & 7;
    unsigned tdm        = (route >> 6) & 1;

    uint32_t brgc = 0;
    const uint32_t BRGC = IMMR + 0x09f0 + 4 * tcs;
    bdm_read_word(BRGC, &brgc);

    // Capture all values while halted, then resume before formatting output.
    vxw_log("Resuming CPU...\n\r");
    ret = bdm_resume_cpu();
    if (ret == false){
        vxw_log("bdm_resume_cpu() failed\n\r");
        return;
    }

    unsigned mode       = gsmr_l & 0xf;
    unsigned ent        = (gsmr_l >> 4) & 1;
    unsigned enr        = (gsmr_l >> 5) & 1;
    unsigned tdcr       = (gsmr_l >> 16) & 3;
    unsigned rdcr       = (gsmr_l >> 14) & 3;
    unsigned tdcr_div[] = {1, 8, 16, 32};

    char str[256];

    snprintf(str, sizeof(str), "SCC%u GSMR_L=0x%08x GSMR_H=0x%08x\n\r",
             uart_nr + 1, gsmr_l, gsmr_h);
    vxw_log(str);
    snprintf(str, sizeof(str), "  MODE=%u ENR=%u ENT=%u TDCR=%u (%d) RDCR=%u (%d)\n\r",
             mode, enr, ent, tdcr, tdcr_div[tdcr], rdcr, tdcr_div[rdcr]);
    vxw_log(str);
    snprintf(str, sizeof(str), "SICR=0x%08x SC%u=%u RCS=%u TCS=%u\n\r",
             sicr, uart_nr + 1, tdm, rcs, tcs);
    vxw_log(str);

    unsigned rst        = (brgc >> 17) & 1;
    unsigned en         = (brgc >> 16) & 1;
    unsigned extc       = (brgc >> 14) & 3;
    unsigned atb        = (brgc >> 13) & 1;
    unsigned cd         = (brgc >> 1) & 0xfff;
    unsigned div16      = brgc & 1;
    unsigned brg_div    = (cd + 1) * (div16 ? 16 : 1);

    snprintf(str, sizeof(str), "BRGC%u=0x%08x\n\r", tcs + 1, brgc);
    vxw_log(str);
    snprintf(str, sizeof(str), "  RST=%u EN=%u EXTC=%u ATB=%u CD=%u DIV16=%u; BRG divider=%u\n\r",
             rst, en, extc, atb, cd, div16, brg_div);
    vxw_log(str);

    unsigned tx_baud = 25000000 / (brg_div * tdcr_div[tdcr]);
    snprintf(str, sizeof(str), "Baud rate: %u\n\r", tx_baud);
    vxw_log(str);
}

bool patch_sw_options(bool enable_vid, bool enable_tmt, bool enable_lim, bool enable_aam)
{
    if (!enable_vid && !enable_tmt && !enable_lim && !enable_aam){
        vxw_log(">>> Don't enable any software functions.\n\r");
        return true;
    }

    const struct {
        const char  *name;
        bool        enable;
        uint32_t    address;        // Start of function where instructions must be patched
    } options[] = {
        { "TDS3VID", enable_vid, 0x001c48b0 }, // softwareVideoOptionAvailable
        { "TDS3TMT", enable_tmt, 0x001c494c }, // softwareMaskTestOptionAvailable
        { "TDS3LIM", enable_lim, 0x001c49d8 }, // softwareLimitTestOptionAvailable
        { "TDS3AAM", enable_aam, 0x001c4a64 }, // softwareAamOptionAvailable
    };

    const char * error = "!!! BDM software-option instruction write failed\n\r";
    for (unsigned i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
        char str[256];

        if (!options[i].enable){
            snprintf(str, sizeof(str), ">>> %s not enabled.\n\r", options[i].name);
            vxw_log(str);
            continue;
        }

        // LI R3,1
        bool ret = bdm_write_word(options[i].address, 0x38600001);
        if (!ret)
            goto fail;

        // BLR
        ret = bdm_write_word(options[i].address + 4, 0x4e800020);
        if (!ret)
            goto fail;

        snprintf(str, sizeof(str), ">>> %s enabled.\n\r", options[i].name);
        vxw_log(str);
    }

    return true;

fail:
    vxw_log(error);
    return false;
}

int boot_setup()
{
    int ret = enable_boot_diagnostics();
    if (!ret)
        return false;

    ret = override_boot_card_id();
    if (!ret)
        return false;

    // sysInit (0x00006000): stop before starting main application
    const uint32_t APP_ENTRY = 0x00006000;

    uint32_t instruction;

    const char *error = "!!! BDM application breakpoint setup failed\n\r";
    ret = bdm_set_instruction_breakpoint(APP_ENTRY);
    if (!ret)
        goto fail;

    // Loop twice: if LZ4 compression is used, the scope hits 0x6000 two times,
    // only the second time is the main application.
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        vxw_log(">>> Waiting for sysInit at 0x00006000.\n\r");

        error = "!!! BDM resume to application breakpoint failed\n\r";
        ret = bdm_request_resume();
        if (!ret)
            goto fail;

        error = "!!! BDM application breakpoint wait failed\n\r";
        // It takes ~10s to LZW decompress the firmware from flash.
        ret = bdm_wait_for_instruction_breakpoint(APP_ENTRY, 11000000);
        if (!ret)
            goto fail;

        error = "!!! BDM application entry read failed\n\r";
        ret = bdm_read_word(APP_ENTRY, &instruction);
        if (!ret)
            goto fail;

        // The main application start with this...
        if (instruction == 0x7c6000a6) // sysInit: MFMSR R3
            break;

        // The LZ4 secondary loader also enters at 0x6000. Let it unpack the
        // application and catch the second visit using the same breakpoint.
        error = "!!! Unexpected application entry instruction\n\r";
        if (instruction != 0x7c3e0b78 || attempt != 0) // Loader: MR R30,R1
            goto fail;

        vxw_log(">>> LZ4 loader entry; waiting for the unpacked application.\n\r");
    }

    error = "!!! BDM application breakpoint removal failed\n\r";
    ret = bdm_clear_instruction_breakpoint();
    if (!ret)
        goto fail;

    ret = patch_sw_options(true, true, true, true); // VID, TMT, LIM, AAM
    if (!ret)
        return false;

    ret = bdm_resume_cpu();
    if (!ret)
        vxw_log("!!! BDM resume failed\n\r");
    return ret;

fail:
    vxw_log(error);
    return false;
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
                    uart_brgc_dump(1); // SCC2: SCPI/RS-232
                    break;
                }
                case 'p': {
                }
            }
        }

        vxw_uart_process();
        scpi_uart_process();
    }
}

