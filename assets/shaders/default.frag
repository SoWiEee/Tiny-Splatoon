#version 450 core
out vec4 FragColor;

in vec2 TexCoord;

uniform vec3 objectColor;

void main() {
    FragColor = vec4(objectColor * vec3(TexCoord, 1.0), 1.0);
}