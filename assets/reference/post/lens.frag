#version 450 core
#define varying in
#define texture2D texture
#define textureCube texture
layout(location=0) out vec4 postColor;
#define gl_FragColor postColor
float luminance(const in vec3 rgb){return dot(vec3(0.2126,0.7152,0.0722),rgb);}

  varying vec2 vP; varying float vI; varying vec3 vCh;
  void main() {
    float d2 = dot(vP, vP);
    if (d2 >= 1.0) discard;
    float k = 1.0 - d2; k = k * k;
    gl_FragColor = vec4(vCh * k * vI, 0.0);
  }