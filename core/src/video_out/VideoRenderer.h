#pragma once

#include "queue/FrameQueue.h"
#include "sync/Sync.h"
#include "video_out/VideoOutput.h"
#include "platform/log.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>

namespace ccplayer {

/**
 * 视频渲染器（纯线程封装）：自管渲染线程与 EGL 生命周期。
 * 从视频帧队列取帧，按 serial 丢弃旧 seek 代际、按 seek 目标丢弃关键帧之前的帧，
 * 以音频主时钟同步后渲染上屏，并周期性上报进度。
 */
class VideoRenderer {
public:
    using ProgressCallback = std::function<void()>;

    VideoRenderer(FrameQueue* frameQueue, AudioVideoSyncer* syncer);
    ~VideoRenderer();

    void setSurface(void* nativeWindow);
    void setSurfaceSize(int width, int height);
    // 设置视频显示宽高（含旋转校正），用于渲染层 letterbox 画面适配
    void setVideoSize(int width, int height);
    void setProgressCallback(ProgressCallback cb);

    // 启动渲染线程（帧队列需已 reset）
    void start();
    // 停止渲染线程（同步 join；帧队列应已 abort 以唤醒阻塞 pop）
    void stop();
    // 更新 seek 代际与目标：丢弃旧代际帧与目标之前的帧
    void seek(int serial, double targetSec);
    // 设置播放倍速（透传视频时钟，无音频流时作为主时钟）
    void setSpeed(double speed);
    // 视频渲染位置（秒）：无音频流时作为主时钟
    double getCurrentPts();

private:
    void renderLoop();
    void setupEGL();
    void teardownEGL();

    FrameQueue* m_frameQueue;
    AudioVideoSyncer* m_syncer;

    std::mutex m_mutex;              // 保护 surface / callback 配置
    void* m_nativeWindow;
    int m_surfaceWidth;
    int m_surfaceHeight;
    int m_videoWidth;
    int m_videoHeight;
    ProgressCallback m_progressCb;

    std::atomic<int> m_serial{0};         // 当前 seek 代际
    std::atomic<double> m_seekTarget{0.0}; // seek 目标（秒）

    Clock m_videoClock;                    // 视频时钟（无音频流时作为主时钟）

    std::thread m_thread;
    std::atomic<bool> m_running{false};

    VideoOutput m_videoOutput;
    EGLDisplay m_eglDisplay;
    EGLContext m_eglContext;
    EGLSurface m_eglSurface;
    bool m_eglInitialized;
};

} // namespace ccplayer
