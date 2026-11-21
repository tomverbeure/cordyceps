#ifndef VXW_UART_H
#define VXW_UART_H

#define VXW_UART_ID             uart1
#define VXW_UART_BAUD_RATE      38400
#define VXW_UART_TX_PIN         VXW_RXD
#define VXW_UART_RX_PIN         VXW_TXD

#define VXW_UART_RX_BUF_SIZE    16384
#define VXW_UART_TX_BUF_SIZE    256

#define VXW_USB_ID              1

void vxw_uart_init(void);
void vxw_uart_process(void);

void vxw_log(const char *message);

#endif
