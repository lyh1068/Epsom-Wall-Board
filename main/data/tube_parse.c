#include "tube_parse.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "data/text_fold.h"

tube_severity_t tube_classify(int s)
{
    switch (s) {
    case 10:    /* Good Service */
    case 18:    /* No Issues */
    case 19:    /* Information */
        return TUBE_GOOD;
    case 0:     /* Special Service */
    case 7:     /* Reduced Service */
    case 8:     /* Bus Service */
    case 9:     /* Minor Delays */
    case 12:    /* Exit Only */
    case 13:    /* No Step Free Access */
    case 14:    /* Change of frequency */
    case 15:    /* Diverted */
    case 17:    /* Issues Reported */
        return TUBE_MINOR;
    case 1:     /* Closed */
    case 2:     /* Suspended */
    case 3:     /* Part Suspended */
    case 5:     /* Part Closure */
    case 6:     /* Severe Delays */
    case 11:    /* Part Closed */
    case 16:    /* Not Running */
        return TUBE_SEVERE;
    case 4:     /* Planned Closure */
    case 20:    /* Service Closed */
        return TUBE_CLOSED;
    default:
        return TUBE_UNKNOWN;
    }
}

/* Higher is worse, for picking one status per line. */
static int rank(tube_severity_t s)
{
    switch (s) {
    case TUBE_SEVERE:  return 4;
    case TUBE_MINOR:   return 3;
    case TUBE_UNKNOWN: return 2;
    case TUBE_CLOSED:  return 1;
    default:           return 0;
    }
}

static void get_str(char *dst, size_t size, const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        text_fold_ascii(dst, size, item->valuestring);
    } else {
        dst[0] = '\0';
    }
}

static void parse_line(const cJSON *l, tube_line_t *out)
{
    memset(out, 0, sizeof(*out));
    get_str(out->id, sizeof(out->id), l, "id");
    get_str(out->name, sizeof(out->name), l, "name");
    out->severity = TUBE_UNKNOWN;
    snprintf(out->status, sizeof(out->status), "No status");

    int best = -1;
    const cJSON *st;
    cJSON_ArrayForEach(st, cJSON_GetObjectItemCaseSensitive(l, "lineStatuses")) {
        const cJSON *sev = cJSON_GetObjectItemCaseSensitive(st, "statusSeverity");
        if (!cJSON_IsNumber(sev)) {
            continue;
        }
        tube_severity_t cls = tube_classify((int)sev->valuedouble);
        if (rank(cls) > best) {
            best = rank(cls);
            out->severity = cls;
            get_str(out->status, sizeof(out->status), st, "statusSeverityDescription");
            get_str(out->reason, sizeof(out->reason), st, "reason");
        }
    }
}

bool tube_parse(const char *json, size_t len, const char *order_csv, tube_status_t *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }

    const char *p = order_csv;
    while (*p && out->count < TUBE_MAX_LINES) {
        const char *comma = strchr(p, ',');
        size_t n = comma ? (size_t)(comma - p) : strlen(p);
        const cJSON *l;
        cJSON_ArrayForEach(l, root) {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(l, "id");
            if (cJSON_IsString(id) && strlen(id->valuestring) == n && strncmp(id->valuestring, p, n) == 0) {
                parse_line(l, &out->line[out->count++]);
                break;
            }
        }
        p += n + (comma ? 1 : 0);
    }
    cJSON_Delete(root);
    return true;
}
