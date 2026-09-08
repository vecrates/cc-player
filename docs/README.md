# CCPlayer 文档

CCPlayer 是一个基于 FFmpeg 的跨平台音视频播放器内核，使用 C++17 编写核心，
通过平台桥接层（Android JNI / iOS Objective-C++）提供给上层应用使用。

## 文档目录

| 文档 | 内容 |
|------|------|
| [architecture.md](./architecture.md) | 整体架构、模块划分、数据流 |
| [design.md](./design.md) | 核心设计方案：状态机、线程模型、音视频同步、缓冲队列、渲染与音频输出 |
| [build.md](./build.md) | 构建指南：FFmpeg 预编译、Android / iOS 编译 |

## 项目结构概览

```
cc-player/
├── core/                  # 跨平台播放内核（C++）
│   ├── include/           # 对外公开头文件（Player.h / PlayerTypes.h）
│   ├── src/
│   │   ├── player/        # 播放器门面与实现（Player / PlayerImpl）
│   │   ├── demuxer/       # 解封装（FFmpeg avformat）
│   │   ├── decoder/       # 解码（FFmpeg avcodec）
│   │   ├── queue/         # 命令队列 / 包队列 / 帧队列
│   │   ├── sync/          # 时钟与音视频同步
│   │   ├── video_out/     # VideoRenderer（渲染线程）+ VideoOutput（OpenGL ES）
│   │   ├── audio_out/     # AudioRenderer（音频线程）+ AudioOutput 抽象 + AAudio
│   │   └── platform/      # 平台日志
│   ├── shaders/           # GLSL 着色器（YUV → RGB）
│   └── CMakeLists.txt
├── android/               # Android 应用与 JNI 桥接
│   └── app/src/main/
│       ├── java/com/ccplayer/   # CCPlayer.kt / MainActivity.kt
│       └── jni/                 # ccplayer_jni.cpp + CMakeLists.txt
├── ios/PlayerApp/         # iOS 应用与 Objective-C++ 桥接
│   ├── CCPlayer.h/.mm
│   └── PlayerViewController.swift
└── scripts/               # FFmpeg 预编译脚本（Android / iOS）
```

## 技术栈

- **解封装 / 解码**：FFmpeg（libavformat / libavcodec / libavutil / libswresample）
- **视频渲染**：OpenGL ES 3.0 + EGL，YUV420P 三平面纹理上传 + 片元着色器转 RGB
- **音频输出**：Android AAudio（回调模式），由 AudioRenderer 内部的互斥锁环形缓冲供数
- **并发**：控制线程 + 命令队列（Actor 模型），`std::thread` + `std::mutex` + `std::condition_variable` + `std::atomic`
- **构建**：CMake（core 静态库 + 平台 JNI/应用层）

## 核心特性

- 控制线程 + 命令队列的 Actor 架构：所有控制命令（prepare/start/pause/resume/seek）由单一控制线程串行执行
- 经典「生产者-消费者」流水线：读包 → 解码 → 渲染，各级之间用队列解耦
- serial 序列号精准 seek：seek 代际贯穿包/帧，消费端丢弃旧代际数据，渲染/音频线程不停
- 以音频时钟为主时钟（audio master clock）的音视频同步策略，音频时钟按实际消费进度更新
- 渲染与音频线程封装为独立 VideoRenderer / AudioRenderer，自管线程、EGL 与环形缓冲
- 完整播放器状态机（Idle / Initialized / Prepared / Started / Paused / Stopped / Error）
- 异步控制接口（prepare/start/pause/resume/seek）、同步 stop/release、进度/完成回调
