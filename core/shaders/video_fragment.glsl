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

    fragColor = vec4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
}
