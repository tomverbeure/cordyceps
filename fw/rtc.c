#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "rtc.h"
#include "pins.h"

#define RTC_I2C_PORT        i2c1
#define RTC_I2C_SDA_PIN     RTC_SDA
#define RTC_I2C_SCL_PIN     RTC_SCL
#define RTC_I2C_BAUDRATE    100000

#define RTC_I2C_TIMEOUT_US  5000

#define PCF8563_I2C_ADDR    (0xA2 >> 1)
#define PCF8563_REG_COUNT   16

int rtc_init()
{
    i2c_init(RTC_I2C_PORT, RTC_I2C_BAUDRATE);

    // There are pullups on the board as well, but this doesn't hurt.
    gpio_pull_up(RTC_I2C_SDA_PIN);
    gpio_pull_up(RTC_I2C_SCL_PIN);

    gpio_set_function(RTC_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(RTC_I2C_SCL_PIN, GPIO_FUNC_I2C);

    rtc_write_register(PCF8563_CONTROL_STATUS_1, 0);
    // We don't use the CLKOUT pin
    rtc_write_register(PCF8563_CLKOUT_CONTROL, 0);
}

int rtc_write_registers(uint8_t reg, const uint8_t *wr_data, int len)
{
    uint8_t buf[1+PCF8563_REG_COUNT];

    buf[0]  = reg;
    memcpy(buf+1, wr_data, len);

    int ret = i2c_write_timeout_us(RTC_I2C_PORT, PCF8563_I2C_ADDR, buf, len+1, false, RTC_I2C_TIMEOUT_US);
    return ret;
}

int rtc_write_register(uint8_t reg, const uint8_t wr_data)
{
    return rtc_write_registers(reg, &wr_data, 1);
}

int rtc_read_registers(uint8_t reg, uint8_t *rd_data, size_t len)
{
    int ret = i2c_write_timeout_us(RTC_I2C_PORT, PCF8563_I2C_ADDR, &reg, 1, true, RTC_I2C_TIMEOUT_US);
    ret = i2c_read_timeout_us(RTC_I2C_PORT, PCF8563_I2C_ADDR, rd_data, len, false, RTC_I2C_TIMEOUT_US);
    return ret;
}

int rtc_test(bool *time_valid, struct tm *t)
{
    uint8_t buf[16];

    int ret = rtc_read_registers(PCF8563_VL_SECONDS, buf+PCF8563_VL_SECONDS, 7);

    *time_valid = (buf[PCF8563_VL_SECONDS] >> 7) & 1;

    t->tm_year  = (((buf[PCF8563_YEARS]          >> 4) & 15) * 10 + (buf[PCF8563_YEARS]          & 15)) + 100;  // Start at 1900
    t->tm_mon   = (((buf[PCF8563_CENTURY_MONTHS] >> 4) &  1) * 10 + (buf[PCF8563_CENTURY_MONTHS] & 15)) - 1;    // 0 - 11
    t->tm_mday  =  ((buf[PCF8563_DAYS]           >> 4) &  3) * 10 + (buf[PCF8563_DAYS]           & 15);
    t->tm_hour  =  ((buf[PCF8563_HOURS]          >> 4) &  3) * 10 + (buf[PCF8563_HOURS]          & 15);
    t->tm_min   =  ((buf[PCF8563_MINUTES]        >> 4) &  7) * 10 + (buf[PCF8563_MINUTES]        & 15);
    t->tm_sec   =  ((buf[PCF8563_VL_SECONDS]     >> 4) &  7) * 10 + (buf[PCF8563_VL_SECONDS]     & 15);
    t->tm_isdst = -1;

    return ret;
}

