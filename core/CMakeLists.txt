cmake_minimum_required(VERSION 3.18)
project(ccplayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Build unit tests" OFF)
option(TARGET_ANDROID "Build for Android" OFF)
option(TARGET_IOS "Build for iOS" OFF)

# ---- FFmpeg 头文件 / 库定位 ----
# 解析优先级：
#   1) -DFFMPEG_DIR=<dir>    （Android JNI 构建由 build.gradle 传入）
#   2) 环境变量 FFMPEG_DIR
#   3) 按平台推断 third_party/<platform>，Android 下再拼接 <ANDROID_ABI>
if(NOT DEFINED FFMPEG_DIR OR FFMPEG_DIR STREQUAL "")
    if(DEFINED ENV{FFMPEG_DIR} AND NOT "$ENV{FFMPEG_DIR}" STREQUAL "")
        set(FFMPEG_DIR "$ENV{FFMPEG_DIR}")
    else()
        # core 可能被 add_subdirectory 引入（android/app/src/main/jni），
        # 此时 CMAKE_SOURCE_DIR 指向上层工程目录，必须用 CMAKE_CURRENT_SOURCE_DIR。
        set(_ffmpeg_root "${CMAKE_CURRENT_SOURCE_DIR}/../third_party")
        string(TOLOWER "${CMAKE_SYSTEM_NAME}" _sys_lower)
        # 平台目录名：Android -> android，iOS -> ios，桌面取系统名（Darwin/Linux）
        if(ANDROID)
            set(_plat_dirs "android")
        elseif(IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
            set(_plat_dirs "ios")
        else()
            set(_plat_dirs "${CMAKE_SYSTEM_NAME}" "${_sys_lower}")
        endif()
        foreach(_cand IN LISTS _plat_dirs)
            if(EXISTS "${_ffmpeg_root}/${_cand}")
                set(FFMPEG_DIR "${_ffmpeg_root}/${_cand}")
                break()
            endif()
        endforeach()
        if(NOT FFMPEG_DIR)
            set(FFMPEG_DIR "${_ffmpeg_root}/${CMAKE_SYSTEM_NAME}")
        endif()
    endif()
endif()

# Android 预编译产物按 ABI 分目录：third_party/android/<abi>/{include,lib}
if(DEFINED ANDROID_ABI AND NOT ANDROID_ABI STREQUAL "")
    if(EXISTS "${FFMPEG_DIR}/${ANDROID_ABI}")
        set(FFMPEG_DIR "${FFMPEG_DIR}/${ANDROID_ABI}")
    endif()
endif()

get_filename_component(FFMPEG_DIR "${FFMPEG_DIR}" ABSOLUTE)

# 校验：给出可执行的修复指引，而不是让编译器抛出难以定位的 “file not found”
if(NOT EXISTS "${FFMPEG_DIR}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR
        "找不到 FFmpeg 头文件: ${FFMPEG_DIR}/include/libavcodec/avcodec.h\n"
        "请先构建对应平台的 FFmpeg 预编译库，或用 -DFFMPEG_DIR=<含 include/ 与 lib/ 的目录> 指定。\n"
        "  Android : ./scripts/build_ffmpeg_android.sh arm64-v8a\n"
        "  iOS     : ./scripts/build_ffmpeg_ios.sh")
endif()

add_library(ccplayer STATIC
    src/platform/log.cpp
    src/queue/PacketQueue.cpp
    src/queue/FrameQueue.cpp
    src/datasource/FileDataSource.cpp
    src/datasource/FdDataSource.cpp
    src/datasource/NetworkDataSource.cpp
    src/demuxer/Demuxer.cpp
    src/decoder/Decoder.cpp
    src/sync/Sync.cpp
    src/audio_out/AAudioOutput.cpp
    src/audio_out/AudioRenderer.cpp
    src/video_out/VideoOutput.cpp
    src/video_out/VideoRenderer.cpp
    src/player/PlayerImpl.cpp
    src/player/Player.cpp
)

target_include_directories(ccplayer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${FFMPEG_DIR}/include
)

target_link_directories(ccplayer PUBLIC ${FFMPEG_DIR}/lib)

target_link_libraries(ccplayer
    avformat
    avcodec
    avutil
    swresample
    avfilter
    swscale
)

if(TARGET_ANDROID)
    target_link_libraries(ccplayer log aaudio OpenSLES)
elseif(TARGET_IOS)
    find_library(OPENGLES_LIB OpenGLES)
    find_library(AUDIO_UNIT AudioToolbox)
    target_link_libraries(ccplayer ${OPENGLES_LIB} ${AUDIO_UNIT})
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(../tests tests)
endif()
