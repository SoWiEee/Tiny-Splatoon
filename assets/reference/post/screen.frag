#version 450 core
#define varying in
#define texture2D texture
#define textureCube texture
layout(location=0) out vec4 postColor;
#define gl_FragColor postColor
#define TAPS 8
float luminance(const in vec3 rgb){return dot(vec3(0.2126,0.7152,0.0722),rgb);}

  uniform sampler2D tDiffuse;
  uniform sampler2D tLens;
  uniform vec2 uRes;
  uniform float uAspect;
  uniform float uTime;
  uniform float uSpeed;
  uniform vec3 uSpeedTint;
  uniform float uStretch;
  uniform float uPunch;
  uniform vec2 uPunchPos;
  uniform float uBlast;
  uniform vec2 uBlastPos;
  uniform float uBlastRing;
  uniform vec3 uBlastColor;
  uniform float uChroma;
  uniform float uLensOn;
  uniform vec2 uLensTexel;
  uniform vec3 uLensColA;
  uniform vec3 uLensColB;
  uniform vec4 uEdgeInk;
  uniform vec4 uAura;
  uniform vec4 uHeart;
  uniform float uHurt;
  uniform vec4 uUrgency;
  uniform vec4 uSwim;
  uniform float uFocus;
  uniform vec4 uShimmer;
  uniform vec4 uFlood;
  uniform float uFloodDrip;
  uniform float uFloodClear;
  uniform float uHole;
  uniform vec3 uHoleRim;
  uniform vec4 uFlash;
  uniform float uDesat;
  uniform float uSat;
  uniform vec4 uKill;
  uniform vec4 uCharge;
  varying vec2 vUv;

  float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
  float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
  float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p); vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x), mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
  }
  float fbm(vec2 p) { float a = 0.5, s = 0.0; for (int i = 0; i < 3; i++) { s += a * vnoise(p); p = p * 2.03 + 17.1; a *= 0.5; } return s; }

  // distance (in screen-height units) to the nearest screen edge, with rounded inner corners
  float edgeD(vec2 uv) {
    vec2 p = uv * vec2(uAspect, 1.0);
    float dx = min(p.x, uAspect - p.x), dy = min(p.y, 1.0 - p.y);
    const float k = 16.0;
    return -log(exp(-k * dx) + exp(-k * dy)) / k;
  }
  // gooey ink band hugging the screen edges: > 0 inside the ink
  float gooBand(vec2 uv, float width, float wob, float seed) {
    vec2 p = uv * vec2(uAspect, 1.0);
    float n = fbm(p * 3.2 + vec2(seed, uTime * 0.12)) - 0.44;
    float n2 = vnoise(p * 10.0 + vec2(uTime * 0.25, seed * 1.7)) - 0.5;
    return width * (1.0 + n * wob * 2.2 + n2 * wob * 0.7) - edgeD(uv);
  }
  // ink drips hanging from the top edge: > 0 inside
  float drips(vec2 uv, float len, float seed, float top) {
    const float N = 19.0;
    float x = uv.x * N;
    float id = floor(x);
    float h = hash12(vec2(id, seed));
    float h2 = hash12(vec2(id, seed + 7.3));
    float fx = (fract(x) - 0.5 - (h2 - 0.5) * 0.5) / N * uAspect;   // horizontal distance, height units
    float L = len * (0.12 + 0.88 * h * h) * (0.9 + 0.1 * sin(uTime * (0.7 + h) + h * 6.28));
    float y = 1.0 - uv.y - top;
    if (y < -0.02) return -1.0;
    float w = (0.005 + 0.011 * h2) * (0.55 + 0.45 * step(0.35, h));
    float bulb = w * 1.55;
    float inTail = clamp(y / max(L, 1e-4), 0.0, 1.0);
    float rad = mix(w * 1.2, w, smoothstep(0.0, 0.3, inTail));
    rad = mix(rad, bulb, smoothstep(0.8, 1.0, inTail));
    float d = length(vec2(fx, max(y - L, 0.0))) - rad;
    return -d;
  }
  // glossy wet-ink shading for a surface with normal n
  vec3 inkShade(vec3 inkCol, vec3 behind, vec3 n, float thick) {
    vec3 L = normalize(vec3(-0.42, 0.62, 0.66));
    float ndl = clamp(dot(n, L), 0.0, 1.0);
    vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
    float spec = pow(clamp(dot(n, H), 0.0, 1.0), 64.0);
    float spec2 = pow(clamp(dot(n, normalize(vec3(0.5, -0.3, 0.8))), 0.0, 1.0), 18.0);
    float fres = pow(1.0 - clamp(n.z, 0.0, 1.0), 2.0);
    vec3 base = inkCol * (0.42 + 0.62 * ndl);
    base = mix(base, behind * (0.35 + inkCol * 1.1), (1.0 - thick) * 0.28);
    return base * (1.0 - fres * 0.45) + spec * 2.4 + spec2 * inkCol * 0.25;
  }

  vec3 sceneTap(vec2 uv, vec2 ca) {
    if (uChroma > 0.0005) return vec3(texture2D(tDiffuse, uv + ca).r, texture2D(tDiffuse, uv).g, texture2D(tDiffuse, uv - ca).b);
    return texture2D(tDiffuse, uv).rgb;
  }

  void main() {
    vec2 uv = vUv;
    vec2 c0 = vUv - 0.5;
    vec2 q = c0 * vec2(uAspect, 1.0);
    float r = length(q);

    // ---------------------------------------------------------------- geometric distortion
    uv = uPunchPos + (uv - uPunchPos) * (1.0 - uPunch);
    if (uStretch > 0.0001) { vec2 d = uv - 0.5; vec2 da = d * vec2(uAspect, 1.0); uv = 0.5 + d * (1.0 - uStretch * dot(da, da)); }
    if (uSwim.a > 0.001) uv += vec2(sin(uv.y * 37.0 + uTime * 2.6) + 0.5 * sin(uv.y * 71.0 - uTime * 3.3), cos(uv.x * 29.0 + uTime * 2.1)) * 0.0011 * uSwim.a;
    if (uBlast > 0.001) {
      vec2 bd = (uv - uBlastPos) * vec2(uAspect, 1.0);
      float bl = length(bd);
      float ring = exp(-pow((bl - uBlastRing) * 18.0, 2.0));
      uv -= (bd / max(bl, 1e-4)) / vec2(uAspect, 1.0) * ring * 0.028 * uBlast;
    }

    // ---------------------------------------------------------------- sampling: radial blurs + chromatic split
    float edgeW = smoothstep(0.22, 0.95, r);
    vec2 blurDir = (vec2(0.5) - uv) * (uSpeed * 0.055 * edgeW) + (uBlastPos - uv) * (uBlast * 0.05);
    vec2 ca = c0 * uChroma * 0.011 * (0.35 + r);
    vec3 col;
    if (dot(blurDir, blurDir) > 1e-7) {
      col = vec3(0.0);
      for (int i = 0; i < TAPS; i++) {
        float t = float(i) / float(TAPS - 1);
        col += sceneTap(uv + blurDir * t, ca);
      }
      col /= float(TAPS);
    } else {
      col = sceneTap(uv, ca);
    }

    // ---------------------------------------------------------------- grading-type adjustments
    float l0 = luma(col);
    col = max(mix(vec3(l0), col, uSat * (1.0 - uDesat)), 0.0);
    if (uFocus > 0.001) {
      float v = smoothstep(0.3, 0.92, r);
      col *= 1.0 - v * uFocus * 0.5;
      col += uCharge.rgb * exp(-abs(r - mix(0.95, 0.3, uCharge.a)) * 40.0) * uCharge.a * 0.35 * step(0.01, uCharge.a);
    }

    // ---------------------------------------------------------------- speed streaks
    if (uSpeed > 0.001) {
      float ang = atan(q.y, q.x);
      float a = ang / 6.28318 * 96.0;
      float id = floor(a);
      float h = hash12(vec2(id, 7.0));
      float lane = abs(fract(a) - 0.5);
      float lw = 0.05 + 0.1 * h;
      float streak = smoothstep(lw, lw * 0.25, lane);
      float mv = fract(r * (1.2 + h) - uTime * (2.2 + 2.4 * h) + h * 10.0);
      float dash = smoothstep(0.0, 0.08, mv) * smoothstep(0.55, 0.25, mv);
      float s = streak * dash * smoothstep(0.42, 1.0, r) * step(0.5, h) * uSpeed;
      col += uSpeedTint * s * 1.3;
    }

    // ---------------------------------------------------------------- swim: submerged tint + caustic glints
    if (uSwim.a > 0.001) {
      float v = smoothstep(0.35, 1.05, r);
      col = mix(col, col * (0.55 + uSwim.rgb * 0.9), v * 0.45 * uSwim.a);
      float cst = vnoise(q * 16.0 + vec2(uTime * 0.7, -uTime * 0.4)) * vnoise(q * 21.0 - vec2(uTime * 0.5, uTime * 0.6));
      col += uSwim.rgb * pow(cst, 3.0) * 1.6 * uSwim.a * v;
    }

    // ---------------------------------------------------------------- low HP heartbeat vignette
    if (uHeart.a > 0.001 || uHurt > 0.001) {
      float v = smoothstep(0.42, 1.08, r + (fbm(q * 2.6 + uTime * 0.08) - 0.44) * 0.18);
      float k = clamp(v * (uHurt * 0.5 + uHeart.a * 0.65), 0.0, 0.9);
      col = mix(col, col * 0.3 + uHeart.rgb * (0.12 + 0.22 * uHeart.a), k);
    }

    // ---------------------------------------------------------------- final-seconds urgency
    if (uUrgency.a > 0.001) {
      float v = smoothstep(0.5, 1.12, r);
      col = mix(col, col * vec3(1.1, 0.78, 0.72) + uUrgency.rgb * 0.2, v * uUrgency.a);
    }

    // ---------------------------------------------------------------- special aura / super-jump charge / kill flash / spawn shimmer
    if (uAura.a > 0.001) {
      float e = edgeD(vUv);
      float ang = atan(q.y, q.x);
      float flow = vnoise(vec2(ang * 4.0 - uTime * 1.2, e * 10.0 - uTime * 2.6));
      float rim = exp(-e * 55.0) * (0.55 + 0.6 * flow);
      float lane = 0.5 + 0.5 * sin(ang * 26.0 - uTime * 7.0 + flow * 3.0);
      float dashes = smoothstep(0.82, 0.97, lane) * exp(-e * 22.0) * 0.9;
      float tint = smoothstep(0.16, 0.0, e) * 0.22;
      col = mix(col, col * (0.7 + uAura.rgb * 0.45), tint * uAura.a);
      col += uAura.rgb * (rim + dashes) * uAura.a;
    }
    if (uKill.a > 0.001) col += uKill.rgb * smoothstep(0.5, 1.15, r) * uKill.a;
    if (uShimmer.a > 0.001) {
      float s = 0.5 + 0.5 * sin(uTime * 9.0 + r * 24.0 - atan(q.y, q.x) * 3.0);
      col += uShimmer.rgb * smoothstep(0.62, 1.12, r) * s * uShimmer.a * 0.35;
    }

    // ---------------------------------------------------------------- blast glow
    if (uBlast > 0.001) {
      float bd = length((vUv - uBlastPos) * vec2(uAspect, 1.0));
      col += uBlastColor * exp(-bd * 6.0) * uBlast * 0.2;
    }

    // ---------------------------------------------------------------- enemy ink underfoot: gooey edge band
    if (uEdgeInk.a > 0.001) {
      float bottom = smoothstep(0.6, 0.0, vUv.y);
      float w = uEdgeInk.a * (0.002 + 0.08 * bottom * bottom + 0.012 * bottom);
      float f = gooBand(vUv, w, 0.85, 11.0);
      float aa = fwidth(f) + 0.0015;
      float m = smoothstep(-aa, aa, f);
      if (m > 0.0) {
        float hgt = smoothstep(0.0, 0.028, f);
        vec3 n = normalize(vec3(-vec2(dFdx(hgt), dFdy(hgt)) * 0.028 * uRes.y * 0.9, 1.0));
        col = mix(col, inkShade(uEdgeInk.rgb, col, n, hgt), m * 0.94);
      }
    }

    // ---------------------------------------------------------------- lens ink (metaball field rendered by LensInk)
    if (uLensOn > 0.5) {
      vec4 Lc = texture2D(tLens, vUv);
      float fs = Lc.r + Lc.g;
      if (fs > 0.03) {
        vec2 tx = uLensTexel * 1.5;
        vec4 Lx1 = texture2D(tLens, vUv + vec2(tx.x, 0.0)), Lx0 = texture2D(tLens, vUv - vec2(tx.x, 0.0));
        vec4 Ly1 = texture2D(tLens, vUv + vec2(0.0, tx.y)), Ly0 = texture2D(tLens, vUv - vec2(0.0, tx.y));
        vec2 gi = vec2((Lx1.r + Lx1.g) - (Lx0.r + Lx0.g), (Ly1.r + Ly1.g) - (Ly0.r + Ly0.g));
        vec3 n = normalize(vec3(-gi * 1.35, 1.0));
        float aa = fwidth(fs) * 1.1 + 0.012;
        float cov = smoothstep(0.5 - aa, 0.5 + aa, fs);
        float film = smoothstep(0.08, 0.5, fs) * (1.0 - cov);
        float thick = clamp((fs - 0.5) * 1.3, 0.0, 1.0);
        vec3 inkCol = (Lc.r * uLensColA + Lc.g * uLensColB) / max(fs, 1e-4);
        vec3 behind = texture2D(tDiffuse, vUv + n.xy * 0.03).rgb;
        vec3 body = inkShade(inkCol, behind, n, thick);
        float rim = smoothstep(0.5, 0.56, fs) * (1.0 - smoothstep(0.56, 0.75, fs));
        body *= 1.0 - rim * 0.3;
        col = mix(col, behind * mix(vec3(1.0), inkCol * 1.5 + 0.1, 0.55), film * 0.42);
        col = mix(col, body, cov);
      }
      float fw = Lc.b;
      if (fw > 0.03) {
        vec2 tx = uLensTexel * 1.5;
        float wx = texture2D(tLens, vUv + vec2(tx.x, 0.0)).b - texture2D(tLens, vUv - vec2(tx.x, 0.0)).b;
        float wy = texture2D(tLens, vUv + vec2(0.0, tx.y)).b - texture2D(tLens, vUv - vec2(0.0, tx.y)).b;
        vec3 n = normalize(vec3(-vec2(wx, wy) * 1.6, 1.0));
        float aa = fwidth(fw) * 1.1 + 0.012;
        float cov = smoothstep(0.5 - aa, 0.5 + aa, fw);
        vec3 refr = texture2D(tDiffuse, vUv - n.xy * 0.07).rgb;
        vec3 L = normalize(vec3(-0.42, 0.62, 0.66));
        float spec = pow(clamp(dot(n, normalize(L + vec3(0.0, 0.0, 1.0))), 0.0, 1.0), 80.0);
        float fres = pow(1.0 - n.z, 1.6);
        vec3 wcol = refr * (1.02 - fres * 0.55) * vec3(0.93, 0.98, 1.04) + spec * 2.6;
        col = mix(col, wcol, cov);
        col = mix(col, col * 0.92, smoothstep(0.1, 0.5, fw) * (1.0 - cov) * 0.4);
      }
    }

    // ---------------------------------------------------------------- splatted ink flood + respawn iris reveal
    if (uFlood.a > 0.001) {
      float width = uFlood.a * 1.05;
      float f = gooBand(vUv, width, 0.3 * (1.0 - 0.5 * smoothstep(0.7, 1.0, uFlood.a)), 3.7);
      f = max(f, drips(vUv, uFloodDrip, 1.3, width * 0.8));
      if (uHole > 0.0) {
        float hn = (fbm(q * 4.5 + vec2(uTime * 0.4, 0.0)) - 0.44) * 0.16;
        f = min(f, r + hn - uHole);
      }
      float aa = fwidth(f) + 0.0015;
      float m = smoothstep(-aa, aa, f);
      if (m > 0.0) {
        // thickness: bevelled rim + slow glossy undulations inside the sheet
        float und = fbm(q * 2.2 + vec2(uTime * 0.05, -uTime * 0.03)) - 0.44;
        float hgt = smoothstep(0.0, 0.05, f) + und * 1.6 * smoothstep(0.01, 0.16, f);
        vec3 n = normalize(vec3(-vec2(dFdx(hgt), dFdy(hgt)) * 0.05 * uRes.y * 0.8, 1.0));
        if (uFloodClear > 0.5) {
          // sea water sheet: refract + tint instead of opaque ink
          vec3 refr = texture2D(tDiffuse, vUv - n.xy * 0.09).rgb;
          vec3 L = normalize(vec3(-0.42, 0.62, 0.66));
          float spec = pow(clamp(dot(n, normalize(L + vec3(0.0, 0.0, 1.0))), 0.0, 1.0), 70.0);
          vec3 w = mix(refr, refr * uFlood.rgb * 2.2 + uFlood.rgb * 0.12, 0.55 + 0.3 * clamp(hgt, 0.0, 1.0)) + spec * 2.2;
          col = mix(col, w, m);
        } else {
          float grain = vnoise(q * 60.0) * 0.05;
          col = mix(col, inkShade(uFlood.rgb * (0.94 + grain), col, n, clamp(hgt, 0.0, 1.0)), m);
        }
      }
      if (uHole > 0.0) {
        float rd = abs(r + (fbm(q * 4.5 + vec2(uTime * 0.4, 0.0)) - 0.44) * 0.16 - uHole);
        col += uHoleRim * exp(-rd * 26.0) * 0.9 * (1.0 - smoothstep(0.9, 1.25, uHole));
      }
    }

    // ---------------------------------------------------------------- whiteout flash
    if (uFlash.a > 0.001) {
      col = mix(col, vec3(luma(col)), clamp(uFlash.a * 0.35, 0.0, 0.5));
      col += uFlash.rgb * uFlash.a * (0.2 + 0.8 * smoothstep(0.12, 0.95, r));   // edge-weighted: the centre (the action) stays readable
    }
    gl_FragColor = vec4(col, 1.0);
  }
