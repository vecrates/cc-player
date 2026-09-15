// 核心模块单元测试（无外部依赖的轻量 harness）
// 覆盖：Clock（时钟插值）、AudioVideoSyncer（同步/丢帧）、CommandQueue（命令队列）

#include <cstdio>
#include <cmath>
#include <cstring>
#include <thread>
#include <chrono>

#include "sync/Sync.h"
#include "queue/CommandQueue.h"
#include "datasource/FileDataSource.h"
#include "datasource/FdDataSource.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
static int openFdReadOnly(const char* path) { return _open(path, _O_RDONLY | _O_BINARY); }
#else
#include <fcntl.h>
#include <unistd.h>
static int openFdReadOnly(const char* path) { return ::open(path, O_RDONLY); }
#endif

using namespace ccplayer;

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++g_failures; \
    } \
} while (0)

#define CHECK_NEAR(a, b, eps) do { \
    double _a = (double)(a), _b = (double)(b); \
    if (std::fabs(_a - _b) > (eps)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s(%.4f) vs %s(%.4f)\n", \
                     __FILE__, __LINE__, #a, _a, #b, _b); \
        ++g_failures; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    std::printf("RUN %s\n", #fn); \
    fn(); \
} while (0)

// ---------- Clock ----------

static void test_clock_interpolation() {
    Clock clock;
    clock.setPTS(10.0);
    CHECK_NEAR(clock.getPTS(), 10.0, 0.01);

    // 插值：时间流逝后 PTS 线性增加
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    double t = clock.getPTS();
    CHECK(t > 10.03);
    CHECK(t < 10.25);

    // reset 归零
    clock.reset();
    CHECK_NEAR(clock.getPTS(), 0.0, 0.01);
}

// ---------- AudioVideoSyncer ----------

static void test_syncer_delay() {
    Clock audioClock;
    AudioVideoSyncer syncer;
    syncer.setMasterClock(&audioClock);
    audioClock.setPTS(10.0);

    // 视频帧领先 0.1s：返回正延迟
    CHECK_NEAR(syncer.computeVideoDelay(10.1), 0.1, 0.03);

    // 领先过多：钳制到 maxDelay(0.1)
    CHECK_NEAR(syncer.computeVideoDelay(100.0), 0.1, 0.01);

    // 落后：返回负延迟（不钳制）
    CHECK(syncer.computeVideoDelay(9.0) < -0.9);
}

static void test_syncer_drop() {
    Clock audioClock;
    AudioVideoSyncer syncer;
    syncer.setMasterClock(&audioClock);
    audioClock.setPTS(10.0);

    // 落后 0.2s > dropThreshold(0.1)：应丢帧
    CHECK(syncer.shouldDrop(9.8));

    // 落后 0.05s < 0.1：不丢
    CHECK(!syncer.shouldDrop(9.96));

    // 领先：不丢
    CHECK(!syncer.shouldDrop(10.1));
}

static void test_syncer_provider() {
    AudioVideoSyncer syncer;
    double master = 10.0;
    syncer.setMasterTimeProvider([&master] { return master; });

    CHECK_NEAR(syncer.computeVideoDelay(10.1), 0.1, 0.03);

    // 变更主时钟源后立即可见
    master = 11.0;
    CHECK_NEAR(syncer.computeVideoDelay(10.1), -0.9, 0.05);
}

// ---------- CommandQueue ----------

static void test_command_queue_basic() {
    CommandQueue<int> q;
    CHECK(q.empty());

    q.push(1);
    q.push(2);

    int v = 0;
    CHECK(q.tryPop(v) && v == 1);
    CHECK(q.tryPop(v) && v == 2);
    CHECK(!q.tryPop(v));
    CHECK(q.empty());
}

static void test_command_queue_blocking_pop() {
    CommandQueue<int> q;
    std::thread producer([&q]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        q.push(42);
    });

    auto v = q.pop();
    CHECK(v.has_value() && *v == 42);
    producer.join();
}

static void test_command_queue_abort_reset() {
    CommandQueue<int> q;
    q.abort();

    // abort 后阻塞 pop 返回空
    CHECK(!q.pop().has_value());

    // abort 后 push 被丢弃
    q.push(1);
    CHECK(q.empty());

    // reset 后恢复正常
    q.reset();
    q.push(7);
    auto v = q.pop();
    CHECK(v.has_value() && *v == 7);
}

// ---------- DataSource ----------

static const char* g_tmpPath = "ccplayer_test_tmp.bin";

static void writeTestFile(const char* path, const uint8_t* data, size_t len) {
    FILE* f = std::fopen(path, "wb");
    if (f) {
        std::fwrite(data, 1, len, f);
        std::fclose(f);
    }
}

static void test_file_datasource() {
    const uint8_t data[] = "hello world, ccplayer";
    size_t len = sizeof(data) - 1;
    writeTestFile(g_tmpPath, data, len);

    FileDataSource ds(g_tmpPath);
    CHECK(ds.open() == 0);
    CHECK(ds.isSeekable());
    CHECK(ds.size() == (int64_t)len);

    uint8_t buf[64];
    int n = ds.read(buf, 5);
    CHECK(n == 5);
    CHECK(std::memcmp(buf, "hello", 5) == 0);

    CHECK(ds.seek(6, SEEK_SET) == 6);
    n = ds.read(buf, 5);
    CHECK(n == 5);
    CHECK(std::memcmp(buf, "world", 5) == 0);

    ds.seek((int64_t)len, SEEK_SET);
    CHECK(ds.read(buf, sizeof(buf)) == 0);

    ds.close();

    FileDataSource bad("nonexistent_ccplayer_test.bin");
    CHECK(bad.open() < 0);

    std::remove(g_tmpPath);
}

static void test_fd_datasource() {
    const uint8_t data[] = "0123456789ABCDEF";
    size_t len = sizeof(data) - 1;
    writeTestFile(g_tmpPath, data, len);

    int fd = openFdReadOnly(g_tmpPath);
    CHECK(fd >= 0);

    // offset=5, length=5 → 数据段 "56789"
    FdDataSource ds(fd, 5, 5);
    CHECK(ds.open() == 0);
    CHECK(ds.isSeekable());
    CHECK(ds.size() == 5);

    uint8_t buf[16];
    int n = ds.read(buf, 5);
    CHECK(n == 5);
    CHECK(std::memcmp(buf, "56789", 5) == 0);

    // 数据段末尾（fd 后续还有内容，但 length 钳制）返回 0
    CHECK(ds.read(buf, sizeof(buf)) == 0);

    // seek 回起点读前 2 字节
    CHECK(ds.seek(0, SEEK_SET) == 0);
    n = ds.read(buf, 2);
    CHECK(n == 2);
    CHECK(std::memcmp(buf, "56", 2) == 0);

    ds.close();  // 关闭 fd

    std::remove(g_tmpPath);
}

int main() {
    RUN_TEST(test_clock_interpolation);
    RUN_TEST(test_syncer_delay);
    RUN_TEST(test_syncer_drop);
    RUN_TEST(test_syncer_provider);
    RUN_TEST(test_command_queue_basic);
    RUN_TEST(test_command_queue_blocking_pop);
    RUN_TEST(test_command_queue_abort_reset);
    RUN_TEST(test_file_datasource);
    RUN_TEST(test_fd_datasource);

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "%d FAILURE(S)\n", g_failures);
    return 1;
}
