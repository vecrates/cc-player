#include "PlayerImpl.h"
#include <cstring>

#define TAG "Player"

namespace ccplayer {

// 帧队列容量：视频帧内存较大取 10 帧，音频帧较小取 5 帧，队列满时阻塞解码线程形成背压
static const int VIDEO_FRAME_QUEUE_SIZE = 10;
static const int AUDIO_FRAME_QUEUE_SIZE = 5;

// 标识当前线程是否为控制线程（用于避免控制线程内重入 stop/release 造成死锁）
static thread_local bool t_inControlThread = false;

PlayerImpl::PlayerImpl()
    : m_userData(nullptr)
    , m_videoFrameQueue(VIDEO_FRAME_QUEUE_SIZE)
    , m_audioFrameQueue(AUDIO_FRAME_QUEUE_SIZE)
    , m_videoRenderer(&m_videoFrameQueue, &m_syncer)
    , m_audioRenderer(&m_audioFrameQueue)
{
    memset(&m_callbacks, 0, sizeof(m_callbacks));

    // 启动控制线程（消息循环）
    m_controlRunning = true;
    m_controlThread = std::thread(&PlayerImpl::controlLoop, this);
}

PlayerImpl::~PlayerImpl() {
    release();
    m_controlRunning = false;
    m_cmdQueue.abort();
    if (m_controlThread.joinable()) {
        m_controlThread.join();
    }
}

// ==================== 配置接口（同步） ====================

void PlayerImpl::setDataSource(const char* path) {
    std::lock_guard<std::mutex> lock(m_configMutex);
    PlayerState s = m_state.load();
    if (s != PlayerState::Idle && s != PlayerState::Stopped) {
        LOGW(TAG, "setDataSource called in state %d", (int)s);
        return;
    }
    m_dataSource = path;
    m_state = PlayerState::Initialized;
    LOGI(TAG, "Data source set: %s", path);
}

void PlayerImpl::setFdOpener(FdOpener opener, void* userData) {
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_fdOpener = opener;
    m_fdOpenerUserData = userData;
}

void PlayerImpl::setSurface(void* nativeWindow) {
    m_videoRenderer.setSurface(nativeWindow);
}

void PlayerImpl::setSurfaceSize(int width, int height) {
    m_videoRenderer.setSurfaceSize(width, height);
}

void PlayerImpl::setCallbacks(const PlayerCallbacks& callbacks) {
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_callbacks = callbacks;
}

void PlayerImpl::setUserData(void* userData) {
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_userData = userData;
}

// ==================== 控制接口（异步） ====================

void PlayerImpl::prepare() {
    m_cmdQueue.push({PlayerCommandType::Prepare, 0});
}

void PlayerImpl::prepareAsync() {
    m_cmdQueue.push({PlayerCommandType::Prepare, 0});
}

void PlayerImpl::start() {
    m_cmdQueue.push({PlayerCommandType::Start, 0});
}

void PlayerImpl::pause() {
    m_cmdQueue.push({PlayerCommandType::Pause, 0});
}

void PlayerImpl::resume() {
    m_cmdQueue.push({PlayerCommandType::Resume, 0});
}

void PlayerImpl::seekTo(int64_t positionMs) {
    m_cmdQueue.push({PlayerCommandType::Seek, positionMs});
}

void PlayerImpl::setSpeed(float speed) {
    m_cmdQueue.push({PlayerCommandType::SetSpeed, (int64_t)(speed * 1000)});
}

// ==================== 同步控制 ====================

void PlayerImpl::stop() {
    if (t_inControlThread) {
        doStop();
        return;
    }
    std::unique_lock<std::mutex> lock(m_waitMutex);
    m_waitDone = false;
    m_cmdQueue.push({PlayerCommandType::Stop, 0});
    m_waitCond.wait(lock, [this] { return m_waitDone.load(); });
}

void PlayerImpl::release() {
    if (t_inControlThread) {
        doStop();
        m_demuxer.close();
        m_videoDecoder.close();
        m_audioDecoder.close();
        m_state = PlayerState::Idle;
        return;
    }
    std::unique_lock<std::mutex> lock(m_waitMutex);
    m_waitDone = false;
    m_cmdQueue.push({PlayerCommandType::Release, 0});
    m_waitCond.wait(lock, [this] { return m_waitDone.load(); });
}

// ==================== 查询接口（同步） ====================

int64_t PlayerImpl::getCurrentPosition() {
    // 统一走主时钟（有音频流为音频时钟，无音频流为视频时钟）
    return (int64_t)(m_syncer.getMasterTime() * 1000);
}

int64_t PlayerImpl::getDuration() {
    return m_demuxer.getDuration();
}

void PlayerImpl::getVideoSize(int& width, int& height) const {
    m_demuxer.getVideoSize(width, height);
}

void PlayerImpl::getDisplayVideoSize(int& width, int& height) const {
    m_demuxer.getDisplayVideoSize(width, height);
}

// ==================== 控制线程 ====================

void PlayerImpl::controlLoop() {
    t_inControlThread = true;
    while (m_controlRunning) {
        auto maybe = m_cmdQueue.pop();
        if (!maybe) break;
        handleCommand(*maybe);
        if (!m_controlRunning) break;
    }
    t_inControlThread = false;
}

void PlayerImpl::handleCommand(const PlayerCommand& cmd) {
    switch (cmd.type) {
        case PlayerCommandType::Prepare:
            doPrepare();
            break;
        case PlayerCommandType::Start:
            doStart();
            break;
        case PlayerCommandType::Pause:
            doPause();
            break;
        case PlayerCommandType::Resume:
            doResume();
            break;
        case PlayerCommandType::Seek:
            doSeek(cmd.arg);
            break;
        case PlayerCommandType::SetSpeed:
            doSetSpeed((float)cmd.arg / 1000.0f);
            break;
        case PlayerCommandType::Stop:
            doStop();
            notifyDone();
            break;
        case PlayerCommandType::Release:
            doStop();
            m_demuxer.close();
            m_videoDecoder.close();
            m_audioDecoder.close();
            m_state = PlayerState::Idle;
            notifyDone();
            break;
        case PlayerCommandType::Eos:
            notifyCompletion();
            break;
    }
}

void PlayerImpl::notifyDone() {
    std::lock_guard<std::mutex> lock(m_waitMutex);
    m_waitDone = true;
    m_waitCond.notify_all();
}

// ==================== 命令执行（控制线程） ====================

IDataSource* PlayerImpl::createDataSource(const std::string& uri) {
    if (uri.rfind("content://", 0) == 0) {
        FdOpener opener;
        void* openerUd;
        {
            std::lock_guard<std::mutex> lock(m_configMutex);
            opener = m_fdOpener;
            openerUd = m_fdOpenerUserData;
        }
        if (!opener) {
            LOGE(TAG, "content:// source requires setFdOpener");
            return nullptr;
        }
        int64_t offset = 0;
        int64_t length = -1;
        int fd = opener(uri.c_str(), &offset, &length, openerUd);
        if (fd < 0) {
            LOGE(TAG, "Failed to open content uri: %s", uri.c_str());
            return nullptr;
        }
        return new FdDataSource(fd, offset, length);
    }
    if (uri.rfind("http://", 0) == 0 || uri.rfind("https://", 0) == 0) {
        return new NetworkDataSource(uri.c_str());
    }
    if (uri.rfind("file://", 0) == 0) {
        return new FileDataSource(uri.c_str() + 7);
    }
    // 其它 scheme（rtsp/rtmp/udp...）暂时不支持
    if (uri.find("://") != std::string::npos) {
        LOGE(TAG, "Unsupported data source scheme: %s", uri.c_str());
        return nullptr;
    }
    // 纯本地路径
    return new FileDataSource(uri.c_str());
}

void PlayerImpl::doPrepare() {
    if (m_state.load() != PlayerState::Initialized) {
        LOGW(TAG, "prepare called in state %d", (int)m_state.load());
        return;
    }

    std::string source;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        source = m_dataSource;
    }

    IDataSource* dataSource = createDataSource(source);
    if (!dataSource) {
        m_state = PlayerState::Error;
        notifyError((int)PlayerError::InvalidDataSource);
        return;
    }
    // Demuxer 接管 dataSource 所有权（无论成功失败都会释放）
    if (m_demuxer.open(dataSource) < 0) {
        m_state = PlayerState::Error;
        notifyError((int)PlayerError::InvalidDataSource);
        return;
    }

    // 复用队列（stop 后 abort 过，这里清除 abort 标志）
    m_videoPacketQueue.reset();
    m_audioPacketQueue.reset();
    m_videoFrameQueue.reset();
    m_audioFrameQueue.reset();

    // 打开视频解码器（失败仅记日志，仍可仅播放音频）
    if (m_demuxer.getVideoStreamIndex() >= 0) {
        auto* par = m_demuxer.getVideoCodecPar();
        auto tb = m_demuxer.getVideoTimeBase();
        if (m_videoDecoder.openVideo(par, tb) < 0) {
            LOGE(TAG, "Failed to open video decoder");
        }
        // 将显示宽高（含旋转校正）传给渲染器，用于 letterbox 画面适配
        int vw = 0, vh = 0;
        m_demuxer.getDisplayVideoSize(vw, vh);
        m_videoRenderer.setVideoSize(vw, vh);
    }

    // 打开音频解码器与音频输出链路
    if (m_demuxer.getAudioStreamIndex() >= 0) {
        auto* par = m_demuxer.getAudioCodecPar();
        auto tb = m_demuxer.getAudioTimeBase();
        if (m_audioDecoder.openAudio(par, tb) < 0) {
            LOGE(TAG, "Failed to open audio decoder");
        } else {
            m_audioRenderer.open(par->sample_rate, par->ch_layout.nb_channels,
                                 (AVSampleFormat)par->format, &par->ch_layout);
        }
    }

    // 主时钟选择：有音频流以音频时钟为准，否则以视频时钟为准（避免无参考时间）
    if (m_demuxer.getAudioStreamIndex() >= 0) {
        m_syncer.setMasterTimeProvider([this] { return m_audioRenderer.getCurrentPts(); });
    } else {
        m_syncer.setMasterTimeProvider([this] { return m_videoRenderer.getCurrentPts(); });
    }

    // 串成流水线
    m_demuxer.setPacketQueues(&m_videoPacketQueue, &m_audioPacketQueue);
    m_videoDecoder.setPacketQueue(&m_videoPacketQueue);
    m_videoDecoder.setFrameQueue(&m_videoFrameQueue);
    m_audioDecoder.setPacketQueue(&m_audioPacketQueue);
    m_audioDecoder.setFrameQueue(&m_audioFrameQueue);

    // 解封装 EOF 时投递 onCompletion 命令到控制线程
    m_demuxer.setEosCallback([this]() {
        m_cmdQueue.push({PlayerCommandType::Eos, 0});
    });

    // 渲染进度回调节流上报
    m_videoRenderer.setProgressCallback([this]() {
        notifyProgress();
    });

    m_state = PlayerState::Prepared;
    LOGI(TAG, "Prepared, duration=%lld ms", (long long)getDuration());

    notifyPrepared();
}

void PlayerImpl::startPipeline() {
    // 清除 pause 时设置的 abort 标志
    m_videoFrameQueue.reset();
    m_audioFrameQueue.reset();

    m_demuxer.start();
    if (m_demuxer.getVideoStreamIndex() >= 0) {
        m_videoDecoder.start();
    }
    if (m_demuxer.getAudioStreamIndex() >= 0) {
        m_audioDecoder.start();
    }

    m_videoRenderer.start();
    m_audioRenderer.start();
}

void PlayerImpl::doStart() {
    PlayerState s = m_state.load();
    if (s != PlayerState::Prepared && s != PlayerState::Paused) {
        LOGW(TAG, "start called in state %d", (int)s);
        return;
    }
    startPipeline();
    m_state = PlayerState::Started;
    LOGI(TAG, "Started");
}

void PlayerImpl::doPause() {
    if (m_state.load() != PlayerState::Started) {
        LOGW(TAG, "pause called in state %d", (int)m_state.load());
        return;
    }

    // 1) abort 帧队列唤醒渲染/音频线程，然后停止它们
    m_videoFrameQueue.abort();
    m_audioFrameQueue.abort();
    m_videoRenderer.stop();
    m_audioRenderer.pause();

    // 2) 暂停解码与读（线程挂起，不退出，保留缓冲）
    if (m_demuxer.getVideoStreamIndex() >= 0) {
        m_videoDecoder.pause();
    }
    if (m_demuxer.getAudioStreamIndex() >= 0) {
        m_audioDecoder.pause();
    }
    m_demuxer.pause();

    m_state = PlayerState::Paused;
    LOGI(TAG, "Paused");
}

void PlayerImpl::doResume() {
    if (m_state.load() != PlayerState::Paused) {
        LOGW(TAG, "resume called in state %d", (int)m_state.load());
        return;
    }
    startPipeline();
    m_state = PlayerState::Started;
    LOGI(TAG, "Resumed");
}

void PlayerImpl::doSeek(int64_t positionMs) {
    PlayerState s = m_state.load();
    if (s != PlayerState::Started && s != PlayerState::Paused) {
        LOGW(TAG, "seekTo called in state %d", (int)s);
        return;
    }

    // 1) 暂停解码与读（渲染/音频线程不停，靠 serial 丢弃旧帧）
    if (m_demuxer.getVideoStreamIndex() >= 0) {
        m_videoDecoder.pause();
    }
    if (m_demuxer.getAudioStreamIndex() >= 0) {
        m_audioDecoder.pause();
    }
    m_demuxer.pause();

    // 2) flush 解码器（同步，解码线程执行 avcodec_flush_buffers）
    if (m_demuxer.getVideoStreamIndex() >= 0) {
        m_videoDecoder.flush();
    }
    if (m_demuxer.getAudioStreamIndex() >= 0) {
        m_audioDecoder.flush();
    }

    // 3) flush 帧队列（清空旧帧）
    m_videoFrameQueue.flush();
    m_audioFrameQueue.flush();

    // 4) demuxer seek（同步，serial 自增 + flush 包队列）
    m_demuxer.seekTo(positionMs);

    // 5) 更新渲染/音频的 serial 与 seek 目标（消费时据此精准丢弃）
    int serial = m_demuxer.getSerial();
    double targetSec = positionMs / 1000.0;
    m_videoRenderer.seek(serial, targetSec);
    m_audioRenderer.seek(serial, targetSec);

    // 6) 恢复（仅 Started 状态恢复；Paused 保持暂停）
    if (s == PlayerState::Started) {
        m_demuxer.resume();
        if (m_demuxer.getVideoStreamIndex() >= 0) {
            m_videoDecoder.resume();
        }
        if (m_demuxer.getAudioStreamIndex() >= 0) {
            m_audioDecoder.resume();
        }
    }

    LOGI(TAG, "Seeked to %lld ms", (long long)positionMs);
    notifySeekComplete();
}

void PlayerImpl::doSetSpeed(float speed) {
    // clamp 到 [0.5, 2.0]，越界记录警告但不中断
    if (speed < 0.5f) {
        LOGW(TAG, "setSpeed %.3f out of range, clamp to 0.5", speed);
        speed = 0.5f;
    } else if (speed > 2.0f) {
        LOGW(TAG, "setSpeed %.3f out of range, clamp to 2.0", speed);
        speed = 2.0f;
    }

    m_speed = speed;

    // 下发到各同步/渲染组件（倍速作为播放器属性，任意状态均可更新）
    m_syncer.setSpeed(speed);
    m_videoRenderer.setSpeed(speed);
    m_audioRenderer.setSpeed(speed);

    LOGI(TAG, "Speed set to %.2f", speed);
}

void PlayerImpl::doStop() {
    // 1) abort 帧队列：唤醒渲染/音频线程的 pop 与解码线程的 push
    m_videoFrameQueue.abort();
    m_audioFrameQueue.abort();
    // 2) 停止渲染线程 + 停止并释放音频
    m_videoRenderer.stop();
    m_audioRenderer.close();

    // 3) 停止解码与读线程
    m_videoDecoder.stop();
    m_audioDecoder.stop();
    m_demuxer.stop();

    // 4) abort + flush 所有队列
    m_videoPacketQueue.abort();
    m_audioPacketQueue.abort();
    m_videoPacketQueue.flush();
    m_audioPacketQueue.flush();
    m_videoFrameQueue.flush();
    m_audioFrameQueue.flush();

    m_state = PlayerState::Stopped;
    LOGI(TAG, "Stopped");
}

// ==================== 回调派发（锁外触发，避免重入死锁） ====================

void PlayerImpl::notifyPrepared() {
    PlayerCallbacks cb;
    void* ud;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        cb = m_callbacks;
        ud = m_userData;
    }
    if (cb.onPrepared) cb.onPrepared(ud);
}

void PlayerImpl::notifyError(int code) {
    PlayerCallbacks cb;
    void* ud;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        cb = m_callbacks;
        ud = m_userData;
    }
    if (cb.onError) cb.onError(code, ud);
}

void PlayerImpl::notifyCompletion() {
    PlayerCallbacks cb;
    void* ud;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        cb = m_callbacks;
        ud = m_userData;
    }
    if (cb.onCompletion) cb.onCompletion(ud);
}

void PlayerImpl::notifySeekComplete() {
    PlayerCallbacks cb;
    void* ud;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        cb = m_callbacks;
        ud = m_userData;
    }
    if (cb.onSeekComplete) cb.onSeekComplete(ud);
}

void PlayerImpl::notifyProgress() {
    PlayerCallbacks cb;
    void* ud;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        cb = m_callbacks;
        ud = m_userData;
    }
    if (cb.onProgress) cb.onProgress(getCurrentPosition(), getDuration(), ud);
}

} // namespace ccplayer
