#version 450 core
out vec4 FinalColor;
in vec3 FragColor;

void main() {
    FinalColor = vec4(FragColor, 1.0);
}