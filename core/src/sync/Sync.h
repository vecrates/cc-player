#pragma once

#include <chrono>
#include <mutex>
#include <atomic>
#include <functional>

namespace ccplayer {

class Clock {
public:
    Clock();

    void setPTS(double pts);
    double getPTS();

    // 播放倍速：getPTS 的 elapsed 部分按 speed 缩放（无音频流时视频时钟为主时钟用）
    void setSpeed(double speed);

    void reset();

private:
    double m_pts;
    double m_speed;
    std::chrono::steady_clock::time_point m_lastUpdate;
    std::mutex m_mutex;
};

class AudioVideoSyncer {
public:
    using MasterTimeProvider = std::function<double()>;

    AudioVideoSyncer();

    void setMasterClock(Clock* audioClock);
    // 设置主时钟时间提供者（优先于 Clock，如 AudioRenderer 的准确 PTS）
    void setMasterTimeProvider(MasterTimeProvider provider);

    double getMasterTime();

    // 返回渲染线程应 sleep 的墙钟时长（媒体时间差 ÷ speed）
    double computeVideoDelay(double videoPTS);

    // 视频帧是否落后主时钟过多（应丢弃追赶），阈值 m_dropThreshold（媒体时间域，不受 speed 影响）
    bool shouldDrop(double videoPTS);

    void setMaxDelay(double maxDelay);
    void setDropThreshold(double threshold);
    // 播放倍速（控制线程写，渲染线程读）
    void setSpeed(double speed);

private:
    Clock* m_masterClock;
    MasterTimeProvider m_provider;
    double m_maxDelay;
    double m_dropThreshold;
    std::atomic<double> m_speed;
};

} // namespace ccplayer
