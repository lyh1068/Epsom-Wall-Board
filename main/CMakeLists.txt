idf_component_register(SRCS "app_main.c" "diag.c" "lvgl_demo_ui.c"
                            "hw/rx8130.c" "net/wifi.c" "net/sntp_time.c"
                    INCLUDE_DIRS ".")

lvgl_port_create_c_image("images/esp_logo.png" "images/gen/" "ARGB8888" "NONE")
lvgl_port_create_c_image("images/esp_text.png" "images/gen/" "ARGB8888" "NONE")
lvgl_port_add_images(${COMPONENT_LIB} "images/gen/")

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
