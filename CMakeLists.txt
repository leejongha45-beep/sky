cmake_minimum_required(VERSION 3.20)

project(sky LANGUAGES CXX)

add_subdirectory(src/base)
add_subdirectory(src/memory)
add_subdirectory(src/container)
add_subdirectory(src/io/file)
add_subdirectory(src/parser/json)
add_subdirectory(src/io/net)

add_executable(sky src/main.cpp)
target_compile_features(sky PRIVATE cxx_std_23)
target_link_libraries(sky PRIVATE sky::memory)
