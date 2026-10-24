/*
 * Epsom Wall Board entry point.
 */

#include "bsp/esp-bsp.h"
#include "config.h"
#include "data/aircraft.h"
#include "data/routes.h"
#include "data/tube.h"
#include "data/weather.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lvgl.h"
#include "net/sntp_time.h"
#include "net/wifi.h"
#include "ui/ui_root.h"

static const char *TAG = "main";

void app_main(void)
{
    /* First, before the display: esp_hosted is already probing for the C6. */
    wifi_power_on();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_start();

    lv_display_t *disp = bsp_display_start();
    if (disp == NULL) {
        ESP_LOGE(TAG, "display did not start");
    }

    time_init();
    time_sync_start();
    aircraft_start();
    routes_start();
    weather_start();
    tube_start();

    bsp_display_lock(0);
    /* Landscape: rotated in software (on the PPA), see the tab5-board skill. */
    bsp_display_rotate(disp, UI_ROTATION);
    ui_root_create(lv_screen_active());
    bsp_display_unlock();
    bsp_display_backlight_on();
}
