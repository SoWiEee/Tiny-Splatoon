#version 450 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D splatTexture;
uniform vec3 paintColor;

void main()
{
    vec4 texColor = texture(splatTexture, TexCoords);
    float shapeAlpha = texColor.a;

    if (shapeAlpha < 0.5) {
        discard; 
    }
    FragColor = vec4(paintColor, 1.0); 
}