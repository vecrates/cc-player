cmake_minimum_required(VERSION 3.18)

# 核心单元测试：覆盖纯 C++ 模块（Clock / AudioVideoSyncer / CommandQueue），
# 不依赖 FFmpeg / 平台库，可在桌面环境直接编译运行。
add_executable(ccplayer_tests
    test_core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/src/sync/Sync.cpp
)

target_include_directories(ccplayer_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/src
)

target_compile_features(ccplayer_tests PRIVATE cxx_std_17)

enable_testing()
add_test(NAME ccplayer_tests COMMAND ccplayer_tests)
