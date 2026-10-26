#version 450 core
#define attribute in
#define varying out
in vec3 position;

  attribute vec4 iA;   // x, y (uv), rx, ry (screen-height units)
  attribute vec4 iB;   // rotation, intensity, channel, unused
  uniform float uAspect;
  varying vec2 vP; varying float vI; varying vec3 vCh;
  void main() {
    vec2 corner = position.xy;
    float c = cos(iB.x), s = sin(iB.x);
    vec2 lp = vec2(corner.x * iA.z, corner.y * iA.w);
    vec2 rp = vec2(c * lp.x - s * lp.y, s * lp.x + c * lp.y);
    vec2 p = iA.xy + vec2(rp.x / uAspect, rp.y);
    vP = corner; vI = iB.y;
    vCh = iB.z < 0.5 ? vec3(1.0, 0.0, 0.0) : iB.z < 1.5 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
  }