#include "AudioRenderer.h"
#include "audio_out/AAudioOutput.h"

extern "C" {
#include <libavutil/frame.h>
}

#define TAG "AudioRenderer"

namespace ccplayer {

AudioRenderer::AudioRenderer(FrameQueue* frameQueue)
    : m_frameQueue(frameQueue)
    , m_ringBuffer(nullptr)
    , m_audioOutput(nullptr)
    , m_swrCtx(nullptr)
    , m_sampleRate(0)
    , m_channels(0)
{
}

AudioRenderer::~AudioRenderer() {
    close();
}

int AudioRenderer::open(int sampleRate, int channels, AVSampleFormat srcFmt, const AVChannelLayout* layout) {
    m_sampleRate = sampleRate;
    m_channels = channels;

    m_ringBuffer = new RingBuffer();

    // AAudio 仅支持 S16：源格式非 S16 时创建重采样器
    if (srcFmt != AV_SAMPLE_FMT_S16) {
        int ret = swr_alloc_set_opts2(&m_swrCtx,
            layout, AV_SAMPLE_FMT_S16, sampleRate,
            layout, srcFmt, sampleRate,
            0, nullptr);
        if (ret < 0 || !m_swrCtx || swr_init(m_swrCtx) < 0) {
            LOGE(TAG, "Failed to init SwrContext");
            if (m_swrCtx) swr_free(&m_swrCtx);
        }
    }

    auto* aaudio = new AAudioOutput();
    if (aaudio->open(sampleRate, channels) == 0) {
        m_audioOutput = aaudio;
        LOGI(TAG, "AAudio output opened");
        return 0;
    }
    LOGE(TAG, "Failed to open AAudio output");
    delete aaudio;
    return -1;
}

void AudioRenderer::close() {
    stop();
    if (m_audioOutput) {
        delete m_audioOutput;
        m_audioOutput = nullptr;
    }
    delete m_ringBuffer;
    m_ringBuffer = nullptr;
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
    }
}

void AudioRenderer::start() {
    if (!m_audioOutput || !m_ringBuffer) return;
    if (m_thread.joinable() && m_running.load()) return;

    if (m_thread.joinable() && !m_running.load()) {
        m_thread.join();
    }
    m_audioOutput->setCallback([this](uint8_t* buffer, int size) -> int {
        return onAudioData(buffer, size);
    });
    m_running = true;
    m_thread = std::thread(&AudioRenderer::audioLoop, this);
    m_audioOutput->resume();
}

void AudioRenderer::pause() {
    if (!m_thread.joinable()) return;
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (m_audioOutput) m_audioOutput->pause();
}

void AudioRenderer::resume() {
    start();
}

void AudioRenderer::stop() {
    if (m_thread.joinable()) {
        m_running = false;
        m_thread.join();
    }
    if (m_audioOutput) m_audioOutput->pause();
}

void AudioRenderer::seek(int serial, double targetSec) {
    m_serial = serial;
    m_seekTarget = targetSec;
    std::lock_guard<std::mutex> lock(m_ringMutex);
    resetLocked();
    m_lastPts = targetSec;
}

double AudioRenderer::getCurrentPts() {
    std::lock_guard<std::mutex> lock(m_ringMutex);
    double lastPts = m_lastPts.load();
    if (!m_ringBuffer) return lastPts;
    int unread = unreadLocked();
    int bytesPerSec = m_sampleRate * m_channels * 2;
    if (bytesPerSec <= 0) return lastPts;
    return lastPts - (double)unread / bytesPerSec;
}

int AudioRenderer::onAudioData(uint8_t* buffer, int size) {
    std::lock_guard<std::mutex> lock(m_ringMutex);
    return readLocked(buffer, size);
}

void AudioRenderer::audioLoop() {
    AudioFrame audioFrame;

    while (m_running) {
        if (m_frameQueue->popAudioFrame(&audioFrame, true) < 0) {
            if (!m_running) break;
            continue;
        }

        // 丢弃旧 seek 代际的帧
        if (audioFrame.serial != m_serial.load()) {
            av_frame_free(&audioFrame.frame);
            continue;
        }

        // 丢弃 seek 目标之前的帧（关键帧到目标之间）
        if (audioFrame.pts < m_seekTarget.load()) {
            av_frame_free(&audioFrame.frame);
            continue;
        }

        // 重采样为 S16 交错
        const uint8_t* srcData;
        int srcSize;
        bool allocated = false;

        if (m_swrCtx && audioFrame.frame->format != AV_SAMPLE_FMT_S16) {
            int nbSamples = audioFrame.frame->nb_samples;
            int outSamples = swr_get_out_samples(m_swrCtx, nbSamples);
            int bufSize = outSamples * m_channels * 2;
            auto* buf = new uint8_t[bufSize];

            int converted = swr_convert(m_swrCtx,
                &buf, outSamples,
                (const uint8_t**)audioFrame.frame->data, nbSamples);
            if (converted > 0) {
                srcData = buf;
                srcSize = converted * m_channels * 2;
                allocated = true;
            } else {
                delete[] buf;
                av_frame_free(&audioFrame.frame);
                continue;
            }
        } else {
            srcData = audioFrame.frame->data[0];
            srcSize = audioFrame.frame->nb_samples * m_channels * 2;
        }

        // 锁内写入：writeLocked 校验 serial，旧代际拒绝写入（seek 与写互斥）
        int written = 0;
        bool dropped = false;
        while (written < srcSize && m_running) {
            int n;
            {
                std::lock_guard<std::mutex> lock(m_ringMutex);
                n = writeLocked(srcData + written, srcSize - written, audioFrame.serial);
            }
            if (n > 0) {
                written += n;
            } else if (n < 0) {
                dropped = true; // serial 不匹配，整帧丢弃
                break;
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        }

        // 仅当帧确实写入后才更新时钟基准（避免旧帧覆盖 seek 基准）
        if (!dropped) {
            m_lastPts = audioFrame.pts;
        }

        if (allocated) {
            delete[] srcData;
        }
        av_frame_free(&audioFrame.frame);
    }
}

int AudioRenderer::writeLocked(const uint8_t* src, int len, int serial) {
    if (serial != m_serial.load()) return -1; // 旧代际，拒绝写入

    int w = m_ringBuffer->writePos;
    int r = m_ringBuffer->readPos;
    int avail = RingBuffer::SIZE - 1 - ((w - r + RingBuffer::SIZE) % RingBuffer::SIZE);
    if (len > avail) len = avail;
    if (len <= 0) return 0;

    int first = std::min(len, RingBuffer::SIZE - w);
    memcpy(m_ringBuffer->data + w, src, first);
    if (len > first) memcpy(m_ringBuffer->data, src + first, len - first);
    m_ringBuffer->writePos = (w + len) % RingBuffer::SIZE;
    return len;
}

int AudioRenderer::readLocked(uint8_t* dst, int len) {
    int w = m_ringBuffer->writePos;
    int r = m_ringBuffer->readPos;
    int avail = (w - r + RingBuffer::SIZE) % RingBuffer::SIZE;
    if (len > avail) len = avail;
    if (len <= 0) return 0;

    int first = std::min(len, RingBuffer::SIZE - r);
    memcpy(dst, m_ringBuffer->data + r, first);
    if (len > first) memcpy(dst + first, m_ringBuffer->data, len - first);
    m_ringBuffer->readPos = (r + len) % RingBuffer::SIZE;
    return len;
}

void AudioRenderer::resetLocked() {
    m_ringBuffer->writePos = 0;
    m_ringBuffer->readPos = 0;
}

int AudioRenderer::unreadLocked() const {
    if (!m_ringBuffer) return 0;
    int w = m_ringBuffer->writePos;
    int r = m_ringBuffer->readPos;
    return (w - r + RingBuffer::SIZE) % RingBuffer::SIZE;
}

} // namespace ccplayer
