#include "Decoder.h"
#include "platform/log.h"

#define TAG "Decoder"

namespace ccplayer {

Decoder::Decoder()
    : m_codecCtx(nullptr)
    , m_isVideo(false)
    , m_packetQueue(nullptr)
    , m_frameQueue(nullptr)
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
    stop();
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

void Decoder::start() {
    // 线程自然退出（队列 abort/EOF）时先回收再重建
    if (m_thread.joinable() && !m_running.load()) {
        m_thread.join();
    }
    if (!m_thread.joinable()) {
        m_paused = false;
        m_running = true;
        m_cmdQueue.reset();
        m_cmdQueue.clear(); // 清空残留命令，避免误执行
        m_thread = std::thread(&Decoder::decodeLoop, this);
    } else {
        m_cmdQueue.push({DecoderCommandType::Resume, 0});
    }
}

void Decoder::pause() {
    m_cmdQueue.push({DecoderCommandType::Pause, 0});
}

void Decoder::resume() {
    m_cmdQueue.push({DecoderCommandType::Resume, 0});
}

void Decoder::flush() {
    if (!m_running.load()) {
        // 线程未运行：无并发，直接同步执行
        if (m_codecCtx) {
            avcodec_flush_buffers(m_codecCtx);
        }
        return;
    }

    // 线程运行中：投递 Flush 命令，等待解码线程执行完成
    std::unique_lock<std::mutex> lock(m_flushMutex);
    m_flushDone = false;
    m_cmdQueue.push({DecoderCommandType::Flush, 0});
    m_flushCond.wait(lock, [this] { return m_flushDone.load(); });
}

void Decoder::stop() {
    if (!m_thread.joinable()) return;

    m_cmdQueue.push({DecoderCommandType::Stop, 0});
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_running = false;
    m_cmdQueue.clear(); // 清空残留命令，避免下次 start 后误执行
}

void Decoder::decodeLoop() {
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    if (!pkt || !frame) {
        LOGE(TAG, "Failed to allocate packet/frame");
        av_packet_free(&pkt);
        av_frame_free(&frame);
        m_running = false;
        return;
    }

    while (m_running) {
        // 1) 处理所有积压命令
        DecoderCommand cmd;
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

        // 3) 带超时取包：超时返回以周期性检查命令（flush/pause/stop）
        int pktSerial = 0;
        int ret = m_packetQueue->popTimeout(pkt, &pktSerial, 50);
        if (ret == -2) {
            continue; // 超时，回顶部检查命令
        }
        if (ret < 0) {
            break; // 队列 abort
        }

        // 4) 解码
        int sendRet = avcodec_send_packet(m_codecCtx, pkt);
        av_packet_unref(pkt);

        if (sendRet < 0) {
            LOGE(TAG, "Error sending packet to decoder");
            continue;
        }

        int recvRet = 0;
        while (recvRet >= 0) {
            recvRet = avcodec_receive_frame(m_codecCtx, frame);
            if (recvRet == AVERROR(EAGAIN) || recvRet == AVERROR_EOF) {
                break;
            }
            if (recvRet < 0) {
                LOGE(TAG, "Error decoding frame");
                break;
            }

            double pts = 0.0;
            if (frame->pts != AV_NOPTS_VALUE) {
                pts = av_q2d(m_timeBase) * frame->pts;
            }

            if (m_isVideo) {
                m_frameQueue->pushVideoFrame(frame, pts, pktSerial);
            } else {
                m_frameQueue->pushAudioFrame(frame, pts, pktSerial);
            }
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    m_running = false;
}

void Decoder::handleCommand(const DecoderCommand& cmd) {
    switch (cmd.type) {
        case DecoderCommandType::Start:
            m_paused = false;
            break;
        case DecoderCommandType::Pause:
            m_paused = true;
            break;
        case DecoderCommandType::Resume:
            m_paused = false;
            break;
        case DecoderCommandType::Flush:
            if (m_codecCtx) {
                avcodec_flush_buffers(m_codecCtx);
            }
            {
                std::lock_guard<std::mutex> lock(m_flushMutex);
                m_flushDone = true;
            }
            m_flushCond.notify_all();
            break;
        case DecoderCommandType::Stop:
            m_running = false;
            break;
    }
}

} // namespace ccplayer
