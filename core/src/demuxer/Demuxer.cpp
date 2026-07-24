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
    , m_running(false)
    , m_abort(false)
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
    stopReading();
    if (m_fmtCtx) {
        avformat_close_input(&m_fmtCtx);
        m_fmtCtx = nullptr;
    }
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
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

void Demuxer::setPacketQueues(PacketQueue* videoQueue, PacketQueue* audioQueue) {
    m_videoQueue = videoQueue;
    m_audioQueue = audioQueue;
}

void Demuxer::startReading() {
    if (m_running) return;
    m_abort = false;
    m_running = true;
    m_readThread = std::thread(&Demuxer::readLoop, this);
}

void Demuxer::stopReading() {
    if (!m_running) return;
    m_abort = true;
    if (m_readThread.joinable()) {
        m_readThread.join();
    }
    m_running = false;
}

void Demuxer::flush() {
    if (m_videoQueue) m_videoQueue->flush();
    if (m_audioQueue) m_audioQueue->flush();
}

int Demuxer::seekTo(int64_t positionMs) {
    if (!m_fmtCtx) return -1;

    int64_t target = positionMs * 1000; // convert ms to AV_TIME_BASE
    int ret = av_seek_frame(m_fmtCtx, -1, target, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        LOGE(TAG, "Seek failed to %lld ms", (long long)positionMs);
        return ret;
    }

    flush();
    LOGI(TAG, "Seeked to %lld ms", (long long)positionMs);
    return 0;
}

void Demuxer::setProgressCallback(ProgressCallback cb) {
    m_progressCb = cb;
}

void Demuxer::readLoop() {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        LOGE(TAG, "Failed to allocate packet");
        return;
    }

    while (!m_abort) {
        int ret = av_read_frame(m_fmtCtx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                LOGI(TAG, "End of file reached");
                break;
            }
            if (m_abort) break;
            LOGE(TAG, "Read frame error: %d", ret);
            break;
        }

        if (pkt->stream_index == m_videoStreamIndex && m_videoQueue) {
            m_videoQueue->push(pkt);
        } else if (pkt->stream_index == m_audioStreamIndex && m_audioQueue) {
            m_audioQueue->push(pkt);
        }

        av_packet_unref(pkt);

        if (m_progressCb) {
            int64_t currentMs = m_fmtCtx->duration > 0 ?
                av_rescale_q(pkt->pts, m_fmtCtx->streams[pkt->stream_index]->time_base, {1, 1000}) : 0;
            m_progressCb(currentMs, getDuration());
        }
    }

    av_packet_free(&pkt);
    m_running = false;
}

} // namespace ccplayer
