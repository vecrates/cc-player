#include "Demuxer.h"
#include "platform/log.h"

#define TAG "Demuxer"

namespace ccplayer {

Demuxer::Demuxer()
    : m_fmtCtx(nullptr)
    , m_videoStreamIndex(-1)
    , m_audioStreamIndex(-1)
    , m_videoQueue(nullptr)
    , m_audioQueue(nullptr)
{
}

Demuxer::~Demuxer() {
    close();
}

int Demuxer::open(const char* url) {
    int ret = avformat_open_input(&m_fmtCtx, url, nullptr, nullptr);
    if (ret < 0) {
        LOGE(TAG, "Failed to open input: %s", url);
        return ret;
    }

    // 注册中断回调：seek/pause/stop 通过置位中断标志打断阻塞中的 av_read_frame
    m_fmtCtx->interrupt_callback.callback = interruptCallback;
    m_fmtCtx->interrupt_callback.opaque = this;

    ret = avformat_find_stream_info(m_fmtCtx, nullptr);
    if (ret < 0) {
        LOGE(TAG, "Failed to find stream info");
        close();
        return ret;
    }

    for (unsigned i = 0; i < m_fmtCtx->nb_streams; i++) {
        if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_videoStreamIndex < 0) {
            m_videoStreamIndex = i;
        }
        if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && m_audioStreamIndex < 0) {
            m_audioStreamIndex = i;
        }
    }

    if (m_videoStreamIndex < 0 && m_audioStreamIndex < 0) {
        LOGE(TAG, "No playable streams found");
        close();
        return -1;
    }

    LOGI(TAG, "Opened: video=%d, audio=%d, duration=%lld ms",
         m_videoStreamIndex, m_audioStreamIndex,
         (long long)(m_fmtCtx->duration / 1000));
    return 0;
}

void Demuxer::close() {
    stop();
    if (m_fmtCtx) {
        avformat_close_input(&m_fmtCtx);
        m_fmtCtx = nullptr;
    }
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    m_eos = false;
}

void Demuxer::start() {
    // 线程已自然退出（EOF）时先回收，再重建
    if (m_thread.joinable() && !m_running.load()) {
        m_thread.join();
    }
    if (!m_thread.joinable()) {
        m_paused = false;
        m_running = true;
        m_cmdQueue.reset();
        m_cmdQueue.clear(); // 清空残留命令，避免误执行
        m_thread = std::thread(&Demuxer::threadLoop, this);
    } else {
        m_cmdQueue.push({DemuxerCommandType::Resume, 0});
    }
}

void Demuxer::pause() {
    m_interrupt = true; // 打断可能阻塞中的读，让线程尽快进入挂起态
    m_cmdQueue.push({DemuxerCommandType::Pause, 0});
}

void Demuxer::resume() {
    m_cmdQueue.push({DemuxerCommandType::Resume, 0});
}

void Demuxer::seekTo(int64_t positionMs) {
    if (!m_running.load()) {
        // 线程未运行：无并发，直接同步执行
        doSeek(positionMs);
        return;
    }

    // 线程运行中：投递 Seek 命令并同步等待完成
    m_interrupt = true; // 打断阻塞中的读
    std::unique_lock<std::mutex> lock(m_seekMutex);
    m_seekDone = false;
    m_cmdQueue.push({DemuxerCommandType::Seek, positionMs});
    m_seekCond.wait(lock, [this] { return m_seekDone.load(); });
}

void Demuxer::stop() {
    if (!m_thread.joinable()) return;

    m_interrupt = true;
    m_cmdQueue.push({DemuxerCommandType::Stop, 0});

    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_running = false;
    m_interrupt = false;
    m_cmdQueue.clear(); // 清空残留命令，避免下次 start 后误执行
}

void Demuxer::setPacketQueues(PacketQueue* videoQueue, PacketQueue* audioQueue) {
    m_videoQueue = videoQueue;
    m_audioQueue = audioQueue;
}

void Demuxer::setEosCallback(EosCallback cb) {
    m_eosCallback = std::move(cb);
}

AVCodecParameters* Demuxer::getVideoCodecPar() const {
    if (m_videoStreamIndex < 0 || !m_fmtCtx) return nullptr;
    return m_fmtCtx->streams[m_videoStreamIndex]->codecpar;
}

AVCodecParameters* Demuxer::getAudioCodecPar() const {
    if (m_audioStreamIndex < 0 || !m_fmtCtx) return nullptr;
    return m_fmtCtx->streams[m_audioStreamIndex]->codecpar;
}

AVRational Demuxer::getVideoTimeBase() const {
    if (m_videoStreamIndex < 0 || !m_fmtCtx) return {1, 1};
    return m_fmtCtx->streams[m_videoStreamIndex]->time_base;
}

AVRational Demuxer::getAudioTimeBase() const {
    if (m_audioStreamIndex < 0 || !m_fmtCtx) return {1, 1};
    return m_fmtCtx->streams[m_audioStreamIndex]->time_base;
}

int64_t Demuxer::getDuration() const {
    if (!m_fmtCtx) return 0;
    return m_fmtCtx->duration / 1000; // convert to ms
}

int Demuxer::interruptCallback(void* opaque) {
    auto* self = static_cast<Demuxer*>(opaque);
    return self->m_interrupt.load() ? 1 : 0;
}

void Demuxer::threadLoop() {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        LOGE(TAG, "Failed to allocate packet");
        m_running = false;
        return;
    }

    while (m_running) {
        // 1) 处理所有积压命令
        DemuxerCommand cmd;
        while (m_cmdQueue.tryPop(cmd)) {
            handleCommand(cmd);
            if (!m_running) break;
        }
        if (!m_running) break;

        // 2) 暂停态：挂起等待新命令（线程不退出）
        if (m_paused) {
            auto maybe = m_cmdQueue.pop();
            if (!maybe) break; // 队列被 abort
            handleCommand(*maybe);
            continue;
        }

        // 3) 读一帧
        int ret = av_read_frame(m_fmtCtx, pkt);
        if (ret == AVERROR_EXIT) {
            // 被中断回调打断（seek/pause/stop），回顶部处理命令
            m_interrupt = false;
            continue;
        }
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                m_eos = true;
                LOGI(TAG, "End of file reached");
                if (m_eosCallback) {
                    m_eosCallback();
                }
            } else {
                LOGE(TAG, "Read frame error: %d", ret);
            }
            break;
        }

        // 4) 分发到对应包队列（push 阻塞形成背压），包携带当前 seek 代际
        int serial = m_serial.load();
        if (pkt->stream_index == m_videoStreamIndex && m_videoQueue) {
            m_videoQueue->push(pkt, serial);
        } else if (pkt->stream_index == m_audioStreamIndex && m_audioQueue) {
            m_audioQueue->push(pkt, serial);
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    m_running = false;
}

void Demuxer::handleCommand(const DemuxerCommand& cmd) {
    switch (cmd.type) {
        case DemuxerCommandType::Start:
            m_paused = false;
            break;
        case DemuxerCommandType::Pause:
            m_interrupt = false;
            m_paused = true;
            break;
        case DemuxerCommandType::Resume:
            m_paused = false;
            break;
        case DemuxerCommandType::Seek:
            m_interrupt = false;
            doSeek(cmd.arg);
            break;
        case DemuxerCommandType::Stop:
        case DemuxerCommandType::Close:
            m_interrupt = false;
            m_running = false;
            break;
    }
}

void Demuxer::doSeek(int64_t positionMs) {
    if (!m_fmtCtx) return;

    // 递增 seek 代际：消费端据此丢弃 seek 前的旧数据
    m_serial++;

    // 此处在 demuxer 线程内执行，与 av_read_frame 不并发
    int64_t target = positionMs * 1000; // convert ms to AV_TIME_BASE
    int ret = av_seek_frame(m_fmtCtx, -1, target, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        LOGE(TAG, "Seek failed to %lld ms", (long long)positionMs);
    } else {
        // 清空旧包，避免 seek 前的数据与新位置数据串台
        if (m_videoQueue) m_videoQueue->flush();
        if (m_audioQueue) m_audioQueue->flush();
        m_eos = false;
        LOGI(TAG, "Seeked to %lld ms", (long long)positionMs);
    }

    // 通知等待者 seek 已完成
    {
        std::lock_guard<std::mutex> lock(m_seekMutex);
        m_seekDone = true;
    }
    m_seekCond.notify_all();
}

} // namespace ccplayer
