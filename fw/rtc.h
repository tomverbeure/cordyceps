#ifndef RTC_H
#define RTC_H

#include "pico/aon_timer.h"

#define PCF8563_CONTROL_STATUS_1        0x00
#define PCF8563_CONTROL_STATUS_2        0x01
#define PCF8563_VL_SECONDS              0x02
#define PCF8563_MINUTES                 0x03
#define PCF8563_HOURS                   0x04
#define PCF8563_DAYS                    0x05
#define PCF8563_WEEKDAYS                0x06
#define PCF8563_CENTURY_MONTHS          0x07
#define PCF8563_YEARS                   0x08
#define PCF8563_MINUTE_ALARM            0x09
#define PCF8563_HOUR_ALARM              0x0a
#define PCF8563_DAY_ALARM               0x0b
#define PCF8563_WEEKDAY_ALARM           0x0c
#define PCF8563_CLKOUT_CONTROL          0x0d
#define PCF8563_TIMER_CONTROL           0x0e
#define PCF8563_TIMER                   0x0f

int rtc_init();
int rtc_write_registers(uint8_t reg, const uint8_t *wr_data, int len);
int rtc_write_register(uint8_t reg, const uint8_t wr_data);
int rtc_read_registers(uint8_t reg, uint8_t *rd_data, size_t len);
int rtc_test(bool *time_valid, struct tm *t);

#endif
