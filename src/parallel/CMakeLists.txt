add_library(
    sky_parallel
    STATIC
    public/mutex.hpp
    public/thread.hpp
    public/thread_system.hpp
    private/mutex.cpp
    private/thread.cpp
    private/thread_system.cpp
)
add_library(sky::parallel ALIAS sky_parallel)

target_include_directories(
    sky_parallel
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private"
)

find_package(Threads REQUIRED)
target_link_libraries(
    sky_parallel
    PUBLIC sky::base sky::container
    PRIVATE Threads::Threads
)
