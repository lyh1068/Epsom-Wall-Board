#include "rx8130.h"

#include <string.h>

/*
 * Register map and init sequence follow M5Stack's own driver for this board
 * (M5Unified, utility/rtc/RX8130_Class.inl).
 */
#define RX8130_ADDR      0x32
#define RX8130_FREQ_HZ   400000
#define RX8130_TIMEOUT   100     /* ms */

#define REG_SEC          0x10    /* SEC MIN HOUR WEEK DAY MONTH YEAR, BCD; WEEK is one-hot */
#define REG_FLAG         0x1D    /* write-0-to-clear; bit 6 must be written 0 */
#define REG_CTRL0        0x1E
#define REG_CTRL1        0x1F
#define REG_OFFSET       0x30

#define FLAG_VLF         0x02    /* oscillator stopped / supply lost: time is invalid */
#define FLAG_CLEAR_VLF   0xBD    /* clears VLF only */

static i2c_master_dev_handle_t s_dev;

static esp_err_t write_regs(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[8];
    if (len + 1 > sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    return i2c_master_transmit(s_dev, buf, len + 1, RX8130_TIMEOUT);
}

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    return write_regs(reg, &val, 1);
}

static esp_err_t read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, RX8130_TIMEOUT);
}

static bool bcd_ok(uint8_t v)
{
    return (v & 0x0F) <= 9 && (v >> 4) <= 9;
}

static int from_bcd(uint8_t v)
{
    return (v >> 4) * 10 + (v & 0x0F);
}

static uint8_t to_bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

esp_err_t rx8130_init(i2c_master_bus_handle_t bus)
{
    if (s_dev != NULL) {
        return ESP_OK;
    }
    if (i2c_master_probe(bus, RX8130_ADDR, RX8130_TIMEOUT) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RX8130_ADDR,
        .scl_speed_hz = RX8130_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    /* M5Stack's init: backup charge + init enable, zero the digital offset,
     * and clear CTRL0 (which also clears STOP, so the clock runs). */
    uint8_t ctrl1 = 0;
    err = read_regs(REG_CTRL1, &ctrl1, 1);
    if (err == ESP_OK) {
        err = write_reg(REG_CTRL1, ctrl1 | 0x30);
    }
    if (err == ESP_OK) {
        err = write_reg(REG_OFFSET, 0x00);
    }
    if (err == ESP_OK) {
        err = write_reg(REG_CTRL0, 0x00);
    }
    return err;
}

esp_err_t rx8130_read_utc(struct tm *out, bool *valid)
{
    *valid = false;
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t flag = 0;
    uint8_t r[7] = { 0 };
    esp_err_t err = read_regs(REG_FLAG, &flag, 1);
    if (err == ESP_OK) {
        err = read_regs(REG_SEC, r, sizeof(r));
    }
    if (err != ESP_OK) {
        return err;
    }

    uint8_t sec = r[0] & 0x7F, min = r[1] & 0x7F, hour = r[2] & 0x3F;
    uint8_t wday = r[3], day = r[4] & 0x3F, mon = r[5] & 0x1F, year = r[6];
    if (!bcd_ok(sec) || !bcd_ok(min) || !bcd_ok(hour) || !bcd_ok(day) || !bcd_ok(mon) || !bcd_ok(year)
        || wday == 0 || (wday & (wday - 1)) != 0) {
        return ESP_OK;
    }

    memset(out, 0, sizeof(*out));
    out->tm_sec = from_bcd(sec);
    out->tm_min = from_bcd(min);
    out->tm_hour = from_bcd(hour);
    out->tm_wday = __builtin_ctz(wday);
    out->tm_mday = from_bcd(day);
    out->tm_mon = from_bcd(mon) - 1;
    out->tm_year = from_bcd(year) + 100;

    /* Anything before this project existed means the RTC was never set. */
    *valid = !(flag & FLAG_VLF)
             && out->tm_sec < 60 && out->tm_min < 60 && out->tm_hour < 24
             && out->tm_mday >= 1 && out->tm_mday <= 31
             && out->tm_mon >= 0 && out->tm_mon < 12
             && out->tm_year >= 125;
    return ESP_OK;
}

esp_err_t rx8130_write_utc(const struct tm *utc)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t r[7] = {
        to_bcd(utc->tm_sec),
        to_bcd(utc->tm_min),
        to_bcd(utc->tm_hour),
        (uint8_t)(1u << (utc->tm_wday & 7)),
        to_bcd(utc->tm_mday),
        to_bcd(utc->tm_mon + 1),
        to_bcd(utc->tm_year % 100),
    };
    esp_err_t err = write_regs(REG_SEC, r, sizeof(r));
    if (err == ESP_OK) {
        err = write_reg(REG_FLAG, FLAG_CLEAR_VLF);
    }
    return err;
}
