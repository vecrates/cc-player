#pragma once

#include <cstdint>
#include <functional>

namespace ccplayer {

class AudioOutput {
public:
    virtual ~AudioOutput() = default;

    virtual int open(int sampleRate, int channels) = 0;
    virtual void close() = 0;

    virtual int write(const uint8_t* data, int size) = 0;
    virtual void flush() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;

    virtual int64_t getLatencyMs() = 0;

    using AudioCallback = std::function<int(uint8_t* buffer, int size)>;
    virtual void setCallback(AudioCallback cb) = 0;
};

} // namespace ccplayer
