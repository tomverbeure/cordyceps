#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "pins.h"

#define VXW_UART_ID         uart1
#define VXW_UART_BAUD_RATE  38400
#define VXW_UART_TX_PIN     VXW_RXD
#define VXW_UART_RX_PIN     VXW_TXD

#define SCPI_USB_ID         0
#define VXW_USB_ID          1

void vxw_uart_init()
{
    uart_init(VXW_UART_ID, VXW_UART_BAUD_RATE);
    gpio_set_function(VXW_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(VXW_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(VXW_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(VXW_UART_ID, true);
}

void bdm_init()
{
    gpio_init(BDM_HRESET);
    gpio_put(BDM_HRESET, 0);
    gpio_set_dir(BDM_HRESET, GPIO_OUT);
}

// Pulsing HRESET resets the scope
void bdm_pulse_hreset()
{
    gpio_put(BDM_HRESET, 1);
    sleep_ms(20);
    gpio_put(BDM_HRESET, 0);
}

int main(void)
{
    tusb_init(0);
    vxw_uart_init();
    bdm_init();

    int secs = 0;

    printf("VxWorks terminal active\n");

    // An endless loop is needed otherwise USB reset won't be serviced.
    while(true){
        tud_task();

        // VXW commands
        if (tud_cdc_n_available(VXW_USB_ID)){
            uint8_t c = tud_cdc_n_read_char(VXW_USB_ID);
            switch(c){
                case 'r': {
                    bdm_pulse_hreset();
                    break;
                }
            }
        }

        // VXW UART to USB
        int pending = 0;
        while(uart_is_readable(VXW_UART_ID) && tud_cdc_n_write_available(VXW_USB_ID)){
            uint8_t ch = uart_getc(VXW_UART_ID);
            tud_cdc_n_write_char(VXW_USB_ID, ch);
            ++pending;
        }
        if (pending){
            tud_cdc_n_write_flush(VXW_USB_ID);
        }

        // SCPI USB loopback
        if (tud_cdc_n_available(SCPI_USB_ID)){
            uint8_t ch = tud_cdc_n_read_char(SCPI_USB_ID);
            tud_cdc_n_write_char(SCPI_USB_ID, ch);
            tud_cdc_n_write_flush(SCPI_USB_ID);
        }
    }
}

