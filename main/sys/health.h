#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Why the board last restarted, and how close it runs to its limits. The
 * serial port cannot be read without stopping the app (tab5-board skill), so
 * this goes on screen. A crash also leaves an ELF core dump in the coredump
 * partition: read it with `idf.py coredump-info`, which works in download mode.
 */

typedef struct {
    const char *reason;         /* "power-on", "crash", "task watchdog", ... */
    bool crashed;               /* panic, watchdog or brownout */
    uint32_t boots;             /* since power-on */
    uint32_t crashes;           /* since power-on */
    bool dump;                  /* a core dump is stored in flash */
    bool dump_this_fw;          /* ... and it came from this firmware */
    char dump_task[16];
    uint32_t dump_pc;           /* look up with riscv32-esp-elf-addr2line -e build/epsom_wallboard.elf */
} health_boot_t;

/* Call first thing in app_main. */
void health_init(void);

const health_boot_t *health_boot(void);

/* Call from an LVGL timer: samples the LVGL task's stack headroom. */
void health_sample_lvgl(void);

uint32_t health_lvgl_stack_free(void);      /* bytes, lowest seen */
size_t health_internal_free(void);          /* bytes, now */
size_t health_internal_min_free(void);      /* bytes, lowest since boot */
uint32_t health_uptime_s(void);
