add_library(
    sky_memory
    STATIC
    private/memory.cpp
    public/memory.hpp
    private/allocator.cpp
    public/allocator.hpp
)
add_library(sky::memory ALIAS sky_memory)

target_include_directories(
    sky_memory
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private"
)

target_link_libraries(sky_memory PUBLIC sky::base sky::container)
