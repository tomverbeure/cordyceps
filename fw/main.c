#include <stdio.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "bdm.h"
#include "pins.h"
#include "rtc.h"
#include "scpi_uart.h"
#include "vxw_uart.h"

#define DEBUG_PB19  1

// FIXME: this code block handling of USB transfers.
// Convert to an FSM to allow cooperative multi-tasking?
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

    error = "!!! BDM resume failed\n\r";
    ret = bdm_resume_cpu();
    if (!ret)
        goto fail;

    vxw_log(">>> PB[19] successfully cleared!\n\r");
    return true;

fail:
    vxw_log(error);
    return false;
}

int boot_setup()
{
    int ret = enable_boot_diagnostics();
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
                    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S\n", &rtc_time);
                    vxw_log(buf);
                    break;
                }
            }
        }

        vxw_uart_process();
        scpi_uart_process();
    }
}

