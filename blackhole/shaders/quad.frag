#version 430
in vec2 uv;
out vec4 c;
uniform sampler2D t;
void main() {
    c = texture(t, uv);
}
