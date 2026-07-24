#include "Decoder.h"
#include "platform/log.h"

#define TAG "Decoder"

namespace ccplayer {

Decoder::Decoder()
    : m_codecCtx(nullptr)
    , m_isVideo(false)
    , m_packetQueue(nullptr)
    , m_frameQueue(nullptr)
    , m_running(false)
    , m_abort(false)
{
}

Decoder::~Decoder() {
    close();
}

int Decoder::openVideo(AVCodecParameters* codecPar, AVRational timeBase) {
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        LOGE(TAG, "Video codec not found: %d", codecPar->codec_id);
        return -1;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        LOGE(TAG, "Failed to allocate video codec context");
        return -1;
    }

    int ret = avcodec_parameters_to_context(m_codecCtx, codecPar);
    if (ret < 0) {
        LOGE(TAG, "Failed to copy codec params");
        close();
        return ret;
    }

    ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        LOGE(TAG, "Failed to open video codec");
        close();
        return ret;
    }

    m_timeBase = timeBase;
    m_isVideo = true;
    LOGI(TAG, "Video decoder opened: %dx%d", codecPar->width, codecPar->height);
    return 0;
}

int Decoder::openAudio(AVCodecParameters* codecPar, AVRational timeBase) {
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        LOGE(TAG, "Audio codec not found: %d", codecPar->codec_id);
        return -1;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        LOGE(TAG, "Failed to allocate audio codec context");
        return -1;
    }

    int ret = avcodec_parameters_to_context(m_codecCtx, codecPar);
    if (ret < 0) {
        LOGE(TAG, "Failed to copy codec params");
        close();
        return ret;
    }

    ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        LOGE(TAG, "Failed to open audio codec");
        close();
        return ret;
    }

    m_timeBase = timeBase;
    m_isVideo = false;
    LOGI(TAG, "Audio decoder opened: %d Hz, %d channels",
         codecPar->sample_rate, codecPar->ch_layout.nb_channels);
    return 0;
}

void Decoder::close() {
    stopDecoding();
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }
}

void Decoder::setPacketQueue(PacketQueue* pktQueue) {
    m_packetQueue = pktQueue;
}

void Decoder::setFrameQueue(FrameQueue* frameQueue) {
    m_frameQueue = frameQueue;
}

void Decoder::startDecoding() {
    if (m_running) return;
    m_abort = false;
    m_running = true;
    m_decodeThread = std::thread(&Decoder::decodeLoop, this);
}

void Decoder::stopDecoding() {
    if (!m_running) return;
    m_abort = true;
    if (m_decodeThread.joinable()) {
        m_decodeThread.join();
    }
    m_running = false;
}

void Decoder::flush() {
    if (m_codecCtx) {
        avcodec_flush_buffers(m_codecCtx);
    }
}

void Decoder::decodeLoop() {
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    if (!pkt || !frame) {
        LOGE(TAG, "Failed to allocate packet/frame");
        av_packet_free(&pkt);
        av_frame_free(&frame);
        return;
    }

    while (!m_abort) {
        if (m_packetQueue->pop(pkt, true) < 0) {
            break;
        }

        int ret = avcodec_send_packet(m_codecCtx, pkt);
        av_packet_unref(pkt);

        if (ret < 0) {
            LOGE(TAG, "Error sending packet to decoder");
            continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(m_codecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                LOGE(TAG, "Error decoding frame");
                break;
            }

            double pts = 0.0;
            if (frame->pts != AV_NOPTS_VALUE) {
                pts = av_q2d(m_timeBase) * frame->pts;
            }

            if (m_isVideo) {
                m_frameQueue->pushVideoFrame(frame, pts);
            } else {
                m_frameQueue->pushAudioFrame(frame, pts);
            }
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    m_running = false;
}

} // namespace ccplayer
