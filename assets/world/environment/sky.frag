#version 450 core
in vec3 vDir; layout(location=0) out vec4 fragColor; uniform vec3 cameraPosition;


uniform float uTime;
uniform vec3 uSunDir;
uniform vec3 uZenith;
uniform vec3 uSkyMid;
uniform vec3 uHorizon;
uniform vec3 uGround;
uniform vec3 uHorizonGlow;
uniform vec3 uGlowColor;
uniform vec4 uGlowParams;   // x tight power, y tight strength, z horizon band sharpness, w wide strength
uniform vec4 uHaze;         // x density (1/m), y max, z height falloff (m)
uniform float uNight;

vec3 skyGradient(vec3 d) {
  float h = d.y;
  float hp = max(h, 0.0);
  float t = sqrt(hp);
  vec3 c = mix(uHorizon, uSkyMid, smoothstep(0.0, 0.52, t));
  c = mix(c, uZenith, smoothstep(0.4, 1.0, t));
  vec2 sh = normalize(uSunDir.xz + vec2(1e-5));
  vec2 dh = normalize(d.xz + vec2(1e-5));
  float az = dot(sh, dh) * 0.5 + 0.5;
  float band = exp(-hp * uGlowParams.z);
  c += uHorizonGlow * band * (0.2 + 0.8 * az * az * az);
  float sd = max(dot(normalize(vec3(d.x, hp, d.z)), uSunDir), 0.0);
  c += uGlowColor * (pow(sd, uGlowParams.x) * uGlowParams.y + pow(sd, 6.0) * uGlowParams.w);
  // dusk: the horizon opposite the sun lies in the earth's shadow — cooler and dimmer than the sunward side, so the
  // far shore / skyline away from the sun hazes into lavender-blue instead of glowing salmon (sky, env map and haze)
  float anti = (1.0 - az) * (1.0 - az);
  c *= mix(vec3(1.0), vec3(0.7, 0.74, 0.95), uNight * anti * exp(-hp * 2.5));
  c = mix(c, uGround, smoothstep(0.0, -0.22, h));
  return c;
}
vec3 hazeColor(vec3 d) {
  vec3 h = skyGradient(normalize(vec3(d.x, clamp(d.y, 0.0, 1.0) * 0.3, d.z)));
  h = mix(h, uSkyMid * 1.08 + uHorizon * 0.12, 0.22);   // aerial perspective: distant land turns bluer
  // looking down through the haze you see scattered ambient, not the glowing horizon band
  return mix(h, uGround * 1.15 + uSkyMid * 0.25, smoothstep(-0.02, -0.45, d.y) * 0.75);
}
vec3 applyHaze(vec3 col, vec3 wp) {
  vec3 dv = wp - cameraPosition;
  float dist = length(dv);
  vec3 dir = dv / max(dist, 1e-3);
  float hf = exp(-max(wp.y, 0.0) / uHaze.z);
  float f = 1.0 - exp(-dist * uHaze.x * mix(0.45, 1.0, hf));
  return mix(col, hazeColor(dir), min(f, uHaze.y));
}


float hash12(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float hash13(vec3 p3){ p3 = fract(p3 * 0.1031); p3 += dot(p3, p3.zyx + 31.32); return fract((p3.x + p3.y) * p3.z); }
float vnoise2(vec2 p){ vec2 i = floor(p), f = fract(p); vec2 u = f*f*(3.0-2.0*f);
  return mix(mix(hash12(i), hash12(i+vec2(1.0,0.0)), u.x), mix(hash12(i+vec2(0.0,1.0)), hash12(i+vec2(1.0,1.0)), u.x), u.y); }
float vnoise3(vec3 p){ vec3 i = floor(p), f = fract(p); vec3 u = f*f*(3.0-2.0*f);
  float a = hash13(i), b = hash13(i+vec3(1.0,0.0,0.0)), c = hash13(i+vec3(0.0,1.0,0.0)), d = hash13(i+vec3(1.0,1.0,0.0));
  float e = hash13(i+vec3(0.0,0.0,1.0)), f1 = hash13(i+vec3(1.0,0.0,1.0)), g = hash13(i+vec3(0.0,1.0,1.0)), h = hash13(i+vec3(1.0,1.0,1.0));
  return mix(mix(mix(a,b,u.x), mix(c,d,u.x), u.y), mix(mix(e,f1,u.x), mix(g,h,u.x), u.y), u.z); }
float fbm2(vec2 p){ float s = 0.0, a = 0.5; mat2 m = mat2(1.6, 1.2, -1.2, 1.6);
  for (int i = 0; i < 5; i++){ s += a * vnoise2(p); p = m * p + 3.7; a *= 0.5; } return s / 0.96875; }
// Round cumulus lobes: nearest jittered feature point; x = 1 - (d/r)^2 (>0 inside a puff), yz = offset from its centre.
vec3 lobes(vec2 p, float period){
  vec2 i = floor(p), f = fract(p);
  // smooth (soft-min) blend of neighbouring lobes → no Voronoi seams; creases between puffs stay soft
  float acc = 0.0; vec2 ob = vec2(0.0);
  for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
    vec2 g = vec2(float(x), float(y));
    vec2 c = i + g;
    if (period > 0.0) c.x = mod(c.x, period);
    vec2 h = vec2(hash12(c), hash12(c + 17.31));
    float rad = 0.6 + 0.35 * hash12(c + 5.13);
    vec2 o = (g + 0.2 + 0.6 * h - f) / rad;
    float w = exp(-5.0 * dot(o, o));
    acc += w; ob -= w * o;
  }
  acc = max(acc, 1e-6);
  return vec3(1.0 + log(acc) / 5.0, ob / acc);
}
float fbm3(vec3 p){ float s = 0.0, a = 0.5;
  for (int i = 0; i < 5; i++){ s += a * vnoise3(p); p = p * 2.02 + vec3(1.7, 9.2, 4.1); a *= 0.5; } return s / 0.96875; }

uniform vec3 uSunDisk;
uniform float uSunCos;
uniform vec3 uCloudLit;
uniform vec3 uCloudShade;
uniform vec4 uCloudParams;   // x overhead coverage, y horizon bank amount, z drift speed, w bank height
uniform sampler2D uCloudTex;


void main() {
  vec3 d = normalize(vDir);
  vec3 col = skyGradient(d);
  float sd = dot(d, uSunDir);
  float cloudA = 0.0;
  vec2 sunH = normalize(uSunDir.xz + vec2(1e-4));

  // ---- baked volumetric cumulus (slow drift = rotation of the whole field) ----
  {
    float u = atan(d.z, d.x) / 6.28318531 + 0.5 + uTime * 0.00035 * uCloudParams.z;
    float v = pow(clamp(d.y, 0.0, 1.0), 1.0 / 1.6);
    vec4 cl = textureLod(uCloudTex, vec2(u, v), 0.0);
    cl *= smoothstep(-0.002, 0.012, d.y);
    col = col * (1.0 - cl.a) + cl.rgb;
    cloudA = cl.a;
  }
  // ---- high cirrus veil: faint, streaky, far above the cumulus ----
  if (d.y > 0.02) {
    vec2 cp = d.xz / (d.y + 0.08) * 0.9 + vec2(uTime * 0.004, uTime * 0.0015) * uCloudParams.z;
    vec2 cs = vec2(cp.x * 0.35 + cp.y * 0.9, cp.y * 0.35 - cp.x * 0.9);
    float ci = fbm2(cs * vec2(0.55, 3.2) + 4.0) - 0.52;
    ci = smoothstep(0.02, 0.3, ci) * smoothstep(0.06, 0.35, d.y) * (1.0 - cloudA) * 0.11;
    col = mix(col, uCloudLit * 0.95 + uGlowColor * 0.1, ci);
  }
#if defined(SKY_SHAFTS) && !defined(ENV_PASS)
  // crepuscular rays: walk from this direction toward the sun across the baked cloud layer — clear paths glow, paths
  // behind a heap fall into its shade, so shafts fan out from the sun between the clouds
  {
    float sdo = dot(d, uSunDir);
    if (sdo > 0.35 && d.y > -0.05) {
      float occ = 0.0;
      for (int k = 0; k < 6; k++) {
        vec3 s = normalize(mix(d, uSunDir, (float(k) + 0.5) / 6.0));
        float uu = atan(s.z, s.x) / 6.28318531 + 0.5 + uTime * 0.00035 * uCloudParams.z;
        float vv = pow(clamp(s.y, 0.0, 1.0), 1.0 / 1.6);
        occ += textureLod(uCloudTex, vec2(uu, vv), 0.0).a * smoothstep(-0.002, 0.012, s.y);
      }
      occ /= 6.0;
      float w = smoothstep(0.35, 1.0, sdo);
      w *= w;
      col += uGlowColor * (1.0 - occ) * w * 0.1 * (1.0 - cloudA);
      col *= 1.0 - occ * w * 0.2 * (1.0 - cloudA);
    }
  }
#endif

#ifndef ENV_PASS
  // ---- sun disk + tight halo (HDR → blooms) ----
  float aa = 0.00012;
  float disk = smoothstep(uSunCos - aa, uSunCos + aa, sd);
  col += uSunDisk * disk * (1.0 - cloudA * 0.94);
  col += uSunDisk * 0.02 * pow(max(sd, 0.0), 1400.0) * (1.0 - cloudA * 0.6);
#else
  // env map: soft sun lobe only (a tiny HDR disk would sparkle in rough mips)
  col += uGlowColor * pow(max(sd, 0.0), 64.0) * 1.2;
  col *= 0.72;
#endif

  fragColor = vec4(col, 1.0);
  #ifndef ENV_PASS
  fragColor.rgb += (hash12(gl_FragCoord.xy + fract(uTime) * 61.0) - 0.5) / 255.0;
#endif
}
