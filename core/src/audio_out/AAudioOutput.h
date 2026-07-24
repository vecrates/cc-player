#pragma once

#include "audio_out/AudioOutput.h"
#include <aaudio/AAudio.h>
#include <mutex>
#include <atomic>

namespace ccplayer {

class AAudioOutput : public AudioOutput {
public:
    AAudioOutput();
    ~AAudioOutput() override;

    int open(int sampleRate, int channels) override;
    void close() override;

    int write(const uint8_t* data, int size) override;
    void flush() override;
    void pause() override;
    void resume() override;

    int64_t getLatencyMs() override;

    void setCallback(AudioCallback cb) override;

private:
    static aaudio_data_callback_result_t dataCallback(
        AAudioStream* stream, void* userData,
        void* audioData, int32_t numFrames);

    AAudioStream* m_stream;
    int m_sampleRate;
    int m_channels;
    AudioCallback m_callback;
    std::mutex m_mutex;
};

} // namespace ccplayer
