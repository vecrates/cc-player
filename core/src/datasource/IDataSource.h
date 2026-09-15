#pragma once

#include <cstdio>
#include <cstdint>
#include <atomic>

namespace ccplayer {

// 数据源错误码约定（与 FFmpeg 解耦、平台无关）：
//   open(): 0 成功，<0 失败（取负 errno，如 -ENOENT / -EACCES / -ENETUNREACH）
//   read(): >0 读取字节数，0 = EOF，<0 错误（负 errno，或 -DS_INTERRUPTED 表示被中断）
constexpr int DS_INTERRUPTED = 10001;

/**
 * 数据源抽象接口：统一封装本地文件 / 网络 / fd（Android content:// 等）三类源的
 * 顺序读与随机访问能力。Demuxer 通过自定义 AVIO 桥接本接口，不感知底层来源。
 *
 * 生命周期：open() 成功后由持有方使用；close() 幂等；析构自动 close。
 * 线程模型：open/close 由控制线程调用；read/seek 仅由 demuxer 读线程调用（单线程，无并发）。
 */
class IDataSource {
public:
    virtual ~IDataSource() = default;

    virtual int open() = 0;
    virtual int read(uint8_t* buf, int size) = 0;
    virtual int64_t seek(int64_t offset, int whence) = 0;  // whence: SEEK_SET / SEEK_CUR / SEEK_END
    virtual int64_t size() = 0;                             // 总字节数，未知返回 -1
    virtual bool isSeekable() = 0;
    virtual void close() = 0;
    virtual void setInterrupt(const std::atomic<bool>* flag) = 0;
};

} // namespace ccplayer
