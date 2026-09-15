#include "VideoRenderer.h"
#include <utility>

extern "C" {
#include <libavutil/frame.h>
}

#define TAG "VideoRenderer"

namespace ccplayer {

// 进度回调节流间隔
static const auto PROGRESS_INTERVAL = std::chrono::milliseconds(200);

VideoRenderer::VideoRenderer(FrameQueue* frameQueue, AudioVideoSyncer* syncer)
    : m_frameQueue(frameQueue)
    , m_syncer(syncer)
    , m_nativeWindow(nullptr)
    , m_surfaceWidth(0)
    , m_surfaceHeight(0)
    , m_videoWidth(0)
    , m_videoHeight(0)
    , m_eglDisplay(EGL_NO_DISPLAY)
    , m_eglContext(EGL_NO_CONTEXT)
    , m_eglSurface(EGL_NO_SURFACE)
    , m_eglInitialized(false)
{
}

VideoRenderer::~VideoRenderer() {
    stop();
}

void VideoRenderer::setSurface(void* nativeWindow) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nativeWindow = nativeWindow;
}

void VideoRenderer::setSurfaceSize(int width, int height) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_surfaceWidth = width;
    m_surfaceHeight = height;
}

void VideoRenderer::setVideoSize(int width, int height) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_videoWidth = width;
    m_videoHeight = height;
}

void VideoRenderer::setProgressCallback(ProgressCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_progressCb = std::move(cb);
}

void VideoRenderer::start() {
    if (m_running.load() || m_thread.joinable()) return;
    m_running = true;
    m_thread = std::thread(&VideoRenderer::renderLoop, this);
}

void VideoRenderer::stop() {
    if (!m_thread.joinable()) return;
    m_running = false;
    // 渲染线程阻塞在 pop 时需由外部 abort 帧队列唤醒（见 PlayerImpl::doStop）
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void VideoRenderer::seek(int serial, double targetSec) {
    m_serial = serial;
    m_seekTarget = targetSec;
    m_videoClock.setPTS(targetSec); // 视频时钟跳到目标位置
}

void VideoRenderer::setSpeed(double speed) {
    m_videoClock.setSpeed(speed);
}

double VideoRenderer::getCurrentPts() {
    return m_videoClock.getPTS();
}

void VideoRenderer::renderLoop() {
    setupEGL();
    if (!m_eglInitialized) {
        LOGE(TAG, "EGL setup failed, cannot render video");
        return;
    }

    m_videoOutput.init();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_surfaceWidth > 0 && m_surfaceHeight > 0) {
            m_videoOutput.setSurfaceSize(m_surfaceWidth, m_surfaceHeight);
        }
        if (m_videoWidth > 0 && m_videoHeight > 0) {
            m_videoOutput.setVideoSize(m_videoWidth, m_videoHeight);
        }
    }

    VideoFrame videoFrame;
    auto lastProgress = std::chrono::steady_clock::now();

    while (m_running) {
        if (m_frameQueue->popVideoFrame(&videoFrame, true) < 0) {
            if (!m_running) break;
            continue;
        }

        // 丢弃旧 seek 代际的帧
        if (videoFrame.serial != m_serial.load()) {
            av_frame_free(&videoFrame.frame);
            continue;
        }

        // 丢弃 seek 目标之前的帧（BACKWARD 定位的关键帧到目标之间）
        if (videoFrame.pts < m_seekTarget.load()) {
            av_frame_free(&videoFrame.frame);
            continue;
        }

        // 丢帧追赶：视频落后主时钟过多时丢弃该帧
        if (m_syncer->shouldDrop(videoFrame.pts)) {
            av_frame_free(&videoFrame.frame);
            continue;
        }

        double delay = m_syncer->computeVideoDelay(videoFrame.pts);
        if (delay > 0) {
            std::this_thread::sleep_for(
                std::chrono::microseconds((int64_t)(delay * 1000000)));
        }

        if (m_videoOutput.renderFrame(&videoFrame) == 0) {
            if (!eglSwapBuffers(m_eglDisplay, m_eglSurface)) {
                LOGE(TAG, "eglSwapBuffers failed: 0x%x", eglGetError());
            }
        } else {
            LOGE(TAG, "Failed to render video frame");
        }

        // 更新视频时钟（无音频流时作为主时钟）
        m_videoClock.setPTS(videoFrame.pts);

        av_frame_free(&videoFrame.frame);

        // 进度回调节流
        auto now = std::chrono::steady_clock::now();
        if (now - lastProgress >= PROGRESS_INTERVAL) {
            lastProgress = now;
            ProgressCallback cb;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                cb = m_progressCb;
            }
            if (cb) cb();
        }
    }

    m_videoOutput.destroy();
    teardownEGL();
}

void VideoRenderer::setupEGL() {
    void* window;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        window = m_nativeWindow;
    }
    if (!window) {
        LOGE(TAG, "No native window for EGL");
        return;
    }

    m_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_eglDisplay == EGL_NO_DISPLAY) {
        LOGE(TAG, "eglGetDisplay failed");
        return;
    }

    EGLint major, minor;
    if (!eglInitialize(m_eglDisplay, &major, &minor)) {
        LOGE(TAG, "eglInitialize failed");
        return;
    }

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(m_eglDisplay, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        LOGE(TAG, "eglChooseConfig failed");
        eglTerminate(m_eglDisplay);
        m_eglDisplay = EGL_NO_DISPLAY;
        return;
    }

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    m_eglContext = eglCreateContext(m_eglDisplay, config, EGL_NO_CONTEXT, contextAttribs);
    if (m_eglContext == EGL_NO_CONTEXT) {
        LOGE(TAG, "eglCreateContext failed");
        eglTerminate(m_eglDisplay);
        m_eglDisplay = EGL_NO_DISPLAY;
        return;
    }

    m_eglSurface = eglCreateWindowSurface(m_eglDisplay, config, (EGLNativeWindowType)window, nullptr);
    if (m_eglSurface == EGL_NO_SURFACE) {
        LOGE(TAG, "eglCreateWindowSurface failed");
        eglDestroyContext(m_eglDisplay, m_eglContext);
        eglTerminate(m_eglDisplay);
        m_eglDisplay = EGL_NO_DISPLAY;
        m_eglContext = EGL_NO_CONTEXT;
        return;
    }

    if (!eglMakeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface, m_eglContext)) {
        LOGE(TAG, "eglMakeCurrent failed");
        eglDestroySurface(m_eglDisplay, m_eglSurface);
        eglDestroyContext(m_eglDisplay, m_eglContext);
        eglTerminate(m_eglDisplay);
        m_eglDisplay = EGL_NO_DISPLAY;
        m_eglContext = EGL_NO_CONTEXT;
        m_eglSurface = EGL_NO_SURFACE;
        return;
    }

    m_eglInitialized = true;
    LOGI(TAG, "EGL context created (%d.%d)", major, minor);
}

void VideoRenderer::teardownEGL() {
    if (!m_eglInitialized) return;

    eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (m_eglSurface != EGL_NO_SURFACE) {
        eglDestroySurface(m_eglDisplay, m_eglSurface);
        m_eglSurface = EGL_NO_SURFACE;
    }
    if (m_eglContext != EGL_NO_CONTEXT) {
        eglDestroyContext(m_eglDisplay, m_eglContext);
        m_eglContext = EGL_NO_CONTEXT;
    }
    if (m_eglDisplay != EGL_NO_DISPLAY) {
        eglTerminate(m_eglDisplay);
        m_eglDisplay = EGL_NO_DISPLAY;
    }

    m_eglInitialized = false;
    LOGI(TAG, "EGL context destroyed");
}

} // namespace ccplayer
