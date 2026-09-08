#pragma once

#include <mutex>
#include <condition_variable>
#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace ccplayer {

struct VideoFrame {
    AVFrame* frame;
    double pts;
    int serial;   // 所属 seek 代际
    int width;
    int height;
    int format;
};

struct AudioFrame {
    AVFrame* frame;
    double pts;
    int serial;   // 所属 seek 代际
    int sampleRate;
    int channels;
};

class FrameQueue {
public:
    FrameQueue(int maxSize);
    ~FrameQueue();

    int pushVideoFrame(AVFrame* frame, double pts, int serial);
    int pushAudioFrame(AVFrame* frame, double pts, int serial);

    int popVideoFrame(VideoFrame* out, bool block);
    int popAudioFrame(AudioFrame* out, bool block);

    void flush();
    void abort();
    // 清除 abort 标志（保留数据），供 pause/resume 复用队列
    void reset();

    int size() const;

private:
    struct FrameNode {
        AVFrame* frame;
        double pts;
        int serial;
        bool isVideo;
        int sampleRate;
        int channels;
        FrameNode* next;
    };

    FrameNode* m_head;
    FrameNode* m_tail;
    int m_count;
    int m_maxSize;
    bool m_abort;

    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
};

} // namespace ccplayer
