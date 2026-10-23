#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    WIFI_ST_OFF,            /* wifi_start() not called yet */
    WIFI_ST_STARTING,       /* waiting for the C6 / esp_wifi_init */
    WIFI_ST_NOT_CONFIGURED, /* radio up, but WIFI_SSID is still the placeholder */
    WIFI_ST_CONNECTING,     /* associating, or backing off after a disconnect */
    WIFI_ST_CONNECTED,      /* has an IP */
    WIFI_ST_FAILED,         /* init failed: see init_err */
} wifi_state_t;

typedef struct {
    wifi_state_t state;
    bool link_up;           /* SDIO transport to the C6 is up */
    bool link_seen_down;    /* esp_hosted reported a transport failure or drop */
    esp_err_t init_err;     /* first failing init call, ESP_OK if none */
    const char *init_step;  /* name of that call */
    uint8_t mac[6];
    uint8_t last_reason;    /* last disconnect reason, 0 if none */
    uint32_t retries;
    int8_t rssi;
    char ip[16];
    char ssid[33];
} wifi_status_t;

/* Power the on-board ESP32-C6. Call first thing in app_main: esp_hosted's SDIO
 * probe starts from a constructor and does not wait for the C6. */
void wifi_power_on(void);

/* Bring up Wi-Fi in its own task. Needs esp_netif_init() and the default event
 * loop. Never blocks the caller; progress and failures land in wifi_get_status(). */
void wifi_start(void);

void wifi_get_status(wifi_status_t *out);

/* Short human name for a disconnect reason, with a hint where one helps. */
const char *wifi_reason_str(uint8_t reason);
