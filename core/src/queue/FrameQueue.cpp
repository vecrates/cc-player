#include "FrameQueue.h"
#include "platform/log.h"

extern "C" {
#include <libavutil/frame.h>
}

#define TAG "FrameQueue"

namespace ccplayer {

FrameQueue::FrameQueue(int maxSize)
    : m_head(nullptr)
    , m_tail(nullptr)
    , m_count(0)
    , m_maxSize(maxSize)
    , m_abort(false)
{
}

FrameQueue::~FrameQueue() {
    flush();
}

int FrameQueue::pushVideoFrame(AVFrame* frame, double pts) {
    std::unique_lock<std::mutex> lock(m_mutex);

    while (m_count >= m_maxSize && !m_abort) {
        m_cond.wait(lock);
    }
    if (m_abort) return -1;

    AVFrame* cloned = av_frame_clone(frame);
    if (!cloned) {
        LOGE(TAG, "Failed to clone video frame");
        return -1;
    }

    auto* node = new FrameNode();
    node->frame = cloned;
    node->pts = pts;
    node->isVideo = true;
    node->sampleRate = 0;
    node->channels = 0;
    node->next = nullptr;

    if (m_tail) {
        m_tail->next = node;
    } else {
        m_head = node;
    }
    m_tail = node;
    m_count++;

    m_cond.notify_one();
    return 0;
}

int FrameQueue::pushAudioFrame(AVFrame* frame, double pts) {
    std::unique_lock<std::mutex> lock(m_mutex);

    while (m_count >= m_maxSize && !m_abort) {
        m_cond.wait(lock);
    }
    if (m_abort) return -1;

    AVFrame* cloned = av_frame_clone(frame);
    if (!cloned) {
        LOGE(TAG, "Failed to clone audio frame");
        return -1;
    }

    auto* node = new FrameNode();
    node->frame = cloned;
    node->pts = pts;
    node->isVideo = false;
    node->sampleRate = frame->sample_rate;
    node->channels = frame->ch_layout.nb_channels;
    node->next = nullptr;

    if (m_tail) {
        m_tail->next = node;
    } else {
        m_head = node;
    }
    m_tail = node;
    m_count++;

    m_cond.notify_one();
    return 0;
}

int FrameQueue::popVideoFrame(VideoFrame* out, bool block) {
    std::unique_lock<std::mutex> lock(m_mutex);

    while (!m_abort) {
        FrameNode* node = m_head;
        while (node && !node->isVideo) {
            node = node->next;
        }
        if (node) break;
        if (!block) return -1;
        m_cond.wait(lock);
    }

    if (m_abort) return -1;

    FrameNode* prev = nullptr;
    FrameNode* node = m_head;
    while (node && !node->isVideo) {
        prev = node;
        node = node->next;
    }
    if (!node) return -1;

    if (prev) {
        prev->next = node->next;
    } else {
        m_head = node->next;
    }
    if (m_tail == node) {
        m_tail = prev;
    }
    m_count--;

    out->frame = node->frame;
    out->pts = node->pts;
    out->width = node->frame->width;
    out->height = node->frame->height;
    out->format = node->frame->format;
    delete node;

    m_cond.notify_one();
    return 0;
}

int FrameQueue::popAudioFrame(AudioFrame* out, bool block) {
    std::unique_lock<std::mutex> lock(m_mutex);

    while (!m_abort) {
        FrameNode* node = m_head;
        while (node && node->isVideo) {
            node = node->next;
        }
        if (node) break;
        if (!block) return -1;
        m_cond.wait(lock);
    }

    if (m_abort) return -1;

    FrameNode* prev = nullptr;
    FrameNode* node = m_head;
    while (node && node->isVideo) {
        prev = node;
        node = node->next;
    }
    if (!node) return -1;

    if (prev) {
        prev->next = node->next;
    } else {
        m_head = node->next;
    }
    if (m_tail == node) {
        m_tail = prev;
    }
    m_count--;

    out->frame = node->frame;
    out->pts = node->pts;
    out->sampleRate = node->sampleRate;
    out->channels = node->channels;
    delete node;

    m_cond.notify_one();
    return 0;
}

void FrameQueue::flush() {
    std::unique_lock<std::mutex> lock(m_mutex);

    while (m_head) {
        FrameNode* node = m_head;
        m_head = node->next;
        av_frame_free(&node->frame);
        delete node;
    }
    m_tail = nullptr;
    m_count = 0;

    m_cond.notify_all();
}

void FrameQueue::abort() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_abort = true;
    m_cond.notify_all();
}

int FrameQueue::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count;
}

} // namespace ccplayer
