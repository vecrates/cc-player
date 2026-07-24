#pragma once

#include <chrono>
#include <mutex>

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
    AudioVideoSyncer();

    void setMasterClock(Clock* audioClock);

    double getMasterTime();

    double computeVideoDelay(double videoPTS);

    void setMaxDelay(double maxDelay);
    void setDropThreshold(double threshold);

private:
    Clock* m_masterClock;
    double m_maxDelay;
    double m_dropThreshold;
};

} // namespace ccplayer
