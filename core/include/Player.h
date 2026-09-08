#pragma once

#include "PlayerTypes.h"
#include <cstdint>

namespace ccplayer {

class PlayerImpl;

/**
 * 播放器对外统一接口（门面类）。
 * 采用 pimpl 模式：公开接口全部转发给内部实现 PlayerImpl，
 * 隔离 FFmpeg / EGL 等实现细节，保持 ABI 稳定。
 * 典型调用流程：setDataSource → prepare/prepareAsync → start → pause/resume → stop → release。
 */
class Player {
public:
    Player();
    ~Player();

    // 禁止拷贝：播放器持有独占的底层资源（解码器、队列、线程）
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // 设置媒体源（文件路径或 URL），仅在 Idle/Stopped 状态下有效，成功后进入 Initialized 状态
    void setDataSource(const char* path);
    // 设置渲染窗口（Android 上为 ANativeWindow*），需在 start 之前设置
    void setSurface(void* nativeWindow);
    // 设置渲染窗口尺寸，用于设置 OpenGL viewport
    void setSurfaceSize(int width, int height);

    // 异步 prepare：post 命令后立即返回，完成后通过 onPrepared 回调通知
    void prepare();
    // 与 prepare 一致（全异步语义下两者等价），保留以兼容旧接口
    void prepareAsync();

    // 启动播放（异步）：由控制线程串行启动解封装/解码/渲染/音频，进入 Started 状态
    void start();
    // 暂停播放（异步）：挂起数据线程、停止渲染/音频线程，进入 Paused 状态（保留缓冲）
    void pause();
    // 恢复播放（异步）：重新启动流水线，进入 Started 状态
    void resume();
    // 停止播放（同步等待完成）：停止线程、清空队列、释放音频资源，进入 Stopped 状态
    void stop();
    // 释放资源（同步等待完成）：先 stop，再关闭解封装器与解码器，回到 Idle 状态可复用
    void release();

    // 跳转到指定位置（异步，毫秒）：刷新解码器与所有队列，完成后回调 onSeekComplete
    void seekTo(int64_t positionMs);

    // 当前播放位置（毫秒），由音频主时钟推导得出（音频作为主时钟驱动视频同步）
    int64_t getCurrentPosition();
    // 媒体总时长（毫秒），来自封装容器信息，0 表示未知（如直播流）
    int64_t getDuration();

    // 查询播放器状态机当前状态，详见 PlayerState 定义
    PlayerState getState() const;

    // 设置事件回调集合，在 prepare 之前设置以确保回调可用（回调可能在后台线程触发）
    void setCallbacks(const PlayerCallbacks& callbacks);
    // 设置透传给所有回调的用户自定义数据指针（如 JNI 层的 PlayerContext）
    void setUserData(void* userData);

private:
    PlayerImpl* m_impl;
};

} // namespace ccplayer
