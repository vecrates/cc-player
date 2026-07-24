#include "VideoOutput.h"
#include "platform/log.h"

#ifdef __APPLE__
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#else
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#endif

#define TAG "VideoOutput"

static const char* VERTEX_SHADER = R"(
#version 300 es
layout(location = 0) in vec4 aPosition;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main() {
    gl_Position = aPosition;
    vTexCoord = aTexCoord;
}
)";

static const char* FRAGMENT_SHADER = R"(
#version 300 es
precision mediump float;
in vec2 vTexCoord;
uniform sampler2D uTexY;
uniform sampler2D uTexU;
uniform sampler2D uTexV;
out vec4 fragColor;
void main() {
    float y = texture(uTexY, vTexCoord).r;
    float u = texture(uTexU, vTexCoord).r - 0.5;
    float v = texture(uTexV, vTexCoord).r - 0.5;
    float r = y + 1.402 * v;
    float g = y - 0.344 * u - 0.714 * v;
    float b = y + 1.772 * u;
    fragColor = vec4(r, g, b, 1.0);
}
)";

namespace ccplayer {

static float QUAD_VERTICES[] = {
    -1.0f, -1.0f,  0.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 1.0f,
    -1.0f,  1.0f,  0.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 0.0f,
};

VideoOutput::VideoOutput()
    : m_program(0)
    , m_vao(0)
    , m_vbo(0)
    , m_texY(0)
    , m_texU(0)
    , m_texV(0)
    , m_texWidth(0)
    , m_texHeight(0)
    , m_surfaceWidth(0)
    , m_surfaceHeight(0)
    , m_initialized(false)
{
}

VideoOutput::~VideoOutput() {
    destroy();
}

int VideoOutput::init() {
    if (m_initialized) return 0;

    if (compileShaders() < 0) return -1;

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTICES), QUAD_VERTICES, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glGenTextures(1, &m_texY);
    glGenTextures(1, &m_texU);
    glGenTextures(1, &m_texV);

    for (unsigned int tex : {m_texY, m_texU, m_texV}) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    m_initialized = true;
    LOGI(TAG, "VideoOutput initialized");
    return 0;
}

void VideoOutput::destroy() {
    if (!m_initialized) return;

    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_texY) glDeleteTextures(1, &m_texY);
    if (m_texU) glDeleteTextures(1, &m_texU);
    if (m_texV) glDeleteTextures(1, &m_texV);

    m_initialized = false;
}

void VideoOutput::setSurfaceSize(int width, int height) {
    m_surfaceWidth = width;
    m_surfaceHeight = height;
    glViewport(0, 0, width, height);
}

int VideoOutput::renderFrame(VideoFrame* frame) {
    if (!m_initialized || !frame || !frame->frame) return -1;

    uploadFrame(frame);

    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texY);
    glUniform1i(glGetUniformLocation(m_program, "uTexY"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_texU);
    glUniform1i(glGetUniformLocation(m_program, "uTexU"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_texV);
    glUniform1i(glGetUniformLocation(m_program, "uTexV"), 2);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    return 0;
}

void VideoOutput::clear() {
    glClear(GL_COLOR_BUFFER_BIT);
}

int VideoOutput::compileShaders() {
    auto compile = [](GLenum type, const char* src) -> unsigned int {
        unsigned int shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        int success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char info[512];
            glGetShaderInfoLog(shader, 512, nullptr, info);
            LOGE("Shader", "Compile failed: %s", info);
            return 0;
        }
        return shader;
    };

    unsigned int vs = compile(GL_VERTEX_SHADER, VERTEX_SHADER);
    unsigned int fs = compile(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
    if (!vs || !fs) return -1;

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);

    int success;
    glGetProgramiv(m_program, GL_LINK_STATUS, &success);
    if (!success) {
        char info[512];
        glGetProgramInfoLog(m_program, 512, nullptr, info);
        LOGE(TAG, "Link failed: %s", info);
        return -1;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return 0;
}

int VideoOutput::uploadFrame(VideoFrame* frame) {
    AVFrame* f = frame->frame;
    int w = f->width;
    int h = f->height;

    if (w != m_texWidth || h != m_texHeight) {
        m_texWidth = w;
        m_texHeight = h;

        glBindTexture(GL_TEXTURE_2D, m_texY);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);

        glBindTexture(GL_TEXTURE_2D, m_texU);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w / 2, h / 2, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);

        glBindTexture(GL_TEXTURE_2D, m_texV);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w / 2, h / 2, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glBindTexture(GL_TEXTURE_2D, m_texY);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, f->data[0]);

    glBindTexture(GL_TEXTURE_2D, m_texU);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w / 2, h / 2, GL_RED, GL_UNSIGNED_BYTE, f->data[1]);

    glBindTexture(GL_TEXTURE_2D, m_texV);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w / 2, h / 2, GL_RED, GL_UNSIGNED_BYTE, f->data[2]);

    return 0;
}

} // namespace ccplayer
