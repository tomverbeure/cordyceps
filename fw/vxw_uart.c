#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

#include "vxw_uart.h"
#include "ring_buf.h"
#include "pins.h"

uint8_t vxw_rx_buf[VXW_UART_RX_BUF_SIZE];

struct ring_buf_t vxw_rx_ring_buf;

static absolute_time_t  vxw_usb_tx_after_dtr;
static bool             vxw_dtr_asserted;

static void vxw_uart_on_irq(void)
{
    while (uart_is_readable(VXW_UART_ID)) {
        uint8_t c = uart_getc(VXW_UART_ID);
        ring_buf_wr(&vxw_rx_ring_buf, c);
    }

    // Don't send any messages to the VXW UART. The firmware doesn't process it.
}

void vxw_log(const char *message)
{
    size_t length = strlen(message);

    // Block UART IRQ so that a log message is not interrupted.
    uint32_t irq_state = save_and_disable_interrupts();

    for (size_t i = 0; i < length; ++i) {
        ring_buf_wr(&vxw_rx_ring_buf, message[i]);
    }
    restore_interrupts(irq_state);
}

void vxw_uart_init(void)
{
    ring_buf_init(&vxw_rx_ring_buf, vxw_rx_buf, VXW_UART_RX_BUF_SIZE);

    uart_init(VXW_UART_ID, VXW_UART_BAUD_RATE);
    gpio_set_function(VXW_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(VXW_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(VXW_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(VXW_UART_ID, true);

    int vxw_uart_irq = (VXW_UART_ID == uart0) ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(vxw_uart_irq, vxw_uart_on_irq);
    irq_set_enabled(vxw_uart_irq, true);

    uart_set_irq_enables(VXW_UART_ID, true, false);
}


// These 2 TinyUSB callbacks are not called explicity, but are used by
// the linker. They're used to block sending data from VXW to the
// USB port until 1s after DTR has been raised. 
// This is a hack to fix an issue where power-up boot text is flushed 
// by picocom (on the mac) shortly after raising DTR.
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)rts;
    if (itf != VXW_USB_ID)
        return;

    // Allow the terminal to finish configuring the port before sending output.
    if (dtr && !vxw_dtr_asserted){
        // The USB terminal has raised DTR, set a timer of 1s 
        // before actually doing so.
        vxw_usb_tx_after_dtr = make_timeout_time_ms(1000);
    }

    vxw_dtr_asserted = dtr;
}

void tud_umount_cb(void)
{
    vxw_dtr_asserted = false;
}

void vxw_uart_process(void)
{
    if (!tud_cdc_n_connected(VXW_USB_ID)) {
        vxw_dtr_asserted = false;
        return;
    }

    if (!vxw_dtr_asserted)
        return;

    if (!time_reached(vxw_usb_tx_after_dtr))
        return;

    uint8_t buf[64];

    int uart_rx_count       = ring_buf_available(&vxw_rx_ring_buf);
    int usb_tx_count        = tud_cdc_n_write_available(VXW_USB_ID);
    int uart2usb_xfer_count  = MIN(MIN(uart_rx_count, usb_tx_count), (int)sizeof(buf));

    if (uart2usb_xfer_count > 0){
        ring_buf_rd_n(&vxw_rx_ring_buf, buf, uart2usb_xfer_count);
        int written_bytes = tud_cdc_n_write(VXW_USB_ID, buf, uart2usb_xfer_count);

        if (written_bytes > 0)
            tud_cdc_n_write_flush(VXW_USB_ID);
    }
}

