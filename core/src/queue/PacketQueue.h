#pragma once

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace ccplayer {

struct PacketNode {
    AVPacket* pkt;
    int serial;   // 数据所属的 seek 代际，消费时校验丢弃旧代际
    PacketNode* next;
};

class PacketQueue {
public:
    PacketQueue();
    ~PacketQueue();

    int push(AVPacket* pkt, int serial);
    int pop(AVPacket* pkt, int* serialOut, bool block);
    // 阻塞最多 timeoutMs 毫秒；超时且队列仍空时返回 -2（用于解码线程周期性检查命令）
    int popTimeout(AVPacket* pkt, int* serialOut, int timeoutMs);
    void flush();
    void abort();
    // 清除 abort 标志（保留数据），供 stop 后重新 prepare 复用队列
    void reset();

    int size() const;
    int byteSize() const;

    void setMaxByteSize(int maxBytes);

    // 设置外部中断标志：push 背压等待时若该标志置位则立即返回（供 seek/pause 打断）
    void setInterrupt(const std::atomic<bool>* flag);

private:
    PacketNode* m_head;
    PacketNode* m_tail;
    int m_count;
    int m_byteSize;
    int m_maxByteSize;
    bool m_abort;
    const std::atomic<bool>* m_interrupt = nullptr;

    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
};

} // namespace ccplayer
