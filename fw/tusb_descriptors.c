#include <tusb.h>
#include "pico/unique_id.h"
#include "pico/usb_reset.h"

enum {
    STR_IDX_LANG_ID         = 0,
    STR_IDX_MANUFACTURER    = 1,
    STR_IDX_PRODUCT         = 2,
    STR_IDX_SERIAL          = 3,
    STR_IDX_CDC_0_ITF       = 4,
    STR_IDX_CDC_1_ITF       = 5,
    STR_IDX_CDC_2_ITF       = 6,
    STR_IDX_VENDOR_RESET    = 7
};

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0210, // Advertise BOS / Microsoft OS 2.0 descriptors
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    // TinyUSB example values
    .idVendor           = 0xcafe,
    .idProduct          = 0x4032,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 1
};

uint8_t const *tud_descriptor_device_cb (void)
{
    return (uint8_t const *)&desc_device;
}

// Interface and endpoint numbering 
enum {
    ITF_NUM_CDC_0           = 0,
    ITF_NUM_CDC_0_DATA      = 1,
    ITF_NUM_CDC_1           = 2,
    ITF_NUM_CDC_1_DATA      = 3,
    ITF_NUM_CDC_2           = 4,
    ITF_NUM_CDC_3_DATA      = 5,
    ITF_NUM_VENDOR_RESET    = 6,
    ITF_NUM_TOTAL
};

#define EPNUM_CDC_0_NOTIF   0x81
#define EPNUM_CDC_0_OUT     0x02
#define EPNUM_CDC_0_IN      0x82

#define EPNUM_CDC_1_NOTIF   0x83
#define EPNUM_CDC_1_OUT     0x04
#define EPNUM_CDC_1_IN      0x84

#define EPNUM_CDC_2_NOTIF   0x85
#define EPNUM_CDC_2_OUT     0x06
#define EPNUM_CDC_2_IN      0x86

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + 3 * TUD_CDC_DESC_LEN + TUD_RPI_RESET_DESC_LEN)

_Static_assert(ITF_NUM_VENDOR_RESET == PICO_USB_RESET_MS_OS_20_DESCRIPTOR_ITF,
               "Reset interface must match the Microsoft OS descriptor");

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // CDC Port 0 (e.g., /dev/ttyACM0 or first COM port)
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_0, STR_IDX_CDC_0_ITF, EPNUM_CDC_0_NOTIF, 8, EPNUM_CDC_0_OUT, EPNUM_CDC_0_IN, 64),

    // CDC Port 1 (e.g., /dev/ttyACM1 or second COM port)
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_1, STR_IDX_CDC_1_ITF, EPNUM_CDC_1_NOTIF, 8, EPNUM_CDC_1_OUT, EPNUM_CDC_1_IN, 64),

    // CDC Port 2 (e.g., /dev/ttyACM2 or third COM port)
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_2, STR_IDX_CDC_2_ITF, EPNUM_CDC_2_NOTIF, 8, EPNUM_CDC_2_OUT, EPNUM_CDC_2_IN, 64),

    TUD_RPI_RESET_DESCRIPTOR(ITF_NUM_VENDOR_RESET, STR_IDX_VENDOR_RESET),
};

_Static_assert(sizeof(desc_configuration) == CONFIG_TOTAL_LEN,
               "USB configuration descriptor length mismatch");

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

// String Descriptors
static char usbd_serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

char const *string_desc_arr[] = {
    [STR_IDX_LANG_ID]       = (const char[]){0x09, 0x04}, // English (0x0409)
    [STR_IDX_MANUFACTURER]  = "Raspberry Pi",
    [STR_IDX_PRODUCT]       = "Cordyceps",
    [STR_IDX_SERIAL]        = usbd_serial_str,
    [STR_IDX_CDC_0_ITF]     = "SCPI serial port",
    [STR_IDX_CDC_1_ITF]     = "VXW serial port",
    [STR_IDX_CDC_2_ITF]     = "GDB serial port",
    [STR_IDX_VENDOR_RESET]  = "Reset",
};

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) 
{
    // Only English strings supported.
    (void)langid;

    uint8_t chr_count;

    // Use the unique board ID as serial number
    if (index == STR_IDX_SERIAL && usbd_serial_str[0] != '\0') {
        pico_get_unique_board_id_string(usbd_serial_str, sizeof(usbd_serial_str));
    }

    if (index == STR_IDX_LANG_ID) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } 
    else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) 
            return NULL;

        const char *str = string_desc_arr[index];
        chr_count = strlen(str);
        if (chr_count > 31) 
            chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
    return _desc_str;
}


