#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aOffset;
layout (location = 2) in vec3 aColor;
layout (location = 3) in float aScale;

out vec3 FragColor;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragColor = aColor;
    vec3 pos = aPos * aScale + aOffset;
    gl_Position = projection * view * vec4(pos, 1.0);
}