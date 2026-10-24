#include "route_parse.h"

#include <stdint.h>
#include <string.h>
#include "cJSON.h"
#include "data/text_fold.h"

/* Airlines whose callsign suffixes are reused, so adsbdb's route is often
 * stale for them (PLAN.md §4). */
static const char *const UNVERIFIED_AIRLINES[] = { "EZY", "RYR", "TOM" };

static void get_str(char *dst, size_t size, const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        text_fold_ascii(dst, size, item->valuestring);
    } else {
        dst[0] = '\0';
    }
}

route_parse_result_t route_parse(const char *json, size_t len, route_t *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == NULL) {
        return ROUTE_PARSE_ERROR;
    }
    route_parse_result_t res = ROUTE_PARSE_ERROR;
    const cJSON *resp = cJSON_GetObjectItemCaseSensitive(root, "response");
    const cJSON *fr = cJSON_GetObjectItemCaseSensitive(resp, "flightroute");

    if (cJSON_IsString(resp) && strcmp(resp->valuestring, "unknown callsign") == 0) {
        res = ROUTE_PARSE_UNKNOWN;
    } else if (cJSON_IsObject(fr)) {
        get_str(out->callsign, sizeof(out->callsign), fr, "callsign");
        const cJSON *al = cJSON_GetObjectItemCaseSensitive(fr, "airline");
        get_str(out->airline_icao, sizeof(out->airline_icao), al, "icao");
        get_str(out->airline_name, sizeof(out->airline_name), al, "name");
        const cJSON *o = cJSON_GetObjectItemCaseSensitive(fr, "origin");
        get_str(out->origin_iata, sizeof(out->origin_iata), o, "iata_code");
        get_str(out->origin_icao, sizeof(out->origin_icao), o, "icao_code");
        get_str(out->origin_city, sizeof(out->origin_city), o, "municipality");
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(fr, "destination");
        get_str(out->dest_iata, sizeof(out->dest_iata), d, "iata_code");
        get_str(out->dest_icao, sizeof(out->dest_icao), d, "icao_code");
        get_str(out->dest_city, sizeof(out->dest_city), d, "municipality");
        res = ROUTE_PARSE_OK;
    }
    cJSON_Delete(root);
    return res;
}

bool route_is_unverified(const route_t *r)
{
    if ((r->origin_iata[0] == '\0' && r->origin_icao[0] == '\0')
        || (r->dest_iata[0] == '\0' && r->dest_icao[0] == '\0')) {
        return true;
    }
    /* The callsign prefix, not just the airline field: adsbdb may leave the
     * airline out, and the suffix-reuse problem follows the callsign. */
    for (size_t i = 0; i < sizeof(UNVERIFIED_AIRLINES) / sizeof(UNVERIFIED_AIRLINES[0]); i++) {
        if (strncmp(r->callsign, UNVERIFIED_AIRLINES[i], 3) == 0
            || strcmp(r->airline_icao, UNVERIFIED_AIRLINES[i]) == 0) {
            return true;
        }
    }
    return false;
}
