# 核心设计方案

本文档描述 CCPlayer 内核的关键设计决策与实现机制。

## 1. 播放器状态机

状态定义于 `PlayerTypes.h`，由 `PlayerImpl` 的 `std::atomic<PlayerState> m_state`
维护。除 `setDataSource` 在调用线程同步写外，其余状态迁移全部收敛到**控制线程**执行，
迁移入口先校验当前状态，非法迁移只打印警告并直接返回。

```
        setDataSource              prepare 成功
 Idle ───────────────► Initialized ────────────► Prepared
   ▲                       │                        │
   │                       │ prepare 失败           │ start
   │ release               ▼                        ▼
   └──────── Stopped ◄── Error                  Started
                ▲                                   │ ▲
                │ stop                              │ │ resume
                │              pause                ▼ │
                └─────────────────────────────  Paused
```

| 方法 | 允许的当前状态 | 迁移到 | 语义 |
|------|----------------|--------|------|
| `setDataSource` | Idle / Stopped | Initialized | 同步（调用线程） |
| `prepare` | Initialized | Prepared 或 Error | 异步命令 |
| `start` | Prepared / Paused | Started | 异步命令 |
| `pause` | Started | Paused | 异步命令 |
| `resume` | Paused | Started | 异步命令 |
| `stop` | 任意（内部停止线程） | Stopped | 同步等待完成 |
| `release` | 任意（stop + 关闭解码器/封装） | Idle | 同步等待完成 |
| `seekTo` | Started / Paused | 不变 | 异步命令 |

**设计要点：**

- `prepare()` 不再阻塞调用线程：`PlayerImpl::prepare()` 仅投递 `Prepare` 命令，
  由控制线程在 `doPrepare()` 中完成打开封装、打开解码器、装配队列与渲染/音频链路。
- 回调通知（`onPrepared` / `onError` 等）在控制线程、**无锁状态下触发**，
  避免持锁调用用户代码造成死锁。
- `stop()` / `release()` 投递命令后阻塞在 `m_waitCond` 上，等待控制线程执行完
  `doStop` / 关闭逻辑并 `notifyDone()` 后返回，保证调用方观察到的状态已收敛。

## 2. 并发模型：控制线程 + 命令队列（Actor）

内核采用 Actor 模型组织并发，避免多线程直接触碰共享的 FFmpeg 上下文：

### 控制 Actor（PlayerImpl）

- 构造函数启动控制线程 `controlLoop()`，阻塞消费 `CommandQueue<PlayerCommand>`。
- 命令类型 `PlayerCommandType`：`Prepare / Start / Pause / Resume / Seek / Stop / Release / Eos`，
  显式 `enum + 参数结构体`（而非 `std::function`），便于测试与避免生命周期问题。
- 控制类接口（`prepare/start/pause/resume/seekTo`）`push` 后立即返回；
  查询接口（`getState/getCurrentPosition/getDuration`）为同步原子快照。
- `handleCommand()` 在控制线程串行执行，状态机迁移与流水线编排均收敛于此。

### 读包 / 解码 Actor

- `Demuxer` 内部命令队列（`Start / Pause / Resume / Seek / Stop / Close`），
  读包线程 `threadLoop()` 先 `tryPop` 处理积压命令，再 `av_read_frame`；
  暂停态挂起等待命令（线程不退出）。
  通过 `AVIOInterruptCB`（`interruptCallback`）打断阻塞中的 `av_read_frame`，
  使 pause/seek/stop 能尽快生效。
- `Decoder` 对称 Actor 化（`Start / Pause / Resume / Flush / Stop`），
  `flush()` 投递 `Flush` 命令并同步等待，由解码线程自身执行 `avcodec_flush_buffers`，
  杜绝与控制线程的并发；取包用 `popTimeout(..., 50ms)` 周期性检查命令。

### 渲染 / 音频线程封装

- `VideoRenderer` / `AudioRenderer` 为**纯线程封装**（非 Actor）：内部自管线程与
  EGL / 环形缓冲资源，对外提供 `start / pause / stop / seek` 等接口。
  因不直接持有 FFmpeg 上下文，无需命令队列串行化，避免过度设计。

### 线程启停

- 各 Actor 用 `std::atomic<bool> m_running` / `m_paused` 控制启停与挂起；
  `start()` 在旧线程自然退出（EOF/abort）后先 join 再重建，否则投递 `Resume` 复用。
- `stop()` 投递停止命令并 join 线程，随后 `clear()` 清空残留命令，避免下次 start 误执行。

## 3. 缓冲队列

### CommandQueue\<T\>（命令队列）

- 通用线程安全队列，供控制线程与各 Actor 复用，基于 `std::deque` + `mutex` + `condition_variable`。
- 语义：`push` 投递并唤醒一个等待者（abort 后丢弃）；`pop` 阻塞弹出（abort 且空时返回 `nullopt`）；
  `tryPop` 非阻塞；`abort` 唤醒所有等待者；`reset` 清除 abort 标志供复用。

### PacketQueue（包队列）

- 链表节点 `PacketNode` 存放 `AVPacket*` 并携带 `serial`（seek 代际），
  跟踪元素数与累计字节数 `m_byteSize`。
- `push(pkt, serial)` / `pop(pkt, &serial, block)` / `popTimeout(pkt, &serial, timeoutMs)`
  （超时返回 -2，供解码线程周期性检查命令）；支持 `flush`、`abort`、`reset`。
- `setMaxByteSize` 做背压控制（防止读包过快导致内存膨胀）。

### FrameQueue（帧队列）

- 节点 `FrameNode` 区分视频/音频帧，附带 PTS、serial、采样率、声道等信息。
- 视频帧队列容量 `VIDEO_FRAME_QUEUE_SIZE = 10`，音频帧队列 `AUDIO_FRAME_QUEUE_SIZE = 5`
  （定义于 `PlayerImpl.cpp`），队列满时阻塞解码线程形成背压，限制解码超前。

### AudioRingBuffer（音频环形缓冲）

- `AudioRenderer` 内部结构体，固定 `65536` 字节，单生产者（audioLoop）单消费者（AAudio 回调）。
- 用 `std::mutex m_ringMutex` 保护读写指针与 serial 校验的原子性：
  `writeLocked` 在锁内校验 serial，旧代际拒绝写入；seek 时 `resetLocked` 清空。

## 4. 音视频同步

### Clock

`Clock` 记录最近一次 PTS 及其对应的 `steady_clock` 时间点；`getPTS()` 返回
`m_pts + 已流逝的真实时间`，从而在两次 `setPTS` 之间做线性插值，得到平滑连续的时钟。

### AudioVideoSyncer（音频为主时钟）

- 主时钟来源通过 `setMasterTimeProvider` 注入（优先于 `setMasterClock`），
  `getMasterTime()` 直接调用 provider。`doPrepare` 中按流情况选择：
  - 有音频流：provider 返回 `AudioRenderer::getCurrentPts()`；
  - 无音频流：provider 返回 `VideoRenderer::getCurrentPts()`（避免无参考时间）。
- **音频时钟按实际消费进度更新**：`AudioRenderer::getCurrentPts()` =
  `最近写入帧 PTS - 未消费字节 / 字节率`，随硬件消费推进，消除「写入即更新」带来的超前误差。
- `computeVideoDelay(videoPTS)` 计算 `delay = videoPTS - masterTime`：
  - `delay > 0`：视频帧领先于音频，渲染线程睡 `delay` 秒再上屏；
  - 上限钳制为 `m_maxDelay`（默认 `0.1s`），避免异常大延迟卡死画面。
- `shouldDrop(videoPTS)`：`videoPTS - masterTime < -m_dropThreshold`（默认 `0.1s`）时
  判定应丢帧，渲染线程据此丢弃落后过多的帧追赶进度（**已启用**）。

## 5. Seek 设计

`PlayerImpl::doSeek(positionMs)`（控制线程执行）：

1. 暂停解码与读线程（**渲染/音频线程不停**，靠 serial 丢弃旧数据）。
2. `m_videoDecoder.flush()` / `m_audioDecoder.flush()`：同步等待解码线程执行
   `avcodec_flush_buffers` 清空内部缓冲。
3. flush 两个帧队列（清空旧帧）。
4. `m_demuxer.seekTo(positionMs)`：读包线程内 `m_serial++` 自增 seek 代际，
   执行 `av_seek_frame(..., AVSEEK_FLAG_BACKWARD)` 定位到目标之前的关键帧，并 flush 两个包队列。
5. 取 `m_demuxer.getSerial()` 与目标秒数，调用 `m_videoRenderer.seek(serial, targetSec)` 与
   `m_audioRenderer.seek(serial, targetSec)` 更新消费端代际与目标。
6. 若原状态为 Started 则恢复读包/解码线程（Paused 保持暂停）。
7. 触发 `onSeekComplete` 回调。

**消费端双重校验**（实现「精准 seek」）：

- `serial` 不匹配 → 丢弃（seek 前残留的旧代际数据）；
- `pts < seekTarget` → 丢弃（BACKWARD 定位的关键帧到目标之间的帧）。

快速连续 seek 时，serial 不断自增，旧代际数据全部被消费端识别丢弃，不会串台。

## 6. 视频渲染（OpenGL ES 3.0）

`VideoRenderer`（线程封装）与 `VideoOutput`（渲染实现）协作：

- **EGL 生命周期**（`VideoRenderer::setupEGL` / `teardownEGL`）：渲染线程内基于
  `ANativeWindow`（Android）或 `EAGLContext`（iOS）创建 `EGLDisplay` /
  `EGLContext`（OpenGL ES 3.0）/ `EGLSurface` 并 `eglMakeCurrent`，退出时释放。
- **渲染循环** `renderLoop`：取帧 → serial 校验 → seek 目标校验 → `shouldDrop` 丢帧 →
  `computeVideoDelay` 睡眠 → `renderFrame` 绘制 → `eglSwapBuffers` → 更新视频时钟 →
  节流上报进度（间隔 `200ms`）。
- **着色器**：顶点着色器传递位置与纹理坐标；片元着色器采样 Y/U/V 三个
  `GL_R8` 单通道纹理，按 BT.601 系数做 YUV→RGB 转换。
- **纹理上传**（`uploadFrame`）：按帧宽高重建纹理（首次或分辨率变化时），
  对 Y（全分辨率）与 U/V（半分辨率）分别 `glTexSubImage2D` 上传，避免每帧重分配。
- **绘制**：绑定三张纹理到 `GL_TEXTURE0/1/2`，以 `GL_TRIANGLE_STRIP` 绘制全屏四边形。

## 7. 音频输出

- **抽象接口** `AudioOutput`：定义 `open/close/write/flush/pause/resume/getLatencyMs/setCallback`，
  便于未来替换为 OpenSL ES 或 iOS AudioUnit 实现。
- **AAudio 实现** `AAudioOutput`：使用回调（callback）模式而非阻塞 write 模式。
  AAudio 需要数据时调用静态 `dataCallback`，转发到 `AudioRenderer` 的 `onAudioData`
  （锁内从环形缓冲读取）。
- **重采样**：解码输出格式若非 `AV_SAMPLE_FMT_S16`，`AudioRenderer::open` 会初始化
  `SwrContext`；`audioLoop` 中按需 `swr_convert` 为 `S16` 交错数据。
- **写入与时钟**：`audioLoop` 取帧后经 serial / seek 目标校验，锁内 `writeLocked`
  写入环形缓冲（serial 不匹配则整帧丢弃）；写入成功后才更新 `m_lastPts`，
  避免旧帧覆盖 seek 基准。

## 8. 日志

`platform/log` 提供 `LogLevel` 与可注入的 `setLogFunction`。Android 桥接层在
`nativeCreate` 时注入映射到 `__android_log_print` 的实现，iOS 桥接层注入映射到
`os_log` 的实现，实现跨平台统一日志宏（`LOGD` / `LOGI` / `LOGW` / `LOGE`）。

## 9. 内存与资源管理

- FFmpeg 对象（`AVPacket` / `AVFrame` / `AVCodecContext` / `AVFormatContext` / `SwrContext`）
  均在拥有者处成对申请/释放：解码线程持有临时 packet/frame，消费端（渲染/音频循环）
  在丢弃或消费后 `av_frame_free`；`close`/`release` 时关闭解码器与封装并释放重采样上下文。
- `AudioRenderer` 的环形缓冲与 `AAudioOutput` 在 `close()` 中释放；`Player` 析构调用 `release()`。
- JNI 层对 Java 对象持有 `NewGlobalRef`，`nativeRelease` 时 `DeleteGlobalRef` 防泄漏。

## 10. 可扩展方向

当前实现已具备的功能与预留接口：

- **丢帧追赶**：`AudioVideoSyncer::shouldDrop` 已在渲染循环启用，落后过多自动丢帧。
- **播放完成**：`Demuxer` 读到 EOF 后经 `EosCallback` 投递 `Eos` 命令，控制线程触发 `onCompletion`。
- **缓冲进度**：`PlayerCallbacks::onBufferingUpdate` 已定义，尚未触发（面向网络流场景）。
- **硬件解码**：可接入 Android MediaCodec / iOS VideoToolbox 替换软解。
- **多音频后端**：通过 `AudioOutput` 抽象接入 OpenSL ES、AudioUnit。
- **倍速/音量/字幕**：需扩展时钟与渲染管线。
