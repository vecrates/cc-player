#include "FdDataSource.h"
#include "platform/log.h"
#include <cerrno>
#include <sys/types.h>

#ifdef _WIN32
#include <io.h>
static int64_t fdSeek(int fd, int64_t off, int whence) { return (int64_t)_lseeki64(fd, off, whence); }
static int64_t fdRead(int fd, void* buf, unsigned int n) { return (int64_t)_read(fd, buf, n); }
static int fdClose(int fd) { return _close(fd); }
#else
#include <unistd.h>
static int64_t fdSeek(int fd, int64_t off, int whence) { return (int64_t)::lseek(fd, (off_t)off, whence); }
static int64_t fdRead(int fd, void* buf, size_t n) { return (int64_t)::read(fd, buf, n); }
static int fdClose(int fd) { return ::close(fd); }
#endif

#define TAG "FdDataSource"

namespace ccplayer {

FdDataSource::FdDataSource(int fd, int64_t offset, int64_t length)
    : m_fd(fd), m_offset(offset), m_length(length) {}

FdDataSource::~FdDataSource() {
    close();
}

int FdDataSource::open() {
    if (m_fd < 0) return -EBADF;

    // 定位到数据起点（offset 可能非 0，如 AssetFileDescriptor.startOffset）
    if (fdSeek(m_fd, m_offset, SEEK_SET) < 0) {
        // 无法 seek：仍可顺序读，但标记不可随机访问
        m_seekable = false;
        m_size = m_length;
        m_pos = 0;
        return 0;
    }

    if (m_length >= 0) {
        m_seekable = true;
        m_size = m_length;
    } else {
        // 长度未知：尝试用 SEEK_END 探测
        int64_t end = fdSeek(m_fd, 0, SEEK_END);
        if (end >= 0) {
            m_seekable = true;
            m_size = end - m_offset;
            if (m_size < 0) m_size = 0;
            fdSeek(m_fd, m_offset, SEEK_SET);
        } else {
            m_seekable = false;
            m_size = -1;
        }
    }
    m_pos = 0;
    return 0;
}

int FdDataSource::read(uint8_t* buf, int size) {
    if (m_fd < 0) return -EBADF;
    if (m_interrupt && m_interrupt->load()) return -DS_INTERRUPTED;

    // 已知长度时钳制到数据末尾，避免读到 fd 后续的无关内容
    if (m_length >= 0) {
        int64_t remain = m_length - m_pos;
        if (remain <= 0) return 0;
        if ((int64_t)size > remain) size = (int)remain;
    }

    int64_t n = fdRead(m_fd, buf, (size_t)size);
    if (n > 0) {
        m_pos += n;
        return (int)n;
    }
    if (n == 0) return 0;  // EOF
    return -errno;
}

int64_t FdDataSource::seek(int64_t offset, int whence) {
    if (m_fd < 0) return -EBADF;
    if (!m_seekable) return -ESPIPE;

    int64_t base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = m_pos; break;
        case SEEK_END: base = m_size; break;
        default: return -EINVAL;
    }
    int64_t target = base + offset;
    if (target < 0) return -EINVAL;
    if (m_length >= 0 && target > m_length) target = m_length;  // 钳制到数据末尾

    if (fdSeek(m_fd, m_offset + target, SEEK_SET) < 0) return -errno;
    m_pos = target;
    return target;
}

int64_t FdDataSource::size() {
    return m_size;
}

void FdDataSource::close() {
    if (m_fd >= 0) {
        fdClose(m_fd);
        m_fd = -1;
    }
    m_seekable = false;
    m_size = -1;
    m_pos = 0;
}

} // namespace ccplayer
