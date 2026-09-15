#include "FileDataSource.h"
#include "platform/log.h"
#include <cerrno>
#include <sys/types.h>

#define TAG "FileDataSource"

namespace ccplayer {

// 跨平台 64 位 seek/tell：POSIX 用 fseeko/ftello，Windows 用 _fseeki64/_ftelli64
static int fileSeek(FILE* f, int64_t offset, int whence) {
#ifdef _WIN32
    return _fseeki64(f, offset, whence) == 0 ? 0 : -1;
#else
    return fseeko(f, (off_t)offset, whence) == 0 ? 0 : -1;
#endif
}

static int64_t fileTell(FILE* f) {
#ifdef _WIN32
    return (int64_t)_ftelli64(f);
#else
    return (int64_t)ftello(f);
#endif
}

FileDataSource::FileDataSource(const char* path) : m_path(path) {}

FileDataSource::~FileDataSource() {
    close();
}

int FileDataSource::open() {
    m_file = std::fopen(m_path.c_str(), "rb");
    if (!m_file) {
        LOGE(TAG, "fopen failed: %s (errno=%d)", m_path.c_str(), errno);
        return -errno;
    }

    // 探测大小与可 seek 性：定位到末尾再回到开头
    if (fileSeek(m_file, 0, SEEK_END) == 0) {
        int64_t sz = fileTell(m_file);
        if (sz >= 0) {
            m_size = sz;
            m_seekable = true;
        }
        fileSeek(m_file, 0, SEEK_SET);
    } else {
        m_seekable = false;
        m_size = -1;
    }
    return 0;
}

int FileDataSource::read(uint8_t* buf, int size) {
    if (!m_file) return -EBADF;
    if (m_interrupt && m_interrupt->load()) return -DS_INTERRUPTED;
    size_t n = std::fread(buf, 1, (size_t)size, m_file);
    if (n > 0) return (int)n;
    if (std::feof(m_file)) return 0;
    return -EIO;
}

int64_t FileDataSource::seek(int64_t offset, int whence) {
    if (!m_file) return -EBADF;
    if (fileSeek(m_file, offset, whence) != 0) return -errno;
    int64_t pos = fileTell(m_file);
    return pos < 0 ? -errno : pos;
}

int64_t FileDataSource::size() {
    return m_size;
}

void FileDataSource::close() {
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
    m_size = -1;
    m_seekable = false;
}

} // namespace ccplayer
