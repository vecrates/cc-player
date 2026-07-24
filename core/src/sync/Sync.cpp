#include "Sync.h"
#include "platform/log.h"

#define TAG "Sync"

namespace ccplayer {

Clock::Clock()
    : m_pts(0.0)
    , m_lastUpdate(std::chrono::steady_clock::now())
{
}

void Clock::setPTS(double pts) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pts = pts;
    m_lastUpdate = std::chrono::steady_clock::now();
}

double Clock::getPTS() {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - m_lastUpdate).count();
    return m_pts + elapsed;
}

void Clock::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pts = 0.0;
    m_lastUpdate = std::chrono::steady_clock::now();
}

AudioVideoSyncer::AudioVideoSyncer()
    : m_masterClock(nullptr)
    , m_maxDelay(0.1)
    , m_dropThreshold(0.1)
{
}

void AudioVideoSyncer::setMasterClock(Clock* audioClock) {
    m_masterClock = audioClock;
}

double AudioVideoSyncer::getMasterTime() {
    if (!m_masterClock) return 0.0;
    return m_masterClock->getPTS();
}

double AudioVideoSyncer::computeVideoDelay(double videoPTS) {
    double masterTime = getMasterTime();
    double delay = videoPTS - masterTime;

    if (delay > m_maxDelay) {
        delay = m_maxDelay;
    }

    return delay;
}

void AudioVideoSyncer::setMaxDelay(double maxDelay) {
    m_maxDelay = maxDelay;
}

void AudioVideoSyncer::setDropThreshold(double threshold) {
    m_dropThreshold = threshold;
}

} // namespace ccplayer
