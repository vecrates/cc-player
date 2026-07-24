#include "platform/log.h"
#include <cstdio>
#include <cstring>

namespace ccplayer {

static LogFunc g_logFunc = nullptr;

void setLogFunction(LogFunc func) {
    g_logFunc = func;
}

void log(LogLevel level, const char* tag, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (g_logFunc) {
        g_logFunc(level, tag, buf);
    }
}

} // namespace ccplayer
