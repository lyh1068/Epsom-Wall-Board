#pragma once

#include <stdbool.h>
#include "esp_err.h"

/*
 * Battery charging and monitoring. The Tab5's charger is enabled by CHG_EN on
 * IO expander 0x44 (P7), which the Espressif BSP never drives: left as a
 * pulled-down input, the charger stays off and the pack never charges. Pin
 * map and settings follow M5Stack's M5Unified (Power_Class, board_M5Tab5).
 */

typedef struct {
    bool monitor_ok;        /* INA226 answered */
    bool charging;          /* CHG_STAT (P6) high */
    float volts;            /* pack voltage (2S Li-ion, ~6.6-8.3 V) */
    float amps;             /* + charging, - discharging */
    int level;              /* 0..100 %, from voltage: approximate */
    bool level_valid;       /* off the charger for a minute: the charging voltage
                             * inflates a voltage-based level, so only then */
    int level_in_s;         /* seconds until level_valid, while it is not */
    float session_mah;      /* charge added since charging last started */
    esp_err_t err;          /* first init failure, ESP_OK if none */
} battery_t;

/* Enable the charger (fast-charge on) and set up the INA226. Needs the BSP's
 * I2C bus and IO expander, so call after the display or Wi-Fi power-up. */
void battery_init(void);

/* Latest reading, refreshed at most every 2 s. */
void battery_read(battery_t *out);
