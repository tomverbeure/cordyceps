#ifndef CORDYCEPS_PINS_H
#define CORDYCEPS_PINS_H

//============================================================
// MPC860 CPU bus
//============================================================
// These should have been defined as D7 to D0.
// D15 is the LSB!!!
#define D15             0
#define D14             1
#define D13             2
#define D12             3
#define D11             4
#define D10             5
#define D9              6
#define D8              7

// A19 is the LSB!!!
#define A19             8
#define A18             9
#define A17            10
#define A16            11
#define A15            12
#define A14            13
#define A13            14
#define A12            15
#define A11            16
#define A10            17
#define A9             18

#define RnW            20
#define nCE2           21
#define nCE1           22
#define nTA            23
#define nOE            24
#define nWE            25

// Enable the bidir multiplexers that select between the BDM and SDcard interface
#define nBDM_SD_ENA    19
// Select SDcard instead of BDM on multiplexer
#define SD_SEL         46

// Serial Ethernet MAC interface
#define SNI_RCLK       26
#define SNI_TCLK       27
#define SNI_RENA       28
#define SNI_CLSN       29
#define SNI_RX         30
#define SNI_TENA       31
#define SNI_TX         32

// BDM signals - also used for SDcard (except HRESET)
#define BDM_HRESET     39
#define BDM_FRZ        33
#define BDM_DSCK       36
#define BDM_DSDI       37
#define BDM_DSDO       38

// VxWorks console UART - always 38400 baud - mapped to RP2350B UART1
#define VXW_RXD        40       // Connected to Pico UART TXD
#define VXW_TXD        41       // Connected to Pico UART RXD

// SCPI UART - mapped to RP2350B UART1 - mapped to RP2350 UART0
#define RXD            44       // Connected to Pico UART TXD
#define TXD            45       // Connected to Pico UART RXD
#define RTS            34       // Connected to Pico UART CTS
#define CTS            35       // Connected to Pico UART RTS

// PCF8563T Real-Time Clock I2C
#define RTC_SDA        42
#define RTC_SCL        43

#endif
