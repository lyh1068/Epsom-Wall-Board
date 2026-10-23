#pragma once

#include <stdbool.h>
#include <time.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * RX8130CE real-time clock on the Tab5's internal I2C bus. Holds UTC, so the
 * GMT/BST changeover never makes a stored time ambiguous.
 */

/* ESP_ERR_NOT_FOUND if nothing answers at the RTC's address. */
esp_err_t rx8130_init(i2c_master_bus_handle_t bus);

/* *valid is false if the RTC lost power or holds an impossible date. */
esp_err_t rx8130_read_utc(struct tm *out, bool *valid);

esp_err_t rx8130_write_utc(const struct tm *utc);
