add_executable(sky main.cpp)
target_compile_features(sky PRIVATE cxx_std_23)
target_link_libraries(sky PRIVATE sky::memory)
