#pragma once

#include "Player.h"
#include "demuxer/Demuxer.h"
#include "decoder/Decoder.h"
#include "queue/PacketQueue.h"
#include "queue/FrameQueue.h"
#include "queue/CommandQueue.h"
#include "sync/Sync.h"
#include "video_out/VideoRenderer.h"
#include "audio_out/AudioRenderer.h"
#include "platform/log.h"

#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <string>

#define TAG "Player"

namespace ccplayer {

/**
 * 播放器控制命令：由单一控制线程串行执行，所有控制接口（异步）post 后立即返回。
 */
enum class PlayerCommandType {
    Prepare,  // 打开媒体源、初始化解码器与渲染/音频链路
    Start,    // 开始播放
    Pause,    // 暂停
    Resume,   // 恢复
    Seek,     // 跳转（arg = 目标毫秒）
    Stop,     // 停止（同步等待完成）
    Release,  // 释放（同步等待完成）
    Eos,      // 解封装到达文件末尾（触发 onCompletion）
};

struct PlayerCommand {
    PlayerCommandType type;
    int64_t arg; // Seek 的目标位置（毫秒），其余命令忽略
};

/**
 * 播放器核心实现（控制 Actor）：
 *  - 控制线程串行执行所有命令，状态机收敛于此；
 *  - Demuxer / Decoder 为 Actor（内部命令队列）；渲染/音频由 VideoRenderer / AudioRenderer 封装；
 *  - 精准 seek：serial（demuxer 自增）+ seek 目标双机制，渲染/音频线程不停，消费时丢弃旧帧。
 */
class PlayerImpl {
public:
    PlayerImpl();
    ~PlayerImpl();

    // ===== 配置接口（同步，调用线程执行；需在 prepare 前设置）=====
    void setDataSource(const char* path);
    void setSurface(void* nativeWindow);
    void setSurfaceSize(int width, int height);
    void setCallbacks(const PlayerCallbacks& callbacks);
    void setUserData(void* userData);

    // ===== 控制接口（异步 post 命令，立即返回）=====
    void prepare();
    void prepareAsync();
    void start();
    void pause();
    void resume();
    void seekTo(int64_t positionMs);

    // ===== 同步控制（post 命令并等待完成）=====
    void stop();
    void release();

    // ===== 查询接口（同步原子快照）=====
    int64_t getCurrentPosition();
    int64_t getDuration();
    PlayerState getState() const { return m_state.load(); }

private:
    // 控制线程
    void controlLoop();
    void handleCommand(const PlayerCommand& cmd);

    // 命令执行（均在控制线程）
    void doPrepare();
    void startPipeline(); // start / resume 共用的流水线启动
    void doPause();
    void doSeek(int64_t positionMs);
    void doStop();
    void notifyDone();    // 同步命令（stop/release）完成通知

    // 回调派发（锁外触发，避免重入死锁）
    void notifyPrepared();
    void notifyError(int code);
    void notifyCompletion();
    void notifySeekComplete();
    void notifyProgress();

    // ===== 配置字段（调用线程写，控制线程读，m_configMutex 保护）=====
    std::mutex m_configMutex;
    std::string m_dataSource;
    PlayerCallbacks m_callbacks;
    void* m_userData;

    // ===== 状态（仅控制线程写，查询线程原子读）=====
    std::atomic<PlayerState> m_state{PlayerState::Idle};

    // ===== 控制线程 =====
    std::thread m_controlThread;
    CommandQueue<PlayerCommand> m_cmdQueue;
    std::atomic<bool> m_controlRunning{false};
    std::mutex m_waitMutex;
    std::condition_variable m_waitCond;
    std::atomic<bool> m_waitDone{false};

    // ===== 流水线模块 =====
    Demuxer m_demuxer;
    Decoder m_videoDecoder;
    Decoder m_audioDecoder;
    PacketQueue m_videoPacketQueue;
    PacketQueue m_audioPacketQueue;
    FrameQueue m_videoFrameQueue;
    FrameQueue m_audioFrameQueue;
    AudioVideoSyncer m_syncer;
    VideoRenderer m_videoRenderer;
    AudioRenderer m_audioRenderer;
};

} // namespace ccplayer
