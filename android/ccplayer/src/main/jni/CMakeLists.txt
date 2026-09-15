cmake_minimum_required(VERSION 3.18)
project(ccplayer_jni LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core library
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core ${CMAKE_BINARY_DIR}/core)

# JNI shared library
add_library(ccplayer_jni SHARED
    ccplayer_jni.cpp
)

target_include_directories(ccplayer_jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core/src
)

target_link_libraries(ccplayer_jni
    ccplayer
    android
    log
    aaudio
    EGL
    GLESv3
)
