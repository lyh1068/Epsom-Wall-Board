#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

/* Europe/London. The trailing /2 puts the October changeover at 02:00 BST. */
#define WALLBOARD_TZ "GMT0BST,M3.5.0/1,M10.5.0/2"

typedef enum {
    CLOCK_SRC_NONE,     /* no RTC time and no NTP yet: the clock is wrong */
    CLOCK_SRC_RTC,      /* seeded from the RX8130 at boot */
    CLOCK_SRC_NTP,      /* synced from NTP at least once */
} clock_src_t;

typedef enum {
    RTC_ST_UNKNOWN,
    RTC_ST_OK,          /* present and held a valid time at boot */
    RTC_ST_INVALID,     /* present, but lost power or was never set */
    RTC_ST_MISSING,     /* nothing answered on I2C */
    RTC_ST_ERROR,       /* I2C error: see rtc_err */
} rtc_state_t;

typedef struct {
    clock_src_t source;
    rtc_state_t rtc;
    esp_err_t rtc_err;
    esp_err_t sntp_err;     /* esp_netif_sntp_init result: non-OK means SNTP never started */
    time_t last_sync;       /* 0 if never */
    bool rtc_written;       /* RTC updated from NTP since boot */
} time_status_t;

/* Set the timezone and seed the system clock from the RTC. Needs the BSP's I2C bus. */
void time_init(void);

/* Start SNTP. Needs esp_netif_init(); fine to call before Wi-Fi connects. */
void time_sync_start(void);

void time_get_status(time_status_t *out);
