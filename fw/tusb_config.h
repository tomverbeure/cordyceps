#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

// Also valid for RP2350
#define CFG_TUSB_MCU                OPT_MCU_RP2040      

// Instead of OPT_OS_NONE: enable a bunch of pico specific features:
// - multi-core management
// - event queuing
// - wait-for-event sleep
// ...
#define CFG_TUSB_OS                 OPT_OS_PICO         

// RHPORT0 -> Root Hub Port 0
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)


#endif 

