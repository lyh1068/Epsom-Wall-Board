#include "battery.h"

#include <string.h>
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "battery";

/* IO expander 0x44 (BSP expander 1). P0 is WLAN_PWR_EN, already driven by the BSP. */
#define PIN_CHG_EN       IO_EXPANDER_PIN_NUM_7  /* high: charger on */
#define PIN_CHG_STAT     IO_EXPANDER_PIN_NUM_6  /* "high: charging" per M5Unified; not trusted, see battery_read() */
#define PIN_NCHG_QC_EN   IO_EXPANDER_PIN_NUM_5  /* low: fast (QC) charge on */

/* INA226 on the internal bus, 5 mOhm shunt. */
#define INA226_ADDR      0x41
#define INA226_SHUNT     0.005f
#define REG_CONFIG       0x00
#define REG_SHUNT        0x01   /* signed, 2.5 uV/LSB */
#define REG_BUS          0x02   /* 1.25 mV/LSB */
/* 16-sample average, 1.1 ms bus and shunt conversions, continuous: as M5Unified. */
#define INA226_CONFIG    (0x4000 | (0x2 << 9) | (0x4 << 6) | (0x4 << 3) | 0x7)

#define SAMPLE_US        (2 * 1000000)
/* Off the charger this long before a voltage-based level means anything. */
#define SETTLE_US        (60 * 1000000LL)
/* Charging means current into the pack: above ON to start, below OFF to stop. */
#define CHARGING_ON_A    0.05f
#define CHARGING_OFF_A   0.02f

static esp_io_expander_handle_t s_ioe;
static i2c_master_dev_handle_t s_ina;
static battery_t s_last;
static int64_t s_last_us;
static int64_t s_off_since_us;      /* when charging last stopped (0 = boot) */

static esp_err_t ina_write(uint8_t reg, uint16_t v)
{
    uint8_t b[3] = { reg, (uint8_t)(v >> 8), (uint8_t)v };
    return i2c_master_transmit(s_ina, b, sizeof(b), 100);
}

static esp_err_t ina_read(uint8_t reg, uint16_t *v)
{
    uint8_t b[2];
    esp_err_t err = i2c_master_transmit_receive(s_ina, &reg, 1, b, sizeof(b), 100);
    *v = (uint16_t)(b[0] << 8 | b[1]);
    return err;
}

static void note_err(esp_err_t err, const char *what)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err));
        if (s_last.err == ESP_OK) {
            s_last.err = err;
        }
    }
}

void battery_init(void)
{
    /* The BSP's own handle: a second driver for the same chip would keep its
     * own copy of the output register and could switch Wi-Fi power off. */
    s_ioe = bsp_io_expander1_init();
    if (s_ioe == NULL) {
        note_err(ESP_ERR_NOT_FOUND, "IO expander 0x44");
    } else {
        note_err(esp_io_expander_set_level(s_ioe, PIN_CHG_EN, 1), "CHG_EN level");
        note_err(esp_io_expander_set_level(s_ioe, PIN_NCHG_QC_EN, 0), "QC level");
        note_err(esp_io_expander_set_dir(s_ioe, PIN_CHG_EN | PIN_NCHG_QC_EN, IO_EXPANDER_OUTPUT), "charge pins out");
        /* The PI4IOE5V6408 resets with every output high-impedance: actually drive
         * them, as the BSP does for its own enable pins. */
        note_err(esp_io_expander_set_output_mode(s_ioe, PIN_CHG_EN | PIN_NCHG_QC_EN, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL),
                 "charge pins push-pull");
        note_err(esp_io_expander_set_dir(s_ioe, PIN_CHG_STAT, IO_EXPANDER_INPUT), "CHG_STAT in");
        ESP_LOGI(TAG, "charger enabled (fast charge on)");
    }

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (i2c_master_probe(bus, INA226_ADDR, 100) != ESP_OK) {
        note_err(ESP_ERR_NOT_FOUND, "INA226 at 0x41");
        return;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = INA226_ADDR,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_ina);
    if (err == ESP_OK) {
        err = ina_write(REG_CONFIG, INA226_CONFIG);
    }
    note_err(err, "INA226 setup");
    s_last.monitor_ok = err == ESP_OK;
}

void battery_read(battery_t *out)
{
    int64_t now = esp_timer_get_time();
    if (s_last_us != 0 && now - s_last_us < SAMPLE_US) {
        *out = s_last;
        return;
    }
    int64_t dt_us = s_last_us ? now - s_last_us : 0;
    s_last_us = now;

    if (s_ina != NULL) {
        uint16_t bus, shunt;
        if (ina_read(REG_BUS, &bus) == ESP_OK && ina_read(REG_SHUNT, &shunt) == ESP_OK) {
            s_last.monitor_ok = true;
            s_last.volts = bus * 0.00125f;
            /* The shunt is wired so charging reads negative: invert, so + is charging. */
            s_last.amps = -((int16_t)shunt * 2.5e-6f) / INA226_SHUNT;
            /* 2S pack: per-cell 3.30 V empty .. 4.15 V full, as M5Unified. */
            int level = (int)((s_last.volts * 500.0f - 3300.0f) * 100.0f / (4150.0f - 3350.0f));
            s_last.level = level < 0 ? 0 : (level > 100 ? 100 : level);
        } else {
            s_last.monitor_ok = false;
        }
    }

    /* Charging is what the current says. CHG_STAT (P6) read high even while
     * the pack was discharging at 0.2 A, so it is not trusted. Hysteresis
     * keeps noise near zero from flickering the state. */
    bool charging = s_last.monitor_ok &&
                    (s_last.charging ? s_last.amps > CHARGING_OFF_A : s_last.amps > CHARGING_ON_A);
    if (charging && !s_last.charging && s_last.level_valid) {
        /* A new charging session. Brief top-off pauses (well under a minute)
         * carry on counting the same session. */
        s_last.session_mah = 0;
    }
    if (!charging && s_last.charging) {
        s_off_since_us = now;
    }
    s_last.charging = charging;
    /* Count the charge going in: current x time since the last sample. */
    if (charging) {
        s_last.session_mah += s_last.amps * 1000.0f * (float)dt_us / 3.6e9f;
    }
    s_last.level_valid = !charging && now - s_off_since_us >= SETTLE_US;
    s_last.level_in_s = s_last.level_valid || charging ? 0 : (int)((SETTLE_US - (now - s_off_since_us)) / 1000000) + 1;
    *out = s_last;
}
