add_library(sky_file STATIC public/file.hpp private/file.cpp)
add_library(sky::file ALIAS sky_file)

target_include_directories(
    sky_file
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private"
)

target_link_libraries(sky_file PUBLIC sky::base sky::container)
