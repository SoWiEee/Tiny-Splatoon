#version 450 core
#define attribute in
#define varying out

attribute vec2 aPos;
attribute vec3 aLocal;
attribute vec4 aSplat;
attribute vec3 aStretch;
attribute vec4 aGrow;
varying vec3 vLocal;
varying vec4 vSplat;
varying vec3 vStretch;
varying vec4 vGrow;
void main() {
  vLocal = aLocal; vSplat = aSplat; vStretch = aStretch; vGrow = aGrow;
  gl_Position = vec4(aPos, 0.0, 1.0);
}