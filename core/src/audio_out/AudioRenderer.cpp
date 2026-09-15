#include "AudioRenderer.h"
#include "audio_out/AAudioOutput.h"
#include <cstdio>

extern "C" {
#include <libavutil/frame.h>
}

#define TAG "AudioRenderer"

namespace ccplayer {

AudioRenderer::AudioRenderer(FrameQueue* frameQueue)
    : m_frameQueue(frameQueue)
    , m_ringBuffer(nullptr)
    , m_audioOutput(nullptr)
    , m_swrCtx(nullptr)
    , m_sampleRate(0)
    , m_channels(0)
    , m_srcFormat(AV_SAMPLE_FMT_NONE)
    , m_swrToFltp(nullptr)
    , m_swrToS16(nullptr)
    , m_filterGraph(nullptr)
    , m_buffersrcCtx(nullptr)
    , m_buffersinkCtx(nullptr)
    , m_atempoCtx(nullptr)
{
    memset(&m_srcLayout, 0, sizeof(m_srcLayout));
}

AudioRenderer::~AudioRenderer() {
    close();
}

int AudioRenderer::open(int sampleRate, int channels, AVSampleFormat srcFmt, const AVChannelLayout* layout) {
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_srcFormat = srcFmt;
    av_channel_layout_copy(&m_srcLayout, layout);

    m_ringBuffer = new RingBuffer();

    // AAudio 仅支持 S16：源格式非 S16 时创建重采样器（原速路径）
    if (srcFmt != AV_SAMPLE_FMT_S16) {
        int ret = swr_alloc_set_opts2(&m_swrCtx,
            layout, AV_SAMPLE_FMT_S16, sampleRate,
            layout, srcFmt, sampleRate,
            0, nullptr);
        if (ret < 0 || !m_swrCtx || swr_init(m_swrCtx) < 0) {
            LOGE(TAG, "Failed to init SwrContext");
            if (m_swrCtx) swr_free(&m_swrCtx);
        }
    }

    // 变速链重采样器：FLTP -> S16（atempo 输出 FLTP）
    {
        int ret = swr_alloc_set_opts2(&m_swrToS16,
            layout, AV_SAMPLE_FMT_S16, sampleRate,
            layout, AV_SAMPLE_FMT_FLTP, sampleRate,
            0, nullptr);
        if (ret < 0 || !m_swrToS16 || swr_init(m_swrToS16) < 0) {
            LOGE(TAG, "Failed to init FLTP->S16 SwrContext");
            if (m_swrToS16) swr_free(&m_swrToS16);
        }
    }
    // 变速链重采样器：源格式 -> FLTP（源非 FLTP 时创建）
    if (srcFmt != AV_SAMPLE_FMT_FLTP) {
        int ret = swr_alloc_set_opts2(&m_swrToFltp,
            layout, AV_SAMPLE_FMT_FLTP, sampleRate,
            layout, srcFmt, sampleRate,
            0, nullptr);
        if (ret < 0 || !m_swrToFltp || swr_init(m_swrToFltp) < 0) {
            LOGE(TAG, "Failed to init src->FLTP SwrContext");
            if (m_swrToFltp) swr_free(&m_swrToFltp);
        }
    }

    auto* aaudio = new AAudioOutput();
    if (aaudio->open(sampleRate, channels) == 0) {
        m_audioOutput = aaudio;
        LOGI(TAG, "AAudio output opened");
        return 0;
    }
    LOGE(TAG, "Failed to open AAudio output");
    delete aaudio;
    return -1;
}

void AudioRenderer::close() {
    stop();
    destroyFilterGraphLocked();
    if (m_audioOutput) {
        delete m_audioOutput;
        m_audioOutput = nullptr;
    }
    delete m_ringBuffer;
    m_ringBuffer = nullptr;
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
    }
    if (m_swrToFltp) {
        swr_free(&m_swrToFltp);
        m_swrToFltp = nullptr;
    }
    if (m_swrToS16) {
        swr_free(&m_swrToS16);
        m_swrToS16 = nullptr;
    }
    av_channel_layout_uninit(&m_srcLayout);
}

void AudioRenderer::start() {
    if (!m_audioOutput || !m_ringBuffer) return;
    if (m_thread.joinable() && m_running.load()) return;

    if (m_thread.joinable() && !m_running.load()) {
        m_thread.join();
    }
    m_audioOutput->setCallback([this](uint8_t* buffer, int size) -> int {
        return onAudioData(buffer, size);
    });
    m_running = true;
    m_thread = std::thread(&AudioRenderer::audioLoop, this);
    m_audioOutput->resume();
}

void AudioRenderer::pause() {
    if (!m_thread.joinable()) return;
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (m_audioOutput) m_audioOutput->pause();
}

void AudioRenderer::resume() {
    start();
}

void AudioRenderer::stop() {
    if (m_thread.joinable()) {
        m_running = false;
        m_thread.join();
    }
    if (m_audioOutput) m_audioOutput->pause();
}

void AudioRenderer::seek(int serial, double targetSec) {
    m_serial = serial;
    m_seekTarget = targetSec;
    std::lock_guard<std::mutex> lock(m_ringMutex);
    resetLocked();
    m_lastPts = targetSec;
}

void AudioRenderer::setSpeed(double speed) {
    m_speed = speed;
}

double AudioRenderer::getCurrentPts() {
    std::lock_guard<std::mutex> lock(m_ringMutex);
    double lastPts = m_lastPts.load();
    if (!m_ringBuffer) return lastPts;
    int unread = unreadLocked();
    int bytesPerSec = m_sampleRate * m_channels * 2;
    if (bytesPerSec <= 0) return lastPts;
    // 变速后 ring buffer 存的是压缩 PCM，未消费墙钟时长需 ×speed 换算回媒体时长
    return lastPts - (double)unread / bytesPerSec * m_speed.load();
}

int AudioRenderer::onAudioData(uint8_t* buffer, int size) {
    std::lock_guard<std::mutex> lock(m_ringMutex);
    return readLocked(buffer, size);
}

void AudioRenderer::audioLoop() {
    AudioFrame audioFrame{};

    while (m_running) {
        // 检测倍速变化：在音频线程内重建滤镜图，避免跨线程操作滤镜
        double speed = m_speed.load();
        if (speed != m_appliedSpeed) {
            if (speed == 1.0) {
                destroyFilterGraphLocked();
            } else {
                if (initFilterGraphLocked() < 0) {
                    LOGE(TAG, "Failed to init atempo filter graph");
                }
            }
            m_appliedSpeed = speed;
            m_tempoLastSerial = -1; // 强制变速游标在下一帧重置
            // 清空旧速残留缓冲，避免新旧速数据混合导致时钟跳变
            {
                std::lock_guard<std::mutex> lock(m_ringMutex);
                resetLocked();
            }
        }

        if (m_frameQueue->popAudioFrame(&audioFrame, true) < 0) {
            if (!m_running) break;
            continue;
        }

        // 丢弃旧 seek 代际的帧
        if (audioFrame.serial != m_serial.load()) {
            av_frame_free(&audioFrame.frame);
            continue;
        }

        // 丢弃 seek 目标之前的帧（关键帧到目标之间）
        if (audioFrame.pts < m_seekTarget.load()) {
            av_frame_free(&audioFrame.frame);
            continue;
        }

        if (m_appliedSpeed == 1.0) {
            // 原速路径：重采样为 S16 交错
            const uint8_t* srcData;
            int srcSize;
            bool allocated = false;

            if (m_swrCtx && audioFrame.frame->format != AV_SAMPLE_FMT_S16) {
                int nbSamples = audioFrame.frame->nb_samples;
                int outSamples = swr_get_out_samples(m_swrCtx, nbSamples);
                int bufSize = outSamples * m_channels * 2;
                auto* buf = new uint8_t[bufSize];

                int converted = swr_convert(m_swrCtx,
                    &buf, outSamples,
                    (const uint8_t**)audioFrame.frame->data, nbSamples);
                if (converted > 0) {
                    srcData = buf;
                    srcSize = converted * m_channels * 2;
                    allocated = true;
                } else {
                    delete[] buf;
                    av_frame_free(&audioFrame.frame);
                    continue;
                }
            } else {
                srcData = audioFrame.frame->data[0];
                srcSize = audioFrame.frame->nb_samples * m_channels * 2;
            }

            writeToRing(srcData, srcSize, audioFrame.serial, audioFrame.pts);

            if (allocated) {
                delete[] srcData;
            }
        } else {
            // 变速路径：FLTP → atempo → S16 → ring buffer
            processFrameTempo(audioFrame.frame, audioFrame.serial);
        }

        av_frame_free(&audioFrame.frame);
    }
}

// 将 S16 数据写入环形缓冲（带 serial 校验，串行背压）；成功返回 0，帧被丢弃返回 -1
int AudioRenderer::writeToRing(const uint8_t* src, int len, int serial, double pts) {
    int written = 0;
    bool dropped = false;
    while (written < len && m_running) {
        int n;
        {
            std::lock_guard<std::mutex> lock(m_ringMutex);
            n = writeLocked(src + written, len - written, serial);
        }
        if (n > 0) {
            written += n;
        } else if (n < 0) {
            dropped = true; // serial 不匹配，整帧丢弃
            break;
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }

    // 仅当帧确实写入后才更新时钟基准（避免旧帧覆盖 seek 基准）
    if (!dropped && written > 0) {
        m_lastPts = pts;
    }
    return dropped ? -1 : 0;
}

// 变速路径：源格式 → FLTP → atempo → FLTP → S16 → ring buffer
int AudioRenderer::processFrameTempo(AVFrame* frame, int serial) {
    if (!m_filterGraph || !m_buffersrcCtx || !m_buffersinkCtx || !m_swrToS16) {
        return -1;
    }

    // serial 变化（seek/speed 切换）时重置媒体时间游标
    if (serial != m_tempoLastSerial) {
        m_tempoLastSerial = serial;
        m_tempoMediaCursor = frame->pts;
    }

    // 1) 源格式 -> FLTP（源已是 FLTP 则直接使用）
    AVFrame* fltpFrame = nullptr;
    bool ownFltp = false;
    if (frame->format == AV_SAMPLE_FMT_FLTP) {
        fltpFrame = frame;
    } else {
        if (!m_swrToFltp) return -1;
        fltpFrame = av_frame_alloc();
        if (!fltpFrame) return -1;
        fltpFrame->sample_rate = m_sampleRate;
        fltpFrame->format = AV_SAMPLE_FMT_FLTP;
        av_channel_layout_copy(&fltpFrame->ch_layout, &frame->ch_layout);
        fltpFrame->nb_samples = frame->nb_samples;
        if (av_frame_get_buffer(fltpFrame, 0) < 0) {
            av_frame_free(&fltpFrame);
            return -1;
        }
        fltpFrame->pts = frame->pts;
        int converted = swr_convert(m_swrToFltp, fltpFrame->data, frame->nb_samples,
                                    (const uint8_t**)frame->data, frame->nb_samples);
        if (converted < 0) {
            av_frame_free(&fltpFrame);
            return -1;
        }
        fltpFrame->nb_samples = converted;
        ownFltp = true;
    }

    // 2) 送入滤镜
    if (av_buffersrc_add_frame(m_buffersrcCtx, fltpFrame) < 0) {
        if (ownFltp) av_frame_free(&fltpFrame);
        return -1;
    }
    if (ownFltp) av_frame_free(&fltpFrame);

    // 3) 取出滤镜输出（FLTP）-> S16 -> ring buffer
    AVFrame* out = av_frame_alloc();
    if (!out) return -1;

    while (true) {
        int ret = av_buffersink_get_frame(m_buffersinkCtx, out);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        int outSamples = out->nb_samples;
        int bufSize = outSamples * m_channels * 2;
        auto* buf = new uint8_t[bufSize];
        uint8_t* outPtr = buf;

        int converted = swr_convert(m_swrToS16, &outPtr, outSamples,
                                    (const uint8_t**)out->data, outSamples);
        if (converted > 0) {
            // 媒体起始时间 = 游标；游标按「输出样本数 / sample_rate × speed」推进
            // （变速后输出 PCM 时长被压缩，但媒体时间仍按原媒体轴推进）
            double mediaStart = m_tempoMediaCursor;
            writeToRing(buf, converted * m_channels * 2, serial, mediaStart);
            m_tempoMediaCursor += (double)converted / m_sampleRate * m_appliedSpeed;
        }
        delete[] buf;
        av_frame_unref(out);
    }

    av_frame_free(&out);
    return 0;
}

// 创建 atempo 滤镜图：abuffer → atempo → abuffersink（仅音频线程调用）
int AudioRenderer::initFilterGraphLocked() {
    destroyFilterGraphLocked();

    m_filterGraph = avfilter_graph_alloc();
    if (!m_filterGraph) return -1;

    const AVFilter* abuffer = avfilter_get_by_name("abuffer");
    const AVFilter* atempo = avfilter_get_by_name("atempo");
    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");
    if (!abuffer || !atempo || !abuffersink) {
        destroyFilterGraphLocked();
        return -1;
    }

    // 声道布局描述字符串（如 "stereo"）
    char chLayout[128] = {0};
    av_channel_layout_describe(&m_srcLayout, chLayout, sizeof(chLayout));

    // abuffer：输入 FLTP
    char args[512];
    snprintf(args, sizeof(args),
             "time_base=1/%d:sample_rate=%d:sample_fmt=fltp:ch_layout=%s",
             m_sampleRate, m_sampleRate, chLayout);
    if (avfilter_graph_create_filter(&m_buffersrcCtx, abuffer, "in", args, nullptr, m_filterGraph) < 0) {
        LOGE(TAG, "Failed to create abuffer");
        destroyFilterGraphLocked();
        return -1;
    }

    // atempo：变速不变调
    char tempoArgs[64];
    snprintf(tempoArgs, sizeof(tempoArgs), "tempo=%.6f", m_speed.load());
    if (avfilter_graph_create_filter(&m_atempoCtx, atempo, "atempo", tempoArgs, nullptr, m_filterGraph) < 0) {
        LOGE(TAG, "Failed to create atempo");
        destroyFilterGraphLocked();
        return -1;
    }

    // abuffersink：输出 FLTP
    if (avfilter_graph_create_filter(&m_buffersinkCtx, abuffersink, "out", nullptr, nullptr, m_filterGraph) < 0) {
        LOGE(TAG, "Failed to create abuffersink");
        destroyFilterGraphLocked();
        return -1;
    }

    if (avfilter_link(m_buffersrcCtx, 0, m_atempoCtx, 0) < 0 ||
        avfilter_link(m_atempoCtx, 0, m_buffersinkCtx, 0) < 0) {
        LOGE(TAG, "Failed to link filter graph");
        destroyFilterGraphLocked();
        return -1;
    }

    if (avfilter_graph_config(m_filterGraph, nullptr) < 0) {
        LOGE(TAG, "Failed to config filter graph");
        destroyFilterGraphLocked();
        return -1;
    }

    LOGI(TAG, "atempo filter graph initialized, speed=%.2f", m_speed.load());
    return 0;
}

void AudioRenderer::destroyFilterGraphLocked() {
    if (m_filterGraph) {
        avfilter_graph_free(&m_filterGraph);
        m_filterGraph = nullptr;
    }
    m_buffersrcCtx = nullptr;
    m_buffersinkCtx = nullptr;
    m_atempoCtx = nullptr;
}

int AudioRenderer::writeLocked(const uint8_t* src, int len, int serial) {
    if (serial != m_serial.load()) return -1; // 旧代际，拒绝写入

    int w = m_ringBuffer->writePos;
    int r = m_ringBuffer->readPos;
    int avail = RingBuffer::SIZE - 1 - ((w - r + RingBuffer::SIZE) % RingBuffer::SIZE);
    if (len > avail) len = avail;
    if (len <= 0) return 0;

    int first = std::min(len, RingBuffer::SIZE - w);
    memcpy(m_ringBuffer->data + w, src, first);
    if (len > first) memcpy(m_ringBuffer->data, src + first, len - first);
    m_ringBuffer->writePos = (w + len) % RingBuffer::SIZE;
    return len;
}

int AudioRenderer::readLocked(uint8_t* dst, int len) {
    int w = m_ringBuffer->writePos;
    int r = m_ringBuffer->readPos;
    int avail = (w - r + RingBuffer::SIZE) % RingBuffer::SIZE;
    if (len > avail) len = avail;
    if (len <= 0) return 0;

    int first = std::min(len, RingBuffer::SIZE - r);
    memcpy(dst, m_ringBuffer->data + r, first);
    if (len > first) memcpy(dst + first, m_ringBuffer->data, len - first);
    m_ringBuffer->readPos = (r + len) % RingBuffer::SIZE;
    return len;
}

void AudioRenderer::resetLocked() {
    m_ringBuffer->writePos = 0;
    m_ringBuffer->readPos = 0;
}

int AudioRenderer::unreadLocked() const {
    if (!m_ringBuffer) return 0;
    int w = m_ringBuffer->writePos;
    int r = m_ringBuffer->readPos;
    return (w - r + RingBuffer::SIZE) % RingBuffer::SIZE;
}

} // namespace ccplayer
