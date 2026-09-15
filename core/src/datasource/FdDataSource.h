#pragma once

#include "datasource/IDataSource.h"

namespace ccplayer {

/**
 * fd 数据源（跨平台）：以「fd + offset + length」描述一段可读数据。
 * 典型场景：Android content:// 经 opener 打开后得到 fd，再交给本类。
 * read/seek/close 走 POSIX read/lseek/close（Windows 映射为 _read/_lseeki64/_close）。
 *
 * offset: 数据在 fd 中的起始字节；length: 数据长度，<0 表示未知。
 * fd 所有权随构造转移给本类，close()（或析构）时负责关闭。
 */
class FdDataSource : public IDataSource {
public:
    FdDataSource(int fd, int64_t offset, int64_t length);
    ~FdDataSource() override;

    int open() override;
    int read(uint8_t* buf, int size) override;
    int64_t seek(int64_t offset, int whence) override;
    int64_t size() override;
    bool isSeekable() override { return m_seekable; }
    void close() override;
    void setInterrupt(const std::atomic<bool>* flag) override { m_interrupt = flag; }

private:
    int m_fd;
    int64_t m_offset;
    int64_t m_length;   // <0 表示未知
    int64_t m_size = -1;
    int64_t m_pos = 0;  // 相对 m_offset 的当前读位置
    bool m_seekable = false;
    const std::atomic<bool>* m_interrupt = nullptr;
};

} // namespace ccplayer
