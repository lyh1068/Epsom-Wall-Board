#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "data/tube_types.h"

/*
 * Parser for TfL /Line/{ids}/Status. Pure C + cJSON, host-tested against
 * test/fixtures/tfl_line_status.json.
 */

/* TfL statusSeverity code -> pill colour. */
tube_severity_t tube_classify(int tfl_severity);

/*
 * Fill out with the lines named in order_csv ("northern,jubilee,..."), in that
 * order: TfL returns them alphabetically. A line with several statuses shows
 * the worst. Lines missing from the response are left out. False if the JSON
 * is not a line array.
 */
bool tube_parse(const char *json, size_t len, const char *order_csv, tube_status_t *out);
