add_library(sky_base INTERFACE)
add_library(sky::base ALIAS sky_base)

target_include_directories(
    sky_base
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/public"
)
