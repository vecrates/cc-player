#pragma once

#include <deque>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <utility>

namespace ccplayer {

/**
 * 通用线程安全命令队列（多生产者单消费者），供控制线程 / Demuxer / Decoder 复用。
 *
 * 语义：
 *  - push：投递命令，唤醒一个等待者；abort 后丢弃新命令。
 *  - pop ：阻塞弹出，abort 且队列空时返回 nullopt。
 *  - tryPop：非阻塞弹出，队列空返回 false。
 *  - abort / reset：用于停止时唤醒所有等待者、复用前复位。
 */
template <typename T>
class CommandQueue {
public:
    CommandQueue() = default;
    CommandQueue(const CommandQueue&) = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

    void push(T cmd) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_abort) return;
            m_deque.push_back(std::move(cmd));
        }
        m_cond.notify_one();
    }

    // 阻塞弹出；abort 且无命令时返回 nullopt
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cond.wait(lock, [this] { return m_abort || !m_deque.empty(); });
        if (m_deque.empty()) return std::nullopt;
        T cmd = std::move(m_deque.front());
        m_deque.pop_front();
        return cmd;
    }

    // 非阻塞弹出；无命令返回 false
    bool tryPop(T& out) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_deque.empty()) return false;
        out = std::move(m_deque.front());
        m_deque.pop_front();
        return true;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deque.clear();
    }

    // 终止：唤醒所有等待者，后续 push 被丢弃
    void abort() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_abort = true;
        }
        m_cond.notify_all();
    }

    // 复位 abort 标志（供对象复用）
    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_abort = false;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_deque.empty();
    }

private:
    std::deque<T> m_deque;
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
    bool m_abort = false;
};

} // namespace ccplayer
