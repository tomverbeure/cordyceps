#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/platform.h"
#include "tusb.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

#include "scpi_uart.h"
#include "ring_buf.h"
#include "pins.h"

uint8_t scpi_rx_buf[SCPI_UART_RX_BUF_SIZE];
uint8_t scpi_tx_buf[SCPI_UART_TX_BUF_SIZE];

struct ring_buf_t scpi_rx_ring_buf;
struct ring_buf_t scpi_tx_ring_buf;

void scpi_uart_on_uart_irq()
{
    while(uart_is_readable(SCPI_UART_ID)){
        uint8_t c = uart_getc(SCPI_UART_ID);
        ring_buf_wr(&scpi_rx_ring_buf, c);
    }

    while(uart_is_writable(SCPI_UART_ID) && ring_buf_available(&scpi_tx_ring_buf)){
        uint8_t c;
        ring_buf_rd(&scpi_tx_ring_buf, &c);
        uart_putc(SCPI_UART_ID, c);

        if (!ring_buf_available(&scpi_tx_ring_buf)){
            uart_set_irq_enables(SCPI_UART_ID, true, false);
        }
    }
}

void scpi_uart_init()
{
    ring_buf_init(&scpi_rx_ring_buf, scpi_rx_buf, SCPI_UART_RX_BUF_SIZE);
    ring_buf_init(&scpi_tx_ring_buf, scpi_tx_buf, SCPI_UART_TX_BUF_SIZE);

    uart_init(SCPI_UART_ID, SCPI_UART_BAUD_RATE);
    gpio_set_function(SCPI_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(SCPI_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(SCPI_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(SCPI_UART_ID, true);

    int scpi_uart_irq = (SCPI_UART_ID == uart0) ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(scpi_uart_irq, scpi_uart_on_uart_irq);
    irq_set_enabled(scpi_uart_irq, true);

    // Don't set TX interrupt initially. It only gets set when there's data available
    // to transmit.
    uart_set_irq_enables(SCPI_UART_ID, true, false);
}

void scpi_uart_process()
{
    uint8_t buf[64];

    // From USB to UART
    int usb_rx_count    = tud_cdc_n_available(SCPI_USB_ID);
    if (usb_rx_count > 0){
        int uart_tx_count       = ring_buf_free_space(&scpi_tx_ring_buf);
        int usb2uart_xfer_count = MIN(MIN(usb_rx_count, uart_tx_count),sizeof(buf));

        int read_bytes = tud_cdc_n_read(SCPI_USB_ID, buf, usb2uart_xfer_count);
        ring_buf_wr_n(&scpi_tx_ring_buf, buf, read_bytes);

        if (read_bytes > 0){
            uart_set_irq_enables(SCPI_UART_ID, true, true);
            int scpi_uart_irq = (SCPI_UART_ID == uart0) ? UART0_IRQ : UART1_IRQ;
            irq_set_pending(scpi_uart_irq);
        }
    }

    // From UART to USB
    int uart_rx_count       = ring_buf_available(&scpi_rx_ring_buf);
    int usb_tx_count        = tud_cdc_n_write_available(SCPI_USB_ID);
    int uart2usb_xfer_count = MIN(MIN(uart_rx_count, usb_tx_count),sizeof(buf));

    if (uart2usb_xfer_count > 0){
        ring_buf_rd_n(&scpi_rx_ring_buf, buf, uart2usb_xfer_count);
        int written_bytes = tud_cdc_n_write(SCPI_USB_ID, buf, uart2usb_xfer_count);

        if  (written_bytes > 0)
            tud_cdc_n_write_flush(SCPI_USB_ID);
    }
}


