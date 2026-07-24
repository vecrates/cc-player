#include "Player.h"
#include "demuxer/Demuxer.h"
#include "decoder/Decoder.h"
#include "queue/PacketQueue.h"
#include "queue/FrameQueue.h"
#include "sync/Sync.h"
#include "audio_out/AudioOutput.h"
#include "audio_out/AAudioOutput.h"
#include "video_out/VideoOutput.h"
#include "platform/log.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>

extern "C" {
#include <libswresample/swresample.h>
}

#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cstring>

#define TAG "Player"

namespace ccplayer {

class PlayerImpl {
public:
    PlayerImpl();
    ~PlayerImpl();

    void setDataSource(const char* path);
    void setSurface(void* nativeWindow);
    void setSurfaceSize(int width, int height);

    void prepare();
    void prepareAsync();
    void start();
    void pause();
    void resume();
    void stop();
    void release();
    void seekTo(int64_t positionMs);

    int64_t getCurrentPosition();
    int64_t getDuration();
    PlayerState getState() const { return m_state; }

    void setCallbacks(const PlayerCallbacks& callbacks);
    void setUserData(void* userData);

private:
    void prepareAsyncThread();
    void renderLoop();
    void audioLoop();
    void setupEGL();
    void teardownEGL();

    PlayerState m_state;
    std::mutex m_stateMutex;

    Demuxer m_demuxer;
    Decoder m_videoDecoder;
    Decoder m_audioDecoder;

    PacketQueue m_videoPacketQueue;
    PacketQueue m_audioPacketQueue;
    FrameQueue m_videoFrameQueue;
    FrameQueue m_audioFrameQueue;

    Clock m_audioClock;
    AudioVideoSyncer m_syncer;
    VideoOutput m_videoOutput;

    AudioOutput* m_audioOutput;

    std::string m_dataSource;
    void* m_nativeWindow;
    int m_surfaceWidth;
    int m_surfaceHeight;

    std::thread m_renderThread;
    std::atomic<bool> m_renderRunning;

    std::thread m_audioThread;
    std::atomic<bool> m_audioRunning;
    int m_audioSampleRate;
    int m_audioChannels;

    struct AudioRingBuffer {
        static constexpr int SIZE = 65536;
        uint8_t data[SIZE];
        std::atomic<int> writePos{0};
        std::atomic<int> readPos{0};

        int writable() const {
            int r = readPos.load(std::memory_order_acquire);
            int w = writePos.load(std::memory_order_relaxed);
            return SIZE - 1 - ((w - r + SIZE) % SIZE);
        }
        int write(const uint8_t* src, int len) {
            int avail = writable();
            if (len > avail) len = avail;
            if (len <= 0) return 0;
            int w = writePos.load(std::memory_order_relaxed);
            int first = std::min(len, SIZE - w);
            memcpy(data + w, src, first);
            if (len > first) memcpy(data, src + first, len - first);
            writePos.store((w + len) % SIZE, std::memory_order_release);
            return len;
        }
        int read(uint8_t* dst, int len) {
            int w = writePos.load(std::memory_order_acquire);
            int r = readPos.load(std::memory_order_relaxed);
            int avail = (w - r + SIZE) % SIZE;
            if (len > avail) len = avail;
            if (len <= 0) return 0;
            int first = std::min(len, SIZE - r);
            memcpy(dst, data + r, first);
            if (len > first) memcpy(dst + first, data, len - first);
            readPos.store((r + len) % SIZE, std::memory_order_release);
            return len;
        }
        void reset() {
            readPos.store(0, std::memory_order_relaxed);
            writePos.store(0, std::memory_order_relaxed);
        }
    };
    AudioRingBuffer* m_ringBuffer;
    SwrContext* m_swrCtx;

    PlayerCallbacks m_callbacks;
    void* m_userData;

    EGLDisplay m_eglDisplay;
    EGLContext m_eglContext;
    EGLSurface m_eglSurface;
    bool m_eglInitialized;
};

} // namespace ccplayer
