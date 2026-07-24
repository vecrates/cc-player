#include "PacketQueue.h"
#include "platform/log.h"
#include <cstdlib>

extern "C" {
#include <libavutil/mem.h>
}

#define TAG "PacketQueue"

namespace ccplayer {

static const int DEFAULT_MAX_BYTE_SIZE = 15 * 1024 * 1024; // 15MB

PacketQueue::PacketQueue()
    : m_head(nullptr)
    , m_tail(nullptr)
    , m_count(0)
    , m_byteSize(0)
    , m_maxByteSize(DEFAULT_MAX_BYTE_SIZE)
    , m_abort(false)
{
}

PacketQueue::~PacketQueue() {
    flush();
}

int PacketQueue::push(AVPacket* pkt) {
    if (m_abort) return -1;

    AVPacket* cloned = av_packet_clone(pkt);
    if (!cloned) {
        LOGE(TAG, "Failed to clone packet");
        return -1;
    }

    auto* node = new PacketNode();
    node->pkt = cloned;
    node->next = nullptr;

    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_tail) {
        m_tail->next = node;
    } else {
        m_head = node;
    }
    m_tail = node;
    m_count++;
    m_byteSize += cloned->size;

    m_cond.notify_one();
    return 0;
}

int PacketQueue::pop(AVPacket* pkt, bool block) {
    std::unique_lock<std::mutex> lock(m_mutex);

    while (!m_head && !m_abort) {
        if (!block) return -1;
        m_cond.wait(lock);
    }

    if (m_abort) return -1;

    PacketNode* node = m_head;
    m_head = node->next;
    if (!m_head) m_tail = nullptr;
    m_count--;
    m_byteSize -= node->pkt->size;

    av_packet_move_ref(pkt, node->pkt);
    av_packet_free(&node->pkt);
    delete node;

    m_cond.notify_one();
    return 0;
}

void PacketQueue::flush() {
    std::unique_lock<std::mutex> lock(m_mutex);

    while (m_head) {
        PacketNode* node = m_head;
        m_head = node->next;
        av_packet_free(&node->pkt);
        delete node;
    }
    m_tail = nullptr;
    m_count = 0;
    m_byteSize = 0;

    m_cond.notify_all();
}

void PacketQueue::abort() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_abort = true;
    m_cond.notify_all();
}

int PacketQueue::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count;
}

int PacketQueue::byteSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_byteSize;
}

void PacketQueue::setMaxByteSize(int maxBytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxByteSize = maxBytes;
}

} // namespace ccplayer
