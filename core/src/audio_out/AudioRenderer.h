#pragma once

#include "queue/FrameQueue.h"
#include "audio_out/AudioOutput.h"
#include "platform/log.h"

extern "C" {
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/channel_layout.h>
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
 * - seek 时在锁内清空环形缓冲并校验 serial，旧代际帧不写入（不停音频线程）；
 * - 变速不变调：speed != 1.0 时走 FLTP → atempo → S16 滤镜链，speed 切换在音频线程内重建。
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

    // 设置播放倍速（原子变量；音频线程检测变化后重建滤镜图，支持播放中动态切换）
    void setSpeed(double speed);

    // 当前播放位置（秒）：最近写入帧 PTS - 未消费时长（已按 speed 换算回媒体时间）
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

    // 将 S16 数据写入环形缓冲（带 serial 校验，串行背压）；成功返回 0，帧被丢弃返回 -1
    int writeToRing(const uint8_t* src, int len, int serial, double pts);

    // 变速路径（仅音频线程调用）：FLTP → atempo → S16 → ring buffer
    int processFrameTempo(AVFrame* frame, int serial);
    // 创建/销毁 atempo 滤镜图（仅音频线程调用）
    int initFilterGraphLocked();
    void destroyFilterGraphLocked();

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
    SwrContext* m_swrCtx;              // 原速路径：源格式 -> S16（源非 S16 时创建）
    int m_sampleRate;
    int m_channels;

    // ===== 变速相关（滤镜图仅音频线程访问）=====
    AVSampleFormat m_srcFormat;        // 源采样格式（open 时保存）
    AVChannelLayout m_srcLayout;       // 源声道布局（open 时拷贝）
    SwrContext* m_swrToFltp;           // 变速：源格式 -> FLTP（源非 FLTP 时创建）
    SwrContext* m_swrToS16;            // 变速：FLTP -> S16
    AVFilterGraph* m_filterGraph;      // atempo 滤镜图
    AVFilterContext* m_buffersrcCtx;
    AVFilterContext* m_buffersinkCtx;
    AVFilterContext* m_atempoCtx;
    std::atomic<double> m_speed{1.0};  // 目标倍速（控制线程写，音频线程读）
    double m_appliedSpeed{1.0};        // 音频线程已应用的倍速
    // 变速媒体时间游标（仅音频线程访问）：下一输出帧的媒体起始时间，seek/speed 切换后重置
    double m_tempoMediaCursor{0.0};
    int m_tempoLastSerial{-1};         // 上一帧 serial，变化时重置媒体时间游标

    std::thread m_thread;
    std::atomic<bool> m_running{false};
};

} // namespace ccplayer
