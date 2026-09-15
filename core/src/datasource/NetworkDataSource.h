#pragma once

#include "datasource/IDataSource.h"
#include <string>

struct AVIOContext;

namespace ccplayer {

/**
 * 网络/协议数据源：内部用 FFmpeg avio_open 复用原生协议（http/https/tcp/udp 等字节流协议），
 * 对外仍暴露为 IDataSource，使 Demuxer 不必感知底层协议差异。
 */
class NetworkDataSource : public IDataSource {
public:
    explicit NetworkDataSource(const char* url);
    ~NetworkDataSource() override;

    int open() override;
    int read(uint8_t* buf, int size) override;
    int64_t seek(int64_t offset, int whence) override;
    int64_t size() override;
    bool isSeekable() override { return m_seekable; }
    void close() override;
    void setInterrupt(const std::atomic<bool>* flag) override;

private:
    static int interruptCallback(void* opaque);

    std::string m_url;
    AVIOContext* m_avio = nullptr;
    bool m_seekable = false;
    const std::atomic<bool>* m_interrupt = nullptr;
};

} // namespace ccplayer
