/*
 * Epsom Wall Board entry point.
 */

#include "bsp/esp-bsp.h"
#include "config.h"
#include "data/aircraft.h"
#include "data/bus.h"
#include "data/routes.h"
#include "hw/battery.h"
#include "data/tube.h"
#include "data/weather.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lvgl.h"
#include "net/sntp_time.h"
#include "net/wifi.h"
#include "sys/health.h"
#include "sys/settings.h"
#include "ui/ui_root.h"

static const char *TAG = "main";

void app_main(void)
{
    /* Why did we restart? Read before anything else can crash again. */
    health_init();
    settings_init();

    /* Then, before the display: esp_hosted is already probing for the C6. */
    wifi_power_on();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_start();

    /* The BSP default, except the draw and PPA rotation buffers go in PSRAM.
     * In internal DMA RAM they took ~216 KB of a ~437 KB heap, and the Wi-Fi
     * link asserts when it cannot get an RX buffer: that was the restarts. */
    /* 240 lines rather than the BSP's 50: a full-screen redraw (a page switch)
     * is rendered, rotated and copied in ~6 passes instead of ~26. */
    const bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 240,
        .double_buffer = CONFIG_BSP_LCD_DRAW_BUF_DOUBLE,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = true,
        },
    };
    lv_display_t *disp = bsp_display_start_with_config(&disp_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "display did not start");
    }

    /* Charger on: the BSP never drives CHG_EN, so the pack would not charge. */
    battery_init();
    time_init();
    time_sync_start();
    aircraft_start();
    routes_start();
    weather_start();
    tube_start();
    bus_start();

    bsp_display_lock(0);
    /* Landscape: rotated in software (on the PPA), see the tab5-board skill. */
    bsp_display_rotate(disp, UI_ROTATION);
    ui_root_create(lv_screen_active());
    bsp_display_unlock();
    /* The saved brightness (swipe down to change), not the BSP's 100%. */
    settings_set_brightness(settings_brightness());
}
