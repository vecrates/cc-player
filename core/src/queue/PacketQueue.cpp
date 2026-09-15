#include "PacketQueue.h"
#include "platform/log.h"
#include <cstdlib>
#include <chrono>

extern "C" {
#include <libavutil/mem.h>
}

#define TAG "PacketQueue"

namespace ccplayer {

static const int DEFAULT_MAX_BYTE_SIZE = 1 * 1024 * 1024; // 15MB

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

int PacketQueue::push(AVPacket* pkt, int serial) {
    AVPacket* cloned = av_packet_clone(pkt);
    if (!cloned) {
        LOGE(TAG, "Failed to clone packet");
        return -1;
    }

    auto* node = new PacketNode();
    node->pkt = cloned;
    node->serial = serial;
    node->next = nullptr;

    std::unique_lock<std::mutex> lock(m_mutex);

    // 背压：队列非空且累计字节数超限时阻塞，等待消费方 pop 腾出空间；
    // 用带超时的 wait_for 轮询，确保中断标志（seek/pause 置位 m_interrupt）能在无 notify 的情况下
    // 也能被及时感知（与 Decoder::popTimeout 对称），避免生产者永久阻塞无法响应控制命令
    while (m_byteSize > 0 && m_byteSize >= m_maxByteSize && !m_abort
           && !(m_interrupt && m_interrupt->load())) {
        m_cond.wait_for(lock, std::chrono::milliseconds(20));
    }

    if (m_abort || (m_interrupt && m_interrupt->load())) {
        // 队列已终止或被打断，释放资源；未入队，生产者应回退检查命令
        delete node;
        av_packet_free(&cloned);
        return m_abort ? -1 : -2;
    }
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

int PacketQueue::pop(AVPacket* pkt, int* serialOut, bool block) {
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

    if (serialOut) *serialOut = node->serial;
    av_packet_move_ref(pkt, node->pkt);
    av_packet_free(&node->pkt);
    delete node;

    m_cond.notify_one();
    return 0;
}

int PacketQueue::popTimeout(AVPacket* pkt, int* serialOut, int timeoutMs) {
    std::unique_lock<std::mutex> lock(m_mutex);

    while (!m_head && !m_abort) {
        if (m_cond.wait_for(lock, std::chrono::milliseconds(timeoutMs)) == std::cv_status::timeout) {
            return -2; // 超时，队列仍为空
        }
    }

    if (m_abort) return -1;

    PacketNode* node = m_head;
    m_head = node->next;
    if (!m_head) m_tail = nullptr;
    m_count--;
    m_byteSize -= node->pkt->size;

    if (serialOut) *serialOut = node->serial;
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

void PacketQueue::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_abort = false;
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

void PacketQueue::setInterrupt(const std::atomic<bool>* flag) {
    // 指针仅在生产/消费线程之外配置（prepare 阶段），无需加锁
    m_interrupt = flag;
}

} // namespace ccplayer
