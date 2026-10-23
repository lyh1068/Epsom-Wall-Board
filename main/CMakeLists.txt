idf_component_register(SRCS "app_main.c"
                            "data/adsb_parse.c" "data/aircraft.c"
                            "data/home.c"
                            "data/route_cache.c" "data/route_parse.c" "data/routes.c"
                            "data/weather.c" "data/weather_parse.c"
                            "geo/geo.c"
                            "hw/rx8130.c"
                            "net/http_json.c" "net/sntp_time.c" "net/wifi.c"
                            "ui/icons.c" "ui/radar.c" "ui/status_corner.c" "ui/weather_view.c"
                    INCLUDE_DIRS ".")

# Credentials: tools/gen_secrets.py turns .env (or real environment variables)
# into wallboard_secrets.h inside the build directory. It runs on every build
# but only rewrites the header when a value changes.
idf_build_get_property(python PYTHON)
idf_build_get_property(project_dir PROJECT_DIR)
set(secrets_dir "${CMAKE_BINARY_DIR}/generated")
add_custom_target(wallboard_secrets
    COMMAND ${python} "${project_dir}/tools/gen_secrets.py"
            --env "${project_dir}/.env" --out "${secrets_dir}/wallboard_secrets.h"
    BYPRODUCTS "${secrets_dir}/wallboard_secrets.h"
    VERBATIM)
add_dependencies(${COMPONENT_LIB} wallboard_secrets)
target_include_directories(${COMPONENT_LIB} PRIVATE "${secrets_dir}")
