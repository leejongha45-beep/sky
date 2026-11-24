add_library(
    sky_net STATIC
    public/socket.hpp
    public/socket_system.hpp
    private/socket.cpp
    private/socket_system.cpp
)
add_library(sky::net ALIAS sky_net)

target_include_directories(
    sky_net
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private"
)

target_link_libraries(sky_net PUBLIC sky::base sky::container)
