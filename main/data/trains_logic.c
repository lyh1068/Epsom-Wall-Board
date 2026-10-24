#include "trains_logic.h"

#include <ctype.h>
#include <string.h>

int hhmm_to_min(const char *s)
{
    if (strlen(s) != 5 || s[2] != ':' || !isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1])
        || !isdigit((unsigned char)s[3]) || !isdigit((unsigned char)s[4])) {
        return -1;
    }
    int h = (s[0] - '0') * 10 + (s[1] - '0');
    int m = (s[3] - '0') * 10 + (s[4] - '0');
    return h < 24 && m < 60 ? h * 60 + m : -1;
}

int minutes_until(int now_min, int t_min)
{
    int d = t_min - now_min;
    if (d < -720) {
        d += 1440;
    } else if (d > 720) {
        d -= 1440;
    }
    return d;
}

etd_kind_t train_etd(const train_t *t, int *expected_min)
{
    int std = hhmm_to_min(t->std);
    int etd = hhmm_to_min(t->etd);
    if (strcmp(t->etd, "Cancelled") == 0) {
        *expected_min = -1;
        return ETD_CANCELLED;
    }
    if (etd >= 0) {
        *expected_min = etd;
        return ETD_EXPECTED;
    }
    *expected_min = std;
    if (strcmp(t->etd, "On time") == 0) {
        return ETD_ON_TIME;
    }
    if (strcmp(t->etd, "Delayed") == 0) {
        return ETD_DELAYED;
    }
    return ETD_UNKNOWN;
}

int train_delay_min(const train_t *t)
{
    int std = hhmm_to_min(t->std);
    int etd = hhmm_to_min(t->etd);
    if (std < 0 || etd < 0) {
        return 0;
    }
    int d = minutes_until(std, etd);
    return d > 0 ? d : 0;
}

bool leave_by(const train_t *t, int n, int now_min, int walk_min, int horizon_min, leave_by_t *out)
{
    for (int i = 0; i < n; i++) {
        if (!t[i].london) {
            continue;
        }
        int exp;
        etd_kind_t k = train_etd(&t[i], &exp);
        if (k == ETD_CANCELLED || exp < 0) {
            continue;
        }
        int until = minutes_until(now_min, exp);
        if (until < walk_min || until > horizon_min) {
            continue;
        }
        out->index = i;
        out->leave_in = until - walk_min;
        return true;
    }
    return false;
}
