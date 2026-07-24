cmake_minimum_required(VERSION 3.18)
project(ccplayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Build unit tests" OFF)
option(TARGET_ANDROID "Build for Android" OFF)
option(TARGET_IOS "Build for iOS" OFF)

# FFmpeg include/lib paths (set by ffmpeg-kit or manually)
if(DEFINED ENV{FFMPEG_DIR})
    set(FFMPEG_DIR $ENV{FFMPEG_DIR})
elseif(NOT DEFINED FFMPEG_DIR)
    set(FFMPEG_DIR ${CMAKE_SOURCE_DIR}/../third_party/${CMAKE_SYSTEM_NAME})
endif()

# On Android, FFmpeg prebuilts are organized per-ABI
if(DEFINED ANDROID_ABI)
    get_filename_component(FFMPEG_DIR_ABS "${FFMPEG_DIR}" ABSOLUTE)
    if(EXISTS "${FFMPEG_DIR_ABS}/${ANDROID_ABI}")
        set(FFMPEG_DIR "${FFMPEG_DIR_ABS}/${ANDROID_ABI}")
    endif()
endif()

add_library(ccplayer STATIC
    src/platform/log.cpp
    src/queue/PacketQueue.cpp
    src/queue/FrameQueue.cpp
    src/demuxer/Demuxer.cpp
    src/decoder/Decoder.cpp
    src/sync/Sync.cpp
    src/audio_out/AAudioOutput.cpp
    src/video_out/VideoOutput.cpp
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
