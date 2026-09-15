#pragma once

#include <cstdint>

namespace ccplayer {

/**
 * 播放器状态机：
 * Idle → Initialized（setDataSource）→ Prepared（prepare）→ Started ↔ Paused（pause/resume）
 * → Stopped（stop）→ 可重新 setDataSource；Error 为终止态；release 后回到 Idle。
 */
enum class PlayerState {
    Idle,        // 初始状态，未设置媒体源（或已 release）
    Initialized, // 已设置媒体源，等待 prepare
    Prepared,    // 媒体源已打开、解码器已就绪，可 start
    Started,     // 正在播放（解封装/解码/渲染线程运行中）
    Paused,      // 已暂停，线程已停止但缓冲保留，可 resume
    Stopped,     // 已停止，缓冲已清空，需重新 prepare
    Error        // 错误终止态（如媒体源无法打开）
};

// 错误码，通过 onError 回调通知上层；负值表示具体错误类型（0 表示无错误）
enum class PlayerError {
    NoError = 0,
    InvalidDataSource = -1, // 媒体源无法打开（路径不存在/格式无效）
    UnsupportedFormat = -2, // 格式不受支持（无可用的解码器）
    DecodeError = -3,       // 解码失败（码流损坏等）
    IOError = -4,           // IO 读取错误
    NetworkTimeout = -5,    // 网络超时（流媒体场景）
    BufferingError = -6,    // 缓冲错误（缓冲溢出/数据不足）
    RenderError = -7,       // 渲染错误（EGL/OpenGL 初始化或渲染失败）
    AudioError = -8         // 音频输出错误（设备打开失败）
};

/**
 * 平台 URI 打开器：用于打开 content:// 等平台专属 URI，返回 fd（所有权转移给调用方）。
 * offset/length 为出参（数据在 fd 中的起始偏移与长度，长度未知时置 -1）；失败返回 -1。
 */
using FdOpener = int (*)(const char* uri, int64_t* offset, int64_t* length, void* userData);

/**
 * 播放器事件回调集合，未设置的回调不触发。
 * 注意：除个别例外外回调均在后台线程触发，上层需自行处理线程切换（如 JNI 层切回 Java 层）。
 */
struct PlayerCallbacks {
    void (*onPrepared)(void* userData) = nullptr;          // prepare 完成（异步模式）
    void (*onCompletion)(void* userData) = nullptr;        // 播放到文件末尾完成播放（预留，尚未触发）
    void (*onError)(int errorCode, void* userData) = nullptr; // 发生错误，errorCode 对应 PlayerError
    void (*onProgress)(int64_t currentMs, int64_t durationMs, void* userData) = nullptr; // 每渲染一帧上报进度（毫秒）
    void (*onBufferingUpdate)(int percent, void* userData) = nullptr; // 缓冲进度百分比（预留，尚未触发）
    void (*onSeekComplete)(void* userData) = nullptr;      // seekTo 完成
};

} // namespace ccplayer
