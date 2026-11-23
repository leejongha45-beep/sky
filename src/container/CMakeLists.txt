add_library(sky_container INTERFACE)
add_library(sky::container ALIAS sky_container)

target_include_directories(
    sky_container
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/public"
)
