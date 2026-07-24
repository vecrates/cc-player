#pragma once

#include "queue/PacketQueue.h"
#include <string>
#include <atomic>
#include <thread>
#include <functional>

extern "C" {
#include <libavformat/avformat.h>
}

namespace ccplayer {

class Demuxer {
public:
    Demuxer();
    ~Demuxer();

    int open(const char* url);
    void close();

    int getVideoStreamIndex() const { return m_videoStreamIndex; }
    int getAudioStreamIndex() const { return m_audioStreamIndex; }
    AVCodecParameters* getVideoCodecPar() const;
    AVCodecParameters* getAudioCodecPar() const;
    AVRational getVideoTimeBase() const;
    AVRational getAudioTimeBase() const;
    int64_t getDuration() const;

    void setPacketQueues(PacketQueue* videoQueue, PacketQueue* audioQueue);

    void startReading();
    void stopReading();
    void flush();

    int seekTo(int64_t positionMs);

    using ProgressCallback = std::function<void(int64_t currentMs, int64_t durationMs)>;
    void setProgressCallback(ProgressCallback cb);

private:
    void readLoop();

    AVFormatContext* m_fmtCtx;
    int m_videoStreamIndex;
    int m_audioStreamIndex;

    PacketQueue* m_videoQueue;
    PacketQueue* m_audioQueue;

    std::thread m_readThread;
    std::atomic<bool> m_running;
    std::atomic<bool> m_abort;

    ProgressCallback m_progressCb;
};

} // namespace ccplayer
