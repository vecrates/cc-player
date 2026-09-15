#include "Sync.h"
#include "platform/log.h"

#define TAG "Sync"

namespace ccplayer {

Clock::Clock()
    : m_pts(0.0)
    , m_speed(1.0)
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
    return m_pts + elapsed *  m_speed;
}

void Clock::setSpeed(double speed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_speed = speed;
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
    , m_speed(1.0)
{
}

void AudioVideoSyncer::setMasterClock(Clock* audioClock) {
    m_masterClock = audioClock;
}

void AudioVideoSyncer::setMasterTimeProvider(MasterTimeProvider provider) {
    m_provider = std::move(provider);
}

double AudioVideoSyncer::getMasterTime() {
    if (m_provider) return m_provider();
    if (m_masterClock) return m_masterClock->getPTS();
    return 0.0;
}

double AudioVideoSyncer::computeVideoDelay(double videoPTS) {
    double masterTime = getMasterTime();
    double delay = videoPTS - masterTime;

    if (delay > m_maxDelay) {
        delay = m_maxDelay;
    }

    // 媒体时间差 ÷ speed = 渲染线程应 sleep 的墙钟时长
    return delay / m_speed.load();
}

bool AudioVideoSyncer::shouldDrop(double videoPTS) {
    double masterTime = getMasterTime();
    return (videoPTS - masterTime) < -m_dropThreshold;
}

void AudioVideoSyncer::setMaxDelay(double maxDelay) {
    m_maxDelay = maxDelay;
}

void AudioVideoSyncer::setDropThreshold(double threshold) {
    m_dropThreshold = threshold;
}

void AudioVideoSyncer::setSpeed(double speed) {
    m_speed = speed;
}

} // namespace ccplayer
