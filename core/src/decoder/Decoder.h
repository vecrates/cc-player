#pragma once

#include "queue/PacketQueue.h"
#include "queue/FrameQueue.h"
#include "queue/CommandQueue.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace ccplayer {

/**
 * Decoder 内部命令（对称 Actor 化）：flush 等命令投递到解码线程自身执行，
 * 避免控制线程直接调用 avcodec_flush_buffers 与解码线程 send/receive 并发。
 */
enum class DecoderCommandType {
    Start,   // 开始解码（线程不存在则创建）
    Pause,   // 暂停解码（线程挂起，不退出）
    Resume,  // 恢复解码
    Flush,   // 清空解码器内部缓冲（avcodec_flush_buffers）
    Stop,    // 停止解码并退出线程
};

struct DecoderCommand {
    DecoderCommandType type;
    int64_t arg; // 预留，当前未使用
};

class Decoder {
public:
    Decoder();
    ~Decoder();

    int openVideo(AVCodecParameters* codecPar, AVRational timeBase);
    int openAudio(AVCodecParameters* codecPar, AVRational timeBase);
    void close();

    void setPacketQueue(PacketQueue* pktQueue);
    void setFrameQueue(FrameQueue* frameQueue);

    // 控制接口（post 命令，异步返回）
    void start();
    void pause();
    void resume();
    // flush 同步等待完成（在解码线程执行 avcodec_flush_buffers），供 seek 协调时序
    void flush();
    // stop 同步：投递 Stop 命令并 join 解码线程
    void stop();

    bool isVideo() const { return m_isVideo; }

private:
    void decodeLoop();
    void handleCommand(const DecoderCommand& cmd);

    AVCodecContext* m_codecCtx;
    AVRational m_timeBase;
    bool m_isVideo;

    PacketQueue* m_packetQueue;
    FrameQueue* m_frameQueue;

    std::thread m_thread;
    CommandQueue<DecoderCommand> m_cmdQueue;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};

    // flush 同步等待：解码线程执行完 Flush 后置位并通知
    std::mutex m_flushMutex;
    std::condition_variable m_flushCond;
    std::atomic<bool> m_flushDone{false};
};

} // namespace ccplayer
