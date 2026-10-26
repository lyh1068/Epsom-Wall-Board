#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "data/trains_logic.h"

/*
 * Parser for Darwin LDBWS GetDepBoardWithDetails (Rail Data Marketplace, JSON).
 * Pure C + cJSON, host-tested against test/fixtures/ldbws_eps_details.json.
 */

/*
 * Fill out (count, services, nrcc; not updated). A service is London-bound if
 * its destination or any later calling point has a CRS in london_csv
 * ("WAT,VIC,..."). Text is folded to ASCII for the fonts, and disruption
 * messages lose their HTML. False if the JSON is not a departure board.
 */
bool trains_parse(const char *json, size_t len, const char *london_csv, train_board_t *out);
