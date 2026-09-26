# 해당코드는 Codex로 수정됨
cmake_minimum_required(VERSION 3.24)
project(rdna3_unified_micro_engine LANGUAGES CXX)

find_package(Vulkan 1.3.304 REQUIRED)

add_library(rdna3_micro_engine STATIC
    src/rdna3_micro_engine.cpp
    src/rdna3_micro_engine.hpp)

target_compile_features(rdna3_micro_engine PUBLIC cxx_std_20)
target_link_libraries(rdna3_micro_engine PUBLIC Vulkan::Vulkan)
target_include_directories(rdna3_micro_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(MSVC)
    target_compile_options(rdna3_micro_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(rdna3_micro_engine PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
