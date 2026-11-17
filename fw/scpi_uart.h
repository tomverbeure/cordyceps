#ifndef SCPI_UART_H
#define SCPI_UART_H

#define SCPI_UART_ID            uart0
#define SCPI_UART_BAUD_RATE     9600
#define SCPI_UART_TX_PIN        RXD
#define SCPI_UART_RX_PIN        TXD

#define SCPI_UART_RX_BUF_SIZE   2048
#define SCPI_UART_TX_BUF_SIZE   256

#define SCPI_USB_ID             0

void scpi_uart_init();
void scpi_uart_process();

#endif
