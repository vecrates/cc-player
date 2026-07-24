#include "PlayerImpl.h"
#include <cstring>

extern "C" {
#include <libswresample/swresample.h>
}

#define TAG "Player"

namespace ccplayer {

static const int VIDEO_FRAME_QUEUE_SIZE = 10;
static const int AUDIO_FRAME_QUEUE_SIZE = 5;

PlayerImpl::PlayerImpl()
    : m_state(PlayerState::Idle)
    , m_videoFrameQueue(VIDEO_FRAME_QUEUE_SIZE)
    , m_audioFrameQueue(AUDIO_FRAME_QUEUE_SIZE)
    , m_audioOutput(nullptr)
    , m_nativeWindow(nullptr)
    , m_renderRunning(false)
    , m_audioRunning(false)
    , m_audioSampleRate(0)
    , m_audioChannels(0)
    , m_ringBuffer(nullptr)
    , m_swrCtx(nullptr)
    , m_userData(nullptr)
    , m_eglDisplay(EGL_NO_DISPLAY)
    , m_eglContext(EGL_NO_CONTEXT)
    , m_eglSurface(EGL_NO_SURFACE)
    , m_eglInitialized(false)
    , m_surfaceWidth(0)
    , m_surfaceHeight(0)
{
    memset(&m_callbacks, 0, sizeof(m_callbacks));
    m_syncer.setMasterClock(&m_audioClock);
}

PlayerImpl::~PlayerImpl() {
    release();
}

void PlayerImpl::setDataSource(const char* path) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_state != PlayerState::Idle && m_state != PlayerState::Stopped) {
        LOGW(TAG, "setDataSource called in state %d", (int)m_state);
        return;
    }
    m_dataSource = path;
    m_state = PlayerState::Initialized;
    LOGI(TAG, "Data source set: %s", path);
}

void PlayerImpl::setSurface(void* nativeWindow) {
    m_nativeWindow = nativeWindow;
}

void PlayerImpl::setSurfaceSize(int width, int height) {
    m_surfaceWidth = width;
    m_surfaceHeight = height;
}

void PlayerImpl::prepare() {
    enum class CallbackToNotify {
        None,
        Prepared,
        Error
    };

    CallbackToNotify callbackToNotify = CallbackToNotify::None;
    PlayerCallbacks callbacks{};
    void* userData = nullptr;
    int errorCode = 0;

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_state != PlayerState::Initialized) {
            LOGW(TAG, "prepare called in state %d", (int)m_state);
            return;
        }

        if (m_demuxer.open(m_dataSource.c_str()) < 0) {
            m_state = PlayerState::Error;
            callbacks = m_callbacks;
            userData = m_userData;
            errorCode = (int)PlayerError::InvalidDataSource;
            callbackToNotify = CallbackToNotify::Error;
        } else {
            if (m_demuxer.getVideoStreamIndex() >= 0) {
                auto* par = m_demuxer.getVideoCodecPar();
                auto tb = m_demuxer.getVideoTimeBase();
                if (m_videoDecoder.openVideo(par, tb) < 0) {
                    LOGE(TAG, "Failed to open video decoder");
                }
            }

            if (m_demuxer.getAudioStreamIndex() >= 0) {
                auto* par = m_demuxer.getAudioCodecPar();
                auto tb = m_demuxer.getAudioTimeBase();
                if (m_audioDecoder.openAudio(par, tb) < 0) {
                    LOGE(TAG, "Failed to open audio decoder");
                } else {
                    m_audioSampleRate = par->sample_rate;
                    m_audioChannels = par->ch_layout.nb_channels;

                    m_ringBuffer = new AudioRingBuffer();

                    AVSampleFormat srcFmt = (AVSampleFormat)par->format;
                    if (srcFmt != AV_SAMPLE_FMT_S16) {
                        int ret = swr_alloc_set_opts2(&m_swrCtx,
                            &par->ch_layout, AV_SAMPLE_FMT_S16, m_audioSampleRate,
                            &par->ch_layout, srcFmt, m_audioSampleRate,
                            0, nullptr);
                        if (ret < 0 || !m_swrCtx || swr_init(m_swrCtx) < 0) {
                            LOGE(TAG, "Failed to init SwrContext");
                            if (m_swrCtx) swr_free(&m_swrCtx);
                        }
                    }

                    auto* aaudio = new AAudioOutput();
                    if (aaudio->open(m_audioSampleRate, m_audioChannels) == 0) {
                        m_audioOutput = aaudio;
                        LOGI(TAG, "AAudio output opened");
                    } else {
                        LOGE(TAG, "Failed to open AAudio output");
                        delete aaudio;
                    }
                }
            }

            m_demuxer.setPacketQueues(&m_videoPacketQueue, &m_audioPacketQueue);
            m_videoDecoder.setPacketQueue(&m_videoPacketQueue);
            m_videoDecoder.setFrameQueue(&m_videoFrameQueue);
            m_audioDecoder.setPacketQueue(&m_audioPacketQueue);
            m_audioDecoder.setFrameQueue(&m_audioFrameQueue);

            m_state = PlayerState::Prepared;
            LOGI(TAG, "Prepared, duration=%lld ms", (long long)getDuration());

            callbacks = m_callbacks;
            userData = m_userData;
            callbackToNotify = CallbackToNotify::Prepared;
        }
    }

    if (callbackToNotify == CallbackToNotify::Prepared && callbacks.onPrepared) {
        callbacks.onPrepared(userData);
    } else if (callbackToNotify == CallbackToNotify::Error && callbacks.onError) {
        callbacks.onError(errorCode, userData);
    }
}

void PlayerImpl::prepareAsync() {
    std::thread(&PlayerImpl::prepareAsyncThread, this).detach();
}

void PlayerImpl::prepareAsyncThread() {
    prepare();
}

void PlayerImpl::start() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_state != PlayerState::Prepared && m_state != PlayerState::Paused) {
        LOGW(TAG, "start called in state %d", (int)m_state);
        return;
    }

    m_demuxer.startReading();
    if (m_demuxer.getVideoStreamIndex() >= 0) {
        m_videoDecoder.startDecoding();
    }
    if (m_demuxer.getAudioStreamIndex() >= 0) {
        m_audioDecoder.startDecoding();
    }

    m_renderRunning = true;
    m_renderThread = std::thread(&PlayerImpl::renderLoop, this);

    if (m_audioOutput) {
        m_audioOutput->setCallback([this](uint8_t* buffer, int size) -> int {
            return m_ringBuffer ? m_ringBuffer->read(buffer, size) : 0;
        });
        m_audioRunning = true;
        m_audioThread = std::thread(&PlayerImpl::audioLoop, this);
        m_audioOutput->resume();
    }

    m_state = PlayerState::Started;
    LOGI(TAG, "Started");
}

void PlayerImpl::pause() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_state != PlayerState::Started) return;

    m_renderRunning = false;
    if (m_renderThread.joinable()) {
        m_renderThread.join();
    }

    m_videoDecoder.stopDecoding();
    m_audioDecoder.stopDecoding();
    m_demuxer.stopReading();

    m_audioRunning = false;
    if (m_audioThread.joinable()) {
        m_audioThread.join();
    }
    if (m_audioOutput) m_audioOutput->pause();

    m_state = PlayerState::Paused;
    LOGI(TAG, "Paused");
}

void PlayerImpl::resume() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_state != PlayerState::Paused) return;

    m_demuxer.startReading();
    if (m_demuxer.getVideoStreamIndex() >= 0) {
        m_videoDecoder.startDecoding();
    }
    if (m_demuxer.getAudioStreamIndex() >= 0) {
        m_audioDecoder.startDecoding();
    }

    m_renderRunning = true;
    m_renderThread = std::thread(&PlayerImpl::renderLoop, this);

    if (m_audioOutput) {
        m_audioRunning = true;
        m_audioThread = std::thread(&PlayerImpl::audioLoop, this);
        m_audioOutput->resume();
    }

    m_state = PlayerState::Started;
    LOGI(TAG, "Resumed");
}

void PlayerImpl::stop() {
    std::lock_guard<std::mutex> lock(m_stateMutex);

    m_renderRunning = false;
    if (m_renderThread.joinable()) {
        m_renderThread.join();
    }

    m_audioRunning = false;
    if (m_audioThread.joinable()) {
        m_audioThread.join();
    }

    m_videoDecoder.stopDecoding();
    m_audioDecoder.stopDecoding();
    m_demuxer.stopReading();

    m_videoPacketQueue.abort();
    m_audioPacketQueue.abort();
    m_videoFrameQueue.abort();
    m_audioFrameQueue.abort();

    m_videoPacketQueue.flush();
    m_audioPacketQueue.flush();
    m_videoFrameQueue.flush();
    m_audioFrameQueue.flush();

    if (m_audioOutput) {
        m_audioOutput->close();
        delete m_audioOutput;
        m_audioOutput = nullptr;
    }

    delete m_ringBuffer;
    m_ringBuffer = nullptr;
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
    }

    m_videoOutput.destroy();
    m_audioClock.reset();

    m_state = PlayerState::Stopped;
    LOGI(TAG, "Stopped");
}

void PlayerImpl::release() {
    stop();
    m_demuxer.close();
    m_videoDecoder.close();
    m_audioDecoder.close();
    m_state = PlayerState::Idle;
}

void PlayerImpl::seekTo(int64_t positionMs) {
    if (m_state != PlayerState::Started && m_state != PlayerState::Paused) return;

    m_demuxer.seekTo(positionMs);
    m_videoDecoder.flush();
    m_audioDecoder.flush();
    m_videoFrameQueue.flush();
    m_audioFrameQueue.flush();
    if (m_ringBuffer) m_ringBuffer->reset();

    if (m_callbacks.onSeekComplete) {
        m_callbacks.onSeekComplete(m_userData);
    }
    LOGI(TAG, "Seeked to %lld ms", (long long)positionMs);
}

int64_t PlayerImpl::getCurrentPosition() {
    return (int64_t)(m_audioClock.getPTS() * 1000);
}

int64_t PlayerImpl::getDuration() {
    return m_demuxer.getDuration();
}

void PlayerImpl::setCallbacks(const PlayerCallbacks& callbacks) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_callbacks = callbacks;
}

void PlayerImpl::setUserData(void* userData) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_userData = userData;
}

void PlayerImpl::renderLoop() {
    setupEGL();
    if (!m_eglInitialized) {
        LOGE(TAG, "EGL setup failed, cannot render video");
        return;
    }

    m_videoOutput.init();

    if (m_surfaceWidth > 0 && m_surfaceHeight > 0) {
        m_videoOutput.setSurfaceSize(m_surfaceWidth, m_surfaceHeight);
    }

    VideoFrame videoFrame;

    while (m_renderRunning) {
        if (m_videoFrameQueue.popVideoFrame(&videoFrame, true) < 0) {
            if (!m_renderRunning) break;
            continue;
        }

        double delay = m_syncer.computeVideoDelay(videoFrame.pts);

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

        av_frame_free(&videoFrame.frame);

        if (m_callbacks.onProgress) {
            m_callbacks.onProgress(getCurrentPosition(), getDuration(), m_userData);
        }
    }

    m_videoOutput.destroy();
    teardownEGL();
}

void PlayerImpl::audioLoop() {
    AudioFrame audioFrame;

    while (m_audioRunning) {
        if (m_audioFrameQueue.popAudioFrame(&audioFrame, true) < 0) {
            if (!m_audioRunning) break;
            continue;
        }

        const uint8_t* srcData;
        int srcSize;

        if (m_swrCtx && audioFrame.frame->format != AV_SAMPLE_FMT_S16) {
            int nbSamples = audioFrame.frame->nb_samples;
            int outSamples = swr_get_out_samples(m_swrCtx, nbSamples);
            int bufSize = outSamples * m_audioChannels * 2;
            auto* buf = new uint8_t[bufSize];

            int converted = swr_convert(m_swrCtx,
                &buf, outSamples,
                (const uint8_t**)audioFrame.frame->data, nbSamples);
            if (converted > 0) {
                srcData = buf;
                srcSize = converted * m_audioChannels * 2;
            } else {
                delete[] buf;
                av_frame_free(&audioFrame.frame);
                continue;
            }
        } else {
            srcData = audioFrame.frame->data[0];
            srcSize = audioFrame.frame->nb_samples * m_audioChannels * 2;
        }

        int written = 0;
        while (written < srcSize && m_audioRunning) {
            int n = m_ringBuffer->write(srcData + written, srcSize - written);
            if (n > 0) {
                written += n;
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        }

        m_audioClock.setPTS(audioFrame.pts);

        if (m_swrCtx && audioFrame.frame->format != AV_SAMPLE_FMT_S16) {
            delete[] srcData;
        }
        av_frame_free(&audioFrame.frame);
    }
}

void PlayerImpl::setupEGL() {
    if (!m_nativeWindow) {
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

    m_eglSurface = eglCreateWindowSurface(m_eglDisplay, config, (EGLNativeWindowType)m_nativeWindow, nullptr);
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
    LOGI(TAG, "EGL context created (%dx%d)", major, minor);
}

void PlayerImpl::teardownEGL() {
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
