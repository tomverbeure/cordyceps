#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

// Also valid for RP2350
#define CFG_TUSB_MCU                OPT_MCU_RP2040      

// Instead of OPT_OS_NONE.
// Enable a bunch of pico specific features:
// - multi-core management
// - event queuing
// - wait-for-event sleep
// ...
#define CFG_TUSB_OS                 OPT_OS_PICO         

// RHPORT0 -> Root Hub Port 0
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

// Number of active CDC ports
#define CFG_TUD_CDC                 1
// TX and RX buffer sizes per port
#define CFG_TUD_CDC_RX_BUFSIZE      256
#define CFG_TUD_CDC_TX_BUFSIZE      256
// Endpoint buffer size. One for RX and one for TX, per port
#define CFG_TUD_CDC_EP_BUFSIZE      64

#endif 

