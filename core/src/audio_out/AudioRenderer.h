#pragma once

#include "queue/FrameQueue.h"
#include "audio_out/AudioOutput.h"
#include "platform/log.h"

extern "C" {
#include <libswresample/swresample.h>
}

#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <cstdint>
#include <algorithm>
#include <cstring>

namespace ccplayer {

/**
 * 音频渲染器（纯线程封装）：自管音频供数线程、重采样、环形缓冲与主时钟。
 * - 时钟采用「最近写入帧 PTS - 未消费字节/字节率」，随实际播放推进（消除超前）；
 * - seek 时在锁内清空环形缓冲并校验 serial，旧代际帧不写入（不停音频线程）。
 */
class AudioRenderer {
public:
    AudioRenderer(FrameQueue* frameQueue);
    ~AudioRenderer();

    // prepare：打开音频输出与重采样器
    int open(int sampleRate, int channels, AVSampleFormat srcFmt, const AVChannelLayout* layout);
    void close();

    void start();    // 创建音频线程 + resume AAudio
    void pause();    // 停止音频线程 + pause AAudio
    void resume();   // 重建音频线程 + resume AAudio
    void stop();     // 停止 + 释放音频资源

    // seek 代际更新：锁内清空环形缓冲、重置基准 PTS
    void seek(int serial, double targetSec);

    // 当前播放位置（秒）：最近写入帧 PTS - 未消费时长
    double getCurrentPts();

private:
    struct RingBuffer {
        static constexpr int SIZE = 65536;
        uint8_t data[SIZE];
        int writePos = 0;
        int readPos = 0;
    };

    void audioLoop();
    int onAudioData(uint8_t* buffer, int size); // AAudio 回调（锁内读）

    // 以下均在持有 m_ringMutex 时调用
    int writeLocked(const uint8_t* src, int len, int serial);
    int readLocked(uint8_t* dst, int len);
    void resetLocked();
    int unreadLocked() const;

    FrameQueue* m_frameQueue;

    std::mutex m_ringMutex;            // 保护 ring buffer 与 serial 校验的原子性
    RingBuffer* m_ringBuffer;
    std::atomic<int> m_serial{0};
    std::atomic<double> m_seekTarget{0.0};
    std::atomic<double> m_lastPts{0.0};

    AudioOutput* m_audioOutput;        // AAudioOutput（回调模式）
    SwrContext* m_swrCtx;
    int m_sampleRate;
    int m_channels;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
};

} // namespace ccplayer
