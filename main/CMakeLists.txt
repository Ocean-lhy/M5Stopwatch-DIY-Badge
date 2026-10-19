# profile/ (or profile.example/) is turned into badge_profile.h + avatar.jpg here.
set(BADGE_PROFILE_OUT "${CMAKE_BINARY_DIR}/badge_profile")

idf_component_register(
    SRCS
        "main.cpp"
        "board.cpp"
        "display.cpp"
        "layers.cpp"
        "ui_pages.cpp"
        "ui_router.cpp"
        "input.cpp"
        "touch.cpp"
        "haptics.cpp"
        "power.cpp"
        "rtc_time.cpp"
        "badge_config.cpp"
        "wifi_portal.cpp"
        "dns_server.cpp"
    INCLUDE_DIRS "." "../include"
    EMBED_FILES
        "${BADGE_PROFILE_OUT}/avatar.jpg"
    REQUIRES
        M5GFX
        M5PM1
        M5IOE1
        ic_rx8130
        esp_http_server
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        nvs_flash
        spiffs
        json
)

idf_build_get_property(python PYTHON)
set(badge_profile_cmd ${python} ${PROJECT_DIR}/tools/gen_profile.py --project ${PROJECT_DIR} --out ${BADGE_PROFILE_OUT})
execute_process(COMMAND ${badge_profile_cmd} RESULT_VARIABLE badge_profile_result)
if(badge_profile_result)
    message(FATAL_ERROR "gen_profile.py failed, see the message above")
endif()
target_include_directories(${COMPONENT_LIB} PRIVATE "${BADGE_PROFILE_OUT}")
# Re-run on every build so edits to profile/ apply without a reconfigure.
add_custom_target(badge_profile COMMAND ${badge_profile_cmd} VERBATIM)
add_dependencies(${COMPONENT_LIB} badge_profile)

# Regenerate the gzip web bundle from web/ (only rewrites the header on change).
add_custom_target(badge_web_assets
    COMMAND ${python} ${PROJECT_DIR}/tools/embed_web.py
            --src ${PROJECT_DIR}/web --out ${PROJECT_DIR}/include/web_assets.h --target idf --budget 160
    WORKING_DIRECTORY ${PROJECT_DIR}
    COMMENT "Embedding web editor"
    VERBATIM)
add_dependencies(${COMPONENT_LIB} badge_web_assets)
