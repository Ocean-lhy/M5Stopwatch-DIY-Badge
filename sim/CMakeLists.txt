cmake_minimum_required(VERSION 3.14)
project(badge_sim LANGUAGES C CXX)

# Host build of the firmware's page renderer and router against the project's
# own M5GFX copy. Only the drawing code is shared; no ESP-IDF drivers are used.
set(CMAKE_CXX_STANDARD 17)
find_package(SDL2 REQUIRED)

set(M5GFX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../components/M5GFX")
file(GLOB M5GFX_SOURCES
    "${M5GFX_DIR}/src/*.cpp"
    "${M5GFX_DIR}/src/lgfx/v1/*.cpp"
    "${M5GFX_DIR}/src/lgfx/v1/misc/*.cpp"
    "${M5GFX_DIR}/src/lgfx/v1/panel/*.cpp"
    "${M5GFX_DIR}/src/lgfx/v1/touch/*.cpp"
    "${M5GFX_DIR}/src/lgfx/utility/*.c"
    "${M5GFX_DIR}/src/lgfx/Fonts/efont/*.c"
    "${M5GFX_DIR}/src/lgfx/Fonts/IPA/*.c"
    "${M5GFX_DIR}/src/lgfx/v1/platforms/sdl/*.cpp"
)
add_library(M5GFX STATIC ${M5GFX_SOURCES})
target_include_directories(M5GFX SYSTEM PUBLIC "${M5GFX_DIR}/src" ${SDL2_INCLUDE_DIRS})
target_link_libraries(M5GFX PUBLIC ${SDL2_LIBRARIES})

add_executable(badge_sim sim_main.cpp ../main/ui_pages.cpp ../main/ui_router.cpp)
target_include_directories(badge_sim PRIVATE ../main)
target_compile_options(badge_sim PRIVATE -Wall -Wextra)
target_link_libraries(badge_sim PRIVATE M5GFX)
