add_library(sky_json STATIC public/json.hpp private/json.cpp)
add_library(sky::json ALIAS sky_json)

target_include_directories(
    sky_json
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private"
)
