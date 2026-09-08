#pragma once

#include <chrono>
#include <mutex>
#include <functional>

namespace ccplayer {

class Clock {
public:
    Clock();

    void setPTS(double pts);
    double getPTS();

    void reset();

private:
    double m_pts;
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

    double computeVideoDelay(double videoPTS);

    // 视频帧是否落后主时钟过多（应丢弃追赶），阈值 m_dropThreshold
    bool shouldDrop(double videoPTS);

    void setMaxDelay(double maxDelay);
    void setDropThreshold(double threshold);

private:
    Clock* m_masterClock;
    MasterTimeProvider m_provider;
    double m_maxDelay;
    double m_dropThreshold;
};

} // namespace ccplayer
