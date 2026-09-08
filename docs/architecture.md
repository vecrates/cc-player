# 整体架构

CCPlayer 采用分层 + 流水线架构。播放内核（`core`）与平台无关，
平台层（Android JNI / iOS Objective-C++）仅做类型转换、生命周期桥接和事件回调转发。

## 分层结构

```
┌────────────────────────────────────────────────────────────┐
│                        应用层 (App)                          │
│   Android: MainActivity.kt        iOS: PlayerViewController │
└──────────────────────────┬─────────────────────────────────┘
                           │  使用
┌──────────────────────────┴─────────────────────────────────┐
│                     平台封装层 (Wrapper)                     │
│   Android: CCPlayer.kt             iOS: CCPlayer.mm (Obj-C++)│
└──────────────────────────┬─────────────────────────────────┘
                           │  JNI / 直接调用
┌──────────────────────────┴─────────────────────────────────┐
│                     桥接层 (Bridge)                          │
│              Android: ccplayer_jni.cpp (JNI)                │
└──────────────────────────┬─────────────────────────────────┘
                           │  C++ API
┌──────────────────────────┴─────────────────────────────────┐
│                    播放内核 (core, C++)                      │
│   Player(门面) → PlayerImpl(控制 Actor)                      │
│   Demuxer / Decoder / Queues / Sync / VideoRenderer /       │
│   AudioRenderer                                            │
└──────────────────────────┬─────────────────────────────────┘
                           │
┌──────────────────────────┴─────────────────────────────────┐
│                     系统能力层 (Platform)                     │
│  FFmpeg | OpenGL ES/EGL | AAudio | ANativeWindow | 日志      │
└────────────────────────────────────────────────────────────┘
```

## 对外 API 设计

内核对外只暴露两个头文件：

- `core/include/Player.h`：门面类 `Player`，采用 **Pimpl 惯用法**
  （前向声明 `PlayerImpl`，隐藏实现细节，保持 ABI 稳定）。
- `core/include/PlayerTypes.h`：公共类型定义，包括：
  - `PlayerState`：播放器状态枚举（Idle/Initialized/Prepared/Started/Paused/Stopped/Error）
  - `PlayerError`：错误码枚举
  - `PlayerCallbacks`：一组 C 风格函数指针回调（onPrepared / onCompletion / onError /
    onProgress / onBufferingUpdate / onSeekComplete），配合 `userData` 透传上下文

接口语义（与旧版不同，**控制类接口全部异步化**）：

- **控制接口**（`prepare` / `prepareAsync` / `start` / `pause` / `resume` / `seekTo`）：
  向控制线程投递命令后立即返回，实际执行在控制线程串行完成。
- **同步控制**（`stop` / `release`）：投递命令并阻塞等待控制线程执行完成。
- **查询接口**（`getState` / `getCurrentPosition` / `getDuration`）：同步原子快照，随时可调。

这种「C 结构体回调 + userData」的设计使内核易于被任意语言绑定（JNI、Obj-C、C ABI）。

## 并发架构：控制线程 + 命令队列（Actor 模型）

播放内核不再由 `PlayerImpl` 直接管理全部线程，而是采用 **Actor 模型**：

- **PlayerImpl = 控制 Actor**：构造函数启动一条控制线程（`controlLoop`），
  阻塞消费 `CommandQueue<PlayerCommand>`，串行执行状态机迁移与流水线编排。
  命令类型见 `PlayerCommandType`：`Prepare / Start / Pause / Resume / Seek / Stop / Release / Eos`。
- **Demuxer = 读包 Actor**：内部命令队列（`Start / Pause / Resume / Seek / Stop / Close`），
  保证 `av_read_frame` / `av_seek_frame` 永不并发触碰 `AVFormatContext`。
  通过 `AVIOInterruptCB` 打断阻塞中的 `av_read_frame`。
- **Decoder = 解码 Actor**：内部命令队列（`Start / Pause / Resume / Flush / Stop`），
  `avcodec_flush_buffers` 由解码线程自身执行，避免与控制线程并发。
- **VideoRenderer / AudioRenderer = 纯线程封装**：各自独立管理渲染线程 / 音频线程，
  对外只提供 `start / pause / stop / seek` 等接口，不涉及 FFmpeg 上下文。

回调（`onPrepared / onError / onCompletion / onSeekComplete / onProgress`）统一在
控制线程（或渲染线程转投递）触发，避免多线程重入用户代码。

## 核心流水线

播放内核是一条典型的 FFmpeg 播放器流水线，各阶段运行在独立线程，用队列解耦：

```
                          ┌──────────────────────────┐
                          │        Demuxer           │
                          │  (读包 Actor, av_read_frame│
                          │   命令队列 + interrupt)    │
                          └──────────┬───────────────┘
                     ┌───────────────┴────────────────┐
                     │                                │
              ┌──────┴──────┐                  ┌──────┴──────┐
              │ VideoPktQueue│                 │ AudioPktQueue│
              └──────┬──────┘                  └──────┬──────┘
                     │                                │
              ┌──────┴──────┐                  ┌──────┴──────┐
              │ VideoDecoder │                 │ AudioDecoder │
              │ (解码 Actor) │                 │ (解码 Actor) │
              └──────┬──────┘                  └──────┬──────┘
                     │                                │
              ┌──────┴──────┐                  ┌──────┴──────┐
              │ VideoFrameQ  │                 │ AudioFrameQ  │
              └──────┬──────┘                  └──────┬──────┘
                     │                                │
              ┌──────┴──────┐                  ┌──────┴──────┐
              │VideoRenderer│                  │AudioRenderer│
              │ (渲染线程,   │                  │ (音频线程,   │
              │  EGL + 同步) │                  │ 重采样+环形缓冲)│
              └──────┬──────┘                  └──────┬──────┘
                     │                                │
                     │                         ┌──────┴──────┐
                     │                         │ AAudioOutput │
                     │                         │ (回调消费)    │
                     │                         └──────────────┘
                     │
             ┌───────┴───────┐
             │  VideoOutput   │
             │ (OpenGL ES 渲染)│
             └───────────────┘
```

**数据流说明：**

1. **Demuxer** 在独立读包线程循环调用 `av_read_frame`，按流类型把 `AVPacket`
   分发到视频/音频包队列，包携带当前 seek 代际（serial）。
2. **Decoder**（视频、音频各一个 Actor）从各自包队列取出 `AVPacket`，
   通过 `avcodec_send_packet` / `avcodec_receive_frame` 解码为 `AVFrame`，
   计算 PTS（秒）后压入对应帧队列，帧同样携带 serial。
3. **VideoRenderer** 的渲染线程从视频帧队列取帧，按 serial 与 seek 目标丢弃旧帧，
   以主时钟同步后上传纹理、`eglSwapBuffers` 上屏，并节流上报进度。
4. **AudioRenderer** 的音频线程从音频帧队列取帧，按需用 `SwrContext` 重采样为
   `S16` 交错数据，写入互斥锁保护的环形缓冲；AAudio 回调从环形缓冲拉取。
5. **主时钟**：有音频流时为 AudioRenderer 的「最近写入帧 PTS - 未消费时长」，
   无音频流时退化为 VideoRenderer 的视频时钟。

## 模块职责

| 模块 | 路径 | 职责 |
|------|------|------|
| `Player` | `src/player/Player.cpp` | 对外门面，转发所有调用到 `PlayerImpl`（Pimpl） |
| `PlayerImpl` | `src/player/PlayerImpl.*` | 控制 Actor：控制线程串行执行命令、状态机、流水线编排、回调派发 |
| `Demuxer` | `src/demuxer/Demuxer.*` | 读包 Actor：打开媒体源、查找流、读包分发、seek、serial 代际、EOF 回调 |
| `Decoder` | `src/decoder/Decoder.*` | 解码 Actor：视频/音频解码，flush 在解码线程执行 |
| `CommandQueue<T>` | `src/queue/CommandQueue.h` | 通用线程安全命令队列（push/pop/tryPop/abort/reset），供控制线程与各 Actor 复用 |
| `PacketQueue` | `src/queue/PacketQueue.*` | 线程安全的 `AVPacket` 链表队列，携带 serial，支持字节数上限、超时取包、flush、abort |
| `FrameQueue` | `src/queue/FrameQueue.*` | 线程安全的 `AVFrame` 链表队列（区分视频/音频帧），携带 serial、PTS |
| `Clock` / `AudioVideoSyncer` | `src/sync/Sync.*` | 基于 `steady_clock` 插值的时钟；主时钟时间源注入、视频延迟与丢帧计算 |
| `VideoRenderer` | `src/video_out/VideoRenderer.*` | 渲染线程封装：自管 EGL 生命周期、同步、丢帧、进度回调 |
| `VideoOutput` | `src/video_out/VideoOutput.*` | OpenGL ES 3.0 渲染：着色器、YUV 三纹理上传、绘制四边形 |
| `AudioRenderer` | `src/audio_out/AudioRenderer.*` | 音频线程封装：重采样、环形缓冲、消费时更新的音频主时钟 |
| `AudioOutput` | `src/audio_out/AudioOutput.h` | 音频输出抽象接口（open/close/write/flush/pause/resume/setCallback） |
| `AAudioOutput` | `src/audio_out/AAudioOutput.*` | Android AAudio 实现，回调模式 |
| `log` | `src/platform/log.*` | 可注入的平台日志（Android 接到 `__android_log_print`，iOS 接到 `os_log`） |

## 平台桥接

### Android

- `CCPlayer.kt` 通过 `System.loadLibrary("ccplayer_jni")` 加载 JNI 库，
  持有一个 `nativeHandle`（指向 C++ `PlayerContext` 的指针）。
- `ccplayer_jni.cpp` 定义 `PlayerContext`（`Player*` + `JavaVM*` + `jobject` 全局引用），
  并把内核 C 回调适配为对 Kotlin 对象方法（`nativeOnPrepared` 等）的调用，
  回调内通过 `AttachCurrentThread` 挂接 JNI 线程。
- 渲染窗口通过 `ANativeWindow_fromSurface` 从 `android.view.Surface` 获取，
  以 `void*` 形式传入内核，由 `VideoRenderer` 在渲染线程建立 EGL 上下文。

### iOS

- `CCPlayer.mm`（Objective-C++）直接持有 C++ `Player` 实例；
  `CCPlayer.h` 定义了 `CCPlayerDelegate` 协议（prepare/complete/error/progress/seekComplete）。
- `PlayerViewController.swift` 使用 `GLKViewController` + `EAGLContext`（OpenGL ES 3.0）
  作为渲染目标，通过 `setSurface(eaglContext)` 传入。

## 线程总览

| 线程 | 所属 | 作用 |
|------|------|------|
| 控制线程 | `PlayerImpl::controlLoop` | 串行执行所有控制命令、状态迁移、回调派发 |
| 读包线程 | `Demuxer::threadLoop` | 循环 `av_read_frame` 并分发（命令队列 + interrupt） |
| 视频解码线程 | `Decoder::decodeLoop` | 解码视频 packet → frame |
| 音频解码线程 | `Decoder::decodeLoop` | 解码音频 packet → frame |
| 渲染线程 | `VideoRenderer::renderLoop` | EGL 初始化、同步、渲染、上屏、进度回调 |
| 音频供数线程 | `AudioRenderer::audioLoop` | 重采样、写入环形缓冲、更新音频时钟 |
| AAudio 内部线程 | AAudio | 回调拉取音频数据 |

> 详细的状态机、并发模型、同步策略、seek 与渲染实现见 [design.md](./design.md)。
