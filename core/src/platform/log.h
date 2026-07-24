#pragma once

#include <cstdarg>

namespace ccplayer {

enum class LogLevel { Debug, Info, Warn, Error };

using LogFunc = void (*)(LogLevel level, const char* tag, const char* msg);

void setLogFunction(LogFunc func);
void log(LogLevel level, const char* tag, const char* fmt, ...);

} // namespace ccplayer

#define LOGD(tag, fmt, ...) ::ccplayer::log(::ccplayer::LogLevel::Debug, tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) ::ccplayer::log(::ccplayer::LogLevel::Info, tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) ::ccplayer::log(::ccplayer::LogLevel::Warn, tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) ::ccplayer::log(::ccplayer::LogLevel::Error, tag, fmt, ##__VA_ARGS__)
