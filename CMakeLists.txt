cmake_minimum_required(VERSION 3.20)

project(sky_net LANGUAGES CXX)

add_executable(sky_net src/main.cpp)
target_compile_features(sky_net PRIVATE cxx_std_23)
