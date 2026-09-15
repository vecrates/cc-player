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
    // 设置视频显示宽高（含旋转校正），渲染时按此比例居中绘制，四周黑边
    void setVideoSize(int width, int height);

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

    int m_videoWidth;
    int m_videoHeight;

    bool m_initialized;
};

} // namespace ccplayer
