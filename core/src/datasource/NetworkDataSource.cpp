#include "NetworkDataSource.h"
#include "platform/log.h"
#include <cerrno>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#define TAG "NetworkDataSource"

namespace ccplayer {

NetworkDataSource::NetworkDataSource(const char* url) : m_url(url) {}

NetworkDataSource::~NetworkDataSource() {
    close();
}

int NetworkDataSource::open() {
    // 内层 avio_open2 复用 FFmpeg 原生协议（http/https/tcp/udp...），并透传 interrupt
    AVIOInterruptCB int_cb;
    int_cb.callback = interruptCallback;
    int_cb.opaque = this;

    int ret = avio_open2(&m_avio, m_url.c_str(), AVIO_FLAG_READ, &int_cb, nullptr);
    if (ret < 0) {
        LOGE(TAG, "avio_open2 failed: %s (ret=%d)", m_url.c_str(), ret);
        return -EIO;
    }

    // 探测可 seek 性（SEEK_CUR 0 仅读当前位置，开销小）
    m_seekable = avio_seek(m_avio, 0, SEEK_CUR) >= 0;
    return 0;
}

int NetworkDataSource::read(uint8_t* buf, int size) {
    if (!m_avio) return -EBADF;
    int ret = avio_read(m_avio, buf, size);
    if (ret == AVERROR_EOF) return 0;
    if (ret == AVERROR_EXIT) return -DS_INTERRUPTED;
    if (ret < 0) return -EIO;
    return ret;
}

int64_t NetworkDataSource::seek(int64_t offset, int whence) {
    if (!m_avio) return -EBADF;
    int64_t ret = avio_seek(m_avio, offset, whence);
    if (ret < 0) return -EIO;
    return ret;
}

int64_t NetworkDataSource::size() {
    if (!m_avio) return -1;
    int64_t sz = avio_size(m_avio);
    return sz < 0 ? -1 : sz;
}

void NetworkDataSource::close() {
    if (m_avio) {
        avio_close(m_avio);
        m_avio = nullptr;
    }
    m_seekable = false;
}

void NetworkDataSource::setInterrupt(const std::atomic<bool>* flag) {
    m_interrupt = flag;
    // 内层 avio 已通过 opaque=this + interruptCallback 动态读取 m_interrupt，无需再配置
}

int NetworkDataSource::interruptCallback(void* opaque) {
    auto* self = static_cast<NetworkDataSource*>(opaque);
    return (self->m_interrupt && self->m_interrupt->load()) ? 1 : 0;
}

} // namespace ccplayer
