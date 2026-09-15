#pragma once

#include "datasource/IDataSource.h"
#include <cstdio>
#include <string>

namespace ccplayer {

/**
 * 本地文件数据源：用 C 标准库 fopen/fread 读取，天然跨平台（Windows 亦可用）。
 * 传入的 path 应为已剥离 file:// 前缀的本地文件路径。
 */
class FileDataSource : public IDataSource {
public:
    explicit FileDataSource(const char* path);
    ~FileDataSource() override;

    int open() override;
    int read(uint8_t* buf, int size) override;
    int64_t seek(int64_t offset, int whence) override;
    int64_t size() override;
    bool isSeekable() override { return m_seekable; }
    void close() override;
    void setInterrupt(const std::atomic<bool>* flag) override { m_interrupt = flag; }

private:
    std::string m_path;
    FILE* m_file = nullptr;
    int64_t m_size = -1;
    bool m_seekable = false;
    const std::atomic<bool>* m_interrupt = nullptr;
};

} // namespace ccplayer
