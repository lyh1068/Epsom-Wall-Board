#!/bin/sh
# Build and run the host tests (no board needed). Uses the cJSON that ships
# with ESP-IDF, so IDF_PATH must be set, or ESP-IDF must be at ~/esp/esp-idf.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
idf=${IDF_PATH:-$HOME/esp/esp-idf}
cjson="$idf/components/json/cJSON"
out="$root/build/host_tests"
mkdir -p "$out"

san="-fsanitize=address,undefined"

# Vendored cJSON: compiled on its own, since the macOS SDK flags its sprintf use.
cc -std=c11 -O1 -g $san -w -c "$cjson/cJSON.c" -o "$out/cJSON.o"

cc -std=c11 -Wall -Wextra -Werror -O1 -g $san \
    -I"$root/main" -I"$cjson" \
    "$root/test/host/test_main.c" \
    "$root/main/geo/geo.c" \
    "$root/main/data/adsb_parse.c" \
    "$root/main/data/route_parse.c" \
    "$root/main/data/route_cache.c" \
    "$out/cJSON.o" \
    -lm -o "$out/test_main"

"$out/test_main" "$root/test/fixtures"
