#pragma once

#include "queue/PacketQueue.h"
#include "queue/FrameQueue.h"
#include <atomic>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace ccplayer {

class Decoder {
public:
    Decoder();
    ~Decoder();

    int openVideo(AVCodecParameters* codecPar, AVRational timeBase);
    int openAudio(AVCodecParameters* codecPar, AVRational timeBase);
    void close();

    void setPacketQueue(PacketQueue* pktQueue);
    void setFrameQueue(FrameQueue* frameQueue);

    void startDecoding();
    void stopDecoding();
    void flush();

    bool isVideo() const { return m_isVideo; }

private:
    void decodeLoop();

    AVCodecContext* m_codecCtx;
    AVRational m_timeBase;
    bool m_isVideo;

    PacketQueue* m_packetQueue;
    FrameQueue* m_frameQueue;

    std::thread m_decodeThread;
    std::atomic<bool> m_running;
    std::atomic<bool> m_abort;
};

} // namespace ccplayer
