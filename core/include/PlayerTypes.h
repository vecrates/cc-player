#pragma once

#include <cstdint>

namespace ccplayer {

enum class PlayerState {
    Idle,
    Initialized,
    Prepared,
    Started,
    Paused,
    Stopped,
    Error
};

enum class PlayerError {
    NoError = 0,
    InvalidDataSource = -1,
    UnsupportedFormat = -2,
    DecodeError = -3,
    IOError = -4,
    NetworkTimeout = -5,
    BufferingError = -6,
    RenderError = -7,
    AudioError = -8
};

struct PlayerCallbacks {
    void (*onPrepared)(void* userData) = nullptr;
    void (*onCompletion)(void* userData) = nullptr;
    void (*onError)(int errorCode, void* userData) = nullptr;
    void (*onProgress)(int64_t currentMs, int64_t durationMs, void* userData) = nullptr;
    void (*onBufferingUpdate)(int percent, void* userData) = nullptr;
    void (*onSeekComplete)(void* userData) = nullptr;
};

} // namespace ccplayer
