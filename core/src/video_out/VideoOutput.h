#pragma once

#include "queue/FrameQueue.h"
#include <cstdint>

namespace ccplayer {

class VideoOutput {
public:
    VideoOutput();
    ~VideoOutput();

    int init();
    void destroy();

    void setSurfaceSize(int width, int height);

    int renderFrame(VideoFrame* frame);

    void clear();

private:
    int compileShaders();
    int uploadFrame(VideoFrame* frame);

    unsigned int m_program;
    unsigned int m_vao;
    unsigned int m_vbo;

    unsigned int m_texY;
    unsigned int m_texU;
    unsigned int m_texV;

    int m_texWidth;
    int m_texHeight;

    int m_surfaceWidth;
    int m_surfaceHeight;

    bool m_initialized;
};

} // namespace ccplayer
