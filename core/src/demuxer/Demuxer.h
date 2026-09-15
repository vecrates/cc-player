#pragma once

#include "queue/PacketQueue.h"
#include "queue/CommandQueue.h"
#include "datasource/IDataSource.h"
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <cstdint>

extern "C" {
#include <libavformat/avformat.h>
}

namespace ccplayer {

/**
 * Demuxer 内部命令（Actor 模型）：所有命令由 demuxer 自己的线程串行执行，
 * 从而保证 av_read_frame / av_seek_frame 永不并发触碰 AVFormatContext。
 */
enum class DemuxerCommandType {
    Start,   // 开始读循环（线程不存在则创建）
    Pause,   // 暂停读（线程挂起，不退出）
    Resume,  // 恢复读
    Seek,    // 跳转（arg = 目标毫秒）
    Stop,    // 停止读循环并退出线程
    Close,   // 同 Stop（由 close() 统一 join 后释放 fmtCtx）
};

struct DemuxerCommand {
    DemuxerCommandType type;
    int64_t arg; // Seek 的目标位置（毫秒），其余命令忽略
};

class Demuxer {
public:
    Demuxer();
    ~Demuxer();

    // open 由控制线程同步调用（此时读线程未启动，无并发）。
    // 接管 dataSource 的所有权：无论成功与否，均由此类负责 delete。
    int open(IDataSource* dataSource);
    // close 同步：停止读线程并释放 AVFormatContext
    void close();

    // 控制接口：post 命令到内部队列后立即返回（异步）
    void start();
    void pause();
    void resume();
    // seekTo 同步等待完成（在 demuxer 线程执行 av_seek_frame + flush 包队列），供 seek 协调时序
    void seekTo(int64_t positionMs);
    // stop 同步：投递 Stop 命令并 join 读线程（供控制线程串行调用）
    void stop();

    int getVideoStreamIndex() const { return m_videoStreamIndex; }
    int getAudioStreamIndex() const { return m_audioStreamIndex; }
    AVCodecParameters* getVideoCodecPar() const;
    AVCodecParameters* getAudioCodecPar() const;
    // 视频编码宽高（codecpar->width/height，未校正旋转），无视频流返回 0
    void getVideoSize(int& width, int& height) const;
    // 视频显示宽高（已应用 display matrix 旋转，90/270 时交换宽高）
    void getDisplayVideoSize(int& width, int& height) const;
    AVRational getVideoTimeBase() const;
    AVRational getAudioTimeBase() const;
    int64_t getDuration() const;
    bool isEndOfStream() const { return m_eos.load(); }
    // 当前 seek 代际（seek 时自增），供消费端校验丢弃旧代际数据
    int getSerial() const { return m_serial.load(); }

    void setPacketQueues(PacketQueue* videoQueue, PacketQueue* audioQueue);

    // 到达文件末尾时回调（在 demuxer 线程触发），供上层投递 onCompletion 命令
    using EosCallback = std::function<void()>;
    void setEosCallback(EosCallback cb);

private:
    void threadLoop();
    void handleCommand(const DemuxerCommand& cmd);
    void doSeek(int64_t positionMs);
    static int interruptCallback(void* opaque);
    // 自定义 AVIO 桥接回调：opaque 为 IDataSource*
    static int readCallback(void* opaque, uint8_t* buf, int size);
    static int64_t seekCallback(void* opaque, int64_t offset, int whence);

    AVFormatContext* m_fmtCtx;
    AVIOContext* m_avio;
    IDataSource* m_dataSource;
    int m_videoStreamIndex;
    int m_audioStreamIndex;

    PacketQueue* m_videoQueue;
    PacketQueue* m_audioQueue;

    std::thread m_thread;
    CommandQueue<DemuxerCommand> m_cmdQueue;
    std::atomic<bool> m_running{false};   // 读线程运行标志（控制线程写，读线程读）
    std::atomic<bool> m_paused{false};    // 暂停标志（仅读线程写）
    std::atomic<bool> m_interrupt{false}; // 中断标志：打断阻塞中的 av_read_frame
    std::atomic<bool> m_eos{false};       // 到达文件末尾
    std::atomic<int> m_serial{0};         // seek 代际（doSeek 时自增）

    // seek 同步等待：demuxer 线程执行完 Seek 后置位并通知
    std::mutex m_seekMutex;
    std::condition_variable m_seekCond;
    std::atomic<bool> m_seekDone{false};

    EosCallback m_eosCallback;
};

} // namespace ccplayer
