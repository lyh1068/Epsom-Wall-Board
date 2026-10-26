#pragma once

/* User settings kept in NVS across reboots. */

/* Open NVS and load saved values. Call early in app_main. */
void settings_init(void);

/* Backlight, percent (BRIGHTNESS_MIN..100). */
int settings_brightness(void);

/* Apply now; saved to flash by settings_save(). */
void settings_set_brightness(int percent);

/* Write changed values to flash. Called when the controls close, not on every
 * slider step, to spare the flash. */
void settings_save(void);
