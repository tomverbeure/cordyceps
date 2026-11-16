#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "bdm.h"
#include "rtc.h"
#include "pins.h"

#define SCPI_USB_ID         0
#define VXW_USB_ID          1

#define SCPI_UART_ID        uart0
#define SCPI_UART_BAUD_RATE 9600
#define SCPI_UART_TX_PIN    RXD
#define SCPI_UART_RX_PIN    TXD

#define VXW_UART_ID         uart1
#define VXW_UART_BAUD_RATE  38400
#define VXW_UART_TX_PIN     VXW_RXD
#define VXW_UART_RX_PIN     VXW_TXD


void scpi_uart_init()
{
    uart_init(SCPI_UART_ID, SCPI_UART_BAUD_RATE);
    gpio_set_function(SCPI_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(SCPI_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(SCPI_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(SCPI_UART_ID, true);
}

void vxw_uart_init()
{
    uart_init(VXW_UART_ID, VXW_UART_BAUD_RATE);
    gpio_set_function(VXW_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(VXW_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(VXW_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(VXW_UART_ID, true);
}

int main(void)
{
    tusb_init(0);
    scpi_uart_init();
    vxw_uart_init();
    bdm_init();
    rtc_init();


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
                case 'h': {
                    const char str[] = "VxWorks terminal active\n";
                    tud_cdc_n_write(VXW_USB_ID, str, strlen(str));
                    tud_cdc_n_write_flush(VXW_USB_ID);
                    break;
                }
                case 't': {
                    bool time_valid;
                    struct tm rtc_time;
                    rtc_test(&time_valid, &rtc_time);

                    char buf[64];
                    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &rtc_time);
                    tud_cdc_n_write(VXW_USB_ID, buf, strlen(buf));
                    tud_cdc_n_write_char(VXW_USB_ID, '\n');
                    tud_cdc_n_write_flush(VXW_USB_ID);
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

        // SCPI UART <> USB
        if (tud_cdc_n_available(SCPI_USB_ID) && uart_is_writable(SCPI_UART_ID)){
            uint8_t ch = tud_cdc_n_read_char(SCPI_USB_ID);
            uart_putc(SCPI_UART_ID, ch);
        }

        pending = 0;
        if (uart_is_readable(SCPI_UART_ID) && tud_cdc_n_write_available(SCPI_USB_ID)){
            uint8_t ch = uart_getc(SCPI_UART_ID);
            tud_cdc_n_write_char(SCPI_USB_ID, ch);
            ++pending;
        }
        if (pending){
            tud_cdc_n_write_flush(SCPI_USB_ID);
        }
    }
}

