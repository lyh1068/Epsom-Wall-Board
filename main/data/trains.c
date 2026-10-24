#include "trains.h"

#include <stdio.h>
#include <string.h>
#include "config.h"

#if TRAINS_DEMO

/*
 * A plausible Epsom board on a fixed clock-face pattern, so rows move on as
 * time passes like a real board, with each status case represented: on time,
 * a small delay, a big (red) delay, a cancellation, and non-London services.
 */
typedef struct {
    int minute;
    const char *dest, *crs, *plat, *op, *etd_kind;
    int delay;
    bool london;
} pattern_t;

static const pattern_t PATTERN[] = {
    { 3,  "London Waterloo", "WAT", "2", "South Western Railway", "ontime",  0, true },
    { 10, "London Victoria", "VIC", "4", "Southern",              "late",    3, true },
    { 18, "Dorking",         "DKG", "1", "South Western Railway", "ontime",  0, false },
    { 25, "London Bridge",   "LBG", "4", "Southern",              "ontime",  0, true },
    { 33, "London Waterloo", "WAT", "2", "South Western Railway", "late",    7, true },
    { 40, "London Victoria", "VIC", "3", "Southern",              "cancel",  0, true },
    { 48, "Guildford",       "GLD", "1", "South Western Railway", "ontime",  0, false },
    { 55, "Horsham",         "HRH", "3", "Southern",              "ontime",  0, false },
};
#define PATTERN_N ((int)(sizeof(PATTERN) / sizeof(PATTERN[0])))

static void fmt_hhmm(char *buf, size_t size, int min)
{
    min = ((min % 1440) + 1440) % 1440;
    snprintf(buf, size, "%02d:%02d", min / 60, min % 60);
}

bool trains_get(train_board_t *out)
{
    memset(out, 0, sizeof(*out));
    time_t now = time(NULL);
    if (now < 1672531200) {
        return false;       /* no clock yet: a demo board at 00:00 would only confuse */
    }
    struct tm lt;
    localtime_r(&now, &lt);
    int now_min = lt.tm_hour * 60 + lt.tm_min;

    /* Start a couple of minutes back, so a just-missed train shows dimmed. */
    int hour_start = (now_min - 2) / 60 * 60;
    for (int h = 0; h < 3 && out->count < TRAINS_MAX; h++) {
        for (int i = 0; i < PATTERN_N && out->count < TRAINS_MAX; i++) {
            int dep = hour_start + h * 60 + PATTERN[i].minute;
            if (minutes_until(now_min, dep % 1440) < -1) {
                continue;
            }
            train_t *t = &out->t[out->count++];
            const pattern_t *p = &PATTERN[i];
            fmt_hhmm(t->std, sizeof(t->std), dep);
            if (strcmp(p->etd_kind, "cancel") == 0) {
                strcpy(t->etd, "Cancelled");
            } else if (strcmp(p->etd_kind, "late") == 0) {
                fmt_hhmm(t->etd, sizeof(t->etd), dep + p->delay);
            } else {
                strcpy(t->etd, "On time");
            }
            strcpy(t->dest, p->dest);
            strcpy(t->dest_crs, p->crs);
            strcpy(t->platform, p->plat);
            strcpy(t->operator_name, p->op);
            t->london = p->london;
        }
    }
    strcpy(out->nrcc, "DEMO: sample disruption message - live trains arrive in Phase 6");
    out->demo = true;
    out->updated = now;
    return true;
}

#else
#error "Live trains are Phase 6; set TRAINS_DEMO to 1 until then"
#endif
