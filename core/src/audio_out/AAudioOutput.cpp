#include "AAudioOutput.h"
#include "platform/log.h"
#include <cstring>

#define TAG "AAudioOutput"

namespace ccplayer {

AAudioOutput::AAudioOutput()
    : m_stream(nullptr)
    , m_sampleRate(0)
    , m_channels(0)
{
}

AAudioOutput::~AAudioOutput() {
    close();
}

int AAudioOutput::open(int sampleRate, int channels) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_sampleRate = sampleRate;
    m_channels = channels;

    AAudioStreamBuilder* builder = nullptr;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK) {
        LOGE(TAG, "Failed to create stream builder: %d", result);
        return -1;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setSampleRate(builder, sampleRate);
    AAudioStreamBuilder_setChannelCount(builder, channels);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, dataCallback, this);

    result = AAudioStreamBuilder_openStream(builder, &m_stream);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK) {
        LOGE(TAG, "Failed to open AAudio stream: %d", result);
        m_stream = nullptr;
        return -1;
    }

    m_sampleRate = AAudioStream_getSampleRate(m_stream);
    m_channels = AAudioStream_getChannelCount(m_stream);

    LOGI(TAG, "AAudio opened: %d Hz, %d channels, latency=%d ms",
         m_sampleRate, m_channels,
         (int)(AAudioStream_getFramesPerBurst(m_stream) * 1000 / m_sampleRate));
    return 0;
}

void AAudioOutput::close() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_stream) {
        AAudioStream_requestStop(m_stream);
        AAudioStream_close(m_stream);
        m_stream = nullptr;
    }
}

int AAudioOutput::write(const uint8_t* data, int size) {
    // AAudio uses callback mode, direct write not needed
    // This is a no-op for callback-based output
    return size;
}

void AAudioOutput::flush() {
    if (m_stream) {
        AAudioStream_requestFlush(m_stream);
    }
}

void AAudioOutput::pause() {
    if (m_stream) {
        AAudioStream_requestPause(m_stream);
    }
}

void AAudioOutput::resume() {
    if (m_stream) {
        AAudioStream_requestStart(m_stream);
    }
}

int64_t AAudioOutput::getLatencyMs() {
    if (!m_stream) return 0;
    int64_t frames = AAudioStream_getFramesRead(m_stream);
    int64_t sampleRate = AAudioStream_getSampleRate(m_stream);
    if (sampleRate == 0) return 0;
    return (frames * 1000) / sampleRate;
}

void AAudioOutput::setCallback(AudioCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = cb;
}

aaudio_data_callback_result_t AAudioOutput::dataCallback(
    AAudioStream* stream, void* userData,
    void* audioData, int32_t numFrames)
{
    auto* self = static_cast<AAudioOutput*>(userData);
    int channels = self->m_channels;
    int bytesPerFrame = channels * sizeof(int16_t);
    int bufferSize = numFrames * bytesPerFrame;

    if (self->m_callback) {
        int filled = self->m_callback(static_cast<uint8_t*>(audioData), bufferSize);
        if (filled < bufferSize) {
            memset(static_cast<uint8_t*>(audioData) + filled, 0, bufferSize - filled);
        }
    } else {
        memset(audioData, 0, bufferSize);
    }

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

} // namespace ccplayer
