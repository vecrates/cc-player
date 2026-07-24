#pragma once

#include <mutex>
#include <condition_variable>
#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace ccplayer {

struct PacketNode {
    AVPacket* pkt;
    PacketNode* next;
};

class PacketQueue {
public:
    PacketQueue();
    ~PacketQueue();

    int push(AVPacket* pkt);
    int pop(AVPacket* pkt, bool block);
    void flush();
    void abort();

    int size() const;
    int byteSize() const;

    void setMaxByteSize(int maxBytes);

private:
    PacketNode* m_head;
    PacketNode* m_tail;
    int m_count;
    int m_byteSize;
    int m_maxByteSize;
    bool m_abort;

    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
};

} // namespace ccplayer
