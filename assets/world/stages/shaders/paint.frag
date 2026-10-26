#version 450 core
#define varying in
layout(location=0) out vec4 iwPaintColor;
#define gl_FragColor iwPaintColor


varying vec3 vLocal;     // metres from the splat centre in face space; z = centre's distance from the face plane
varying vec4 vSplat;     // final radius, team, seed, flags (isWall + 2 × kind)
varying vec3 vStretch;   // travel direction in face space, smear amount (0 = none)
varying vec4 vGrow;      // x: age / spread time (runs on past 1) · y: drip progress 0..1 · z: 1 = drips only
float hsh(float n) { return fract(sin(n) * 43758.5453123); }
float wob(float a, float s) {
  return 1.0 + 0.12 * sin(3.0 * a + s * 6.2831) + 0.08 * sin(5.0 * a + s * 17.0) + 0.05 * sin(7.0 * a + s * 41.0)
    + 0.03 * sin(11.0 * a + s * 73.0) + 0.018 * sin(17.0 * a + s * 29.0)
    + 0.17 * pow(max(cos(a - s * 37.7), 0.0), 28.0) + 0.12 * pow(max(cos(a - s * 53.3 - 2.1), 0.0), 36.0);
}
float smin(float a, float b, float k) { float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0); return mix(b, a, h) - k * h * (1.0 - h); }
// thin tapered ray from a (radius ra) to b (radius rb)
float sdRay(vec2 p, vec2 a, vec2 b, float ra, float rb) {
  vec2 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-8), 0.0, 1.0);
  return length(pa - ba * h) - mix(ra, rb, h);
}
// per kind: rays, satellite droplets, spatter dots, drips
vec4 kindShape(float k) {
  if (k < 0.5) return vec4(5.0, 7.0, 8.0, 3.0);     // shot
  if (k < 1.5) return vec4(3.0, 4.0, 5.0, 2.0);     // charger line
  if (k < 2.5) return vec4(7.0, 9.0, 10.0, 4.0);    // blast
  if (k < 3.5) return vec4(10.0, 12.0, 14.0, 5.0);  // bomb / slam / splat-out
  if (k < 4.5) return vec4(3.0, 4.0, 4.0, 2.0);     // trail drip
  if (k < 5.5) return vec4(2.0, 2.0, 0.0, 1.0);     // droplet paint
  return vec4(0.0);                                  // roller band, speck
}
void main() {
  float R = vSplat.x, team = vSplat.y, seed = vSplat.z;
  float isWall = mod(vSplat.w, 2.0);
  float kind = floor(vSplat.w * 0.5 + 0.01);
  float dn = vLocal.z;
  float r2 = R * R - dn * dn;
  if (r2 <= 0.0) discard;
  float r = sqrt(r2);                                    // footprint radius on this face
  float fall = clamp(r / max(R, 1e-3), 0.0, 1.0);        // 1 on the face the blob hit, smaller on faces it grazes
  vec2 p0 = vLocal.xy;
  float tx = max(max(abs(dFdx(p0.x)), abs(dFdy(p0.x))), max(abs(dFdx(p0.y)), abs(dFdy(p0.y))));   // metres per texel
  vec2 dir = vStretch.xy; float sa = vStretch.z;
  vec2 p = p0;
  if (sa > 0.0) {                                        // shots: smeared forward along the travel direction
    float a = dot(p, dir); vec2 perp = p - a * dir;
    float s = a > 0.0 ? 1.0 + sa : 1.0 + 0.25 * sa;
    p = perp + dir * (a / s);
  }
  float tn = vGrow.x;
  vec4 ks = kindShape(kind);
  float sd = 1e3;
  if (vGrow.z < 0.5) {
    // ---- body: floods out from ~40 % with a strong ease-out; its final edge is the CPU gameplay edge
    float tb = 1.0 - pow(1.0 - clamp(tn, 0.0, 1.0), 4.0);
    float grow = mix(0.4, 1.0, tb);
    if (kind > 5.5 && kind < 6.5) {
      // roller band: a straight-edged segment across the drum, edges gently wavy
      vec2 bx = vec2(-dir.y, dir.x);
      vec2 q = vec2(dot(p0, dir), dot(p0, bx));
      float wv = r * (0.03 * sin(q.x / r * 9.0 + seed * 30.0) + 0.018 * sin(q.x / r * 23.0 + seed * 11.0));
      vec2 dq = abs(q) - vec2(r * 0.55 * grow, r * 0.62 + wv);
      sd = length(max(dq, 0.0)) + min(max(dq.x, dq.y), 0.0) - r * 0.1;
    } else if (kind > 6.5) {
      sd = length(p) - r * grow * (1.0 + 0.12 * sin(3.0 * atan(p.y, p.x) + seed * 20.0));
    } else {
      sd = length(p) - r * grow * wob(atan(p.y, p.x), seed);
    }
    float dirAng = sa > 0.0 ? atan(dir.y, dir.x) : 0.0;
    float spread = mix(6.2831, 2.5, clamp(sa * 1.2, 0.0, 1.0));
    bool big = kind > 1.5 && kind < 3.5;
    // ---- rays: short tapered streaks shot out ahead of the body (the splat's "star"), mostly stubby with the odd
    // long one, each ending in a bead where the ink collected as it flew
    float tsp = 1.0 - pow(1.0 - clamp(tn * 1.4, 0.0, 1.0), 3.0);
    for (int k = 0; k < 10; k++) {
      float fk = float(k);
      if (fk >= ks.x) break;
      float h1 = hsh(seed * 7.31 + fk * 1.93), h2 = hsh(seed * 3.17 + fk * 5.71), h3 = hsh(seed * 11.3 + fk * 2.39);
      float a = sa > 0.0 ? dirAng + (h1 - 0.5) * spread : (fk + 0.35 + 0.6 * h1) / ks.x * 6.2831 + seed * 6.2831;
      vec2 u = vec2(cos(a), sin(a));
      float edge = r * grow * wob(a, seed);
      float len = r * (0.07 + (big ? 0.5 : 0.4) * h2 * h2 * h2) * tsp;
      float wB = r * (0.055 + 0.06 * h3);
      float tipR = max(r * (0.012 + 0.012 * h3), tx * 0.45);
      vec2 tip = u * (edge + len) + vec2(-u.y, u.x) * len * 0.18 * (h1 - 0.5);
      float ray = min(sdRay(p, u * edge * 0.72, tip, wB, tipR), length(p - tip) - tipR * (1.6 + 1.4 * h2));
      sd = smin(sd, ray, r * 0.06);
    }
    // ---- satellite droplets flung off the crown: they land a beat after the body (the farthest last), streaked
    // along their flight line; on walls gravity drags the spray down a little
    for (int k = 0; k < 12; k++) {
      float fk = float(k);
      if (fk >= ks.y) break;
      float h1 = hsh(seed * 13.1 + fk * 7.7), h2 = hsh(seed * 5.3 + fk * 3.1), h3 = hsh(seed * 9.9 + fk * 1.7);
      float tl = 0.28 + 0.95 * h2;
      float land = smoothstep(tl, tl + 0.2, tn);
      if (land <= 0.0) continue;
      float a2 = sa > 0.0 ? dirAng + (h1 - 0.5) * spread : h1 * 6.2831;
      vec2 u = vec2(cos(a2), sin(a2));
      if (isWall > 0.5) u = normalize(mix(u, vec2(0.0, -1.0), 0.32));
      float dist = r * (1.1 + (big ? 1.05 : 0.8) * h2 * h2);
      float rad = r * (0.028 + 0.085 * h3) * fall * (1.0 - 0.4 * h2) * (big ? 0.8 : 1.0) * land;
      vec2 q = p - u * dist;
      float el = 1.0 + (0.5 + 1.6 * sa) * h2;
      q -= u * dot(q, u) * (1.0 - 1.0 / el);
      sd = smin(sd, length(q) - rad, rad * 0.8);
    }
    // ---- fine spatter: tiny dots sprayed farther out, landing last
    for (int k = 0; k < 14; k++) {
      float fk = float(k);
      if (fk >= ks.z) break;
      float h1 = hsh(seed * 17.9 + fk * 4.13), h2 = hsh(seed * 2.71 + fk * 8.09), h3 = hsh(seed * 6.47 + fk * 3.37);
      if (tn < 0.45 + 0.95 * h2) continue;
      float a3 = sa > 0.0 ? dirAng + (h1 - 0.5) * spread * 1.15 : h1 * 6.2831;
      vec2 u = vec2(cos(a3), sin(a3));
      if (isWall > 0.5) u = normalize(mix(u, vec2(0.0, -1.0), 0.25));
      float rad = max(r * (0.011 + 0.02 * h3) * fall, tx * 0.9);
      sd = min(sd, length(p - u * r * (1.3 + 1.2 * h2)) - rad);
    }
  }
  // ---- drips on walls: the lower edge sags into streams that keep running; each thins as its bulbous head carries
  // the ink down, meandering a little
  if (isWall > 0.5 && fall > 0.3 && ks.w > 0.0) {
    float dT = vGrow.y;
    float nD = min(6.0, ks.w + floor(R * 1.2));
    for (int k = 0; k < 6; k++) {
      float fk = float(k);
      if (fk >= nD) break;
      float h1 = hsh(seed * 3.7 + fk * 11.3), h2 = hsh(seed * 8.1 + fk * 2.9), h3 = hsh(seed * 4.3 + fk * 5.9);
      if (k > 1 && h3 < 0.3) continue;
      float x = (h1 * 2.0 - 1.0) * r * 0.72;
      float c = sqrt(max(1.0 - (x / r) * (x / r), 0.0));
      float yTop = -c * r * 0.7;
      float len = c * r * 0.25 + r * (0.3 + 2.3 * h2 * h2) * fall * dT;
      float w = r * (0.042 + 0.04 * h3) * (0.75 + 0.35 * fall);
      vec2 q = p0 - vec2(x, yTop);
      float ty = clamp(-q.y / max(len, 1e-4), 0.0, 1.0);
      q.x += sin(q.y / r * 9.0 + seed * 20.0 + fk * 2.3) * w * 0.35 * ty;
      float wt = w * mix(1.0, 0.6, smoothstep(0.05, 0.85, ty));
      float stream = max(abs(q.x) - wt, max(q.y, -len - q.y));
      vec2 tq = (q - vec2(0.0, -len + w * 0.3)) * vec2(1.0, 0.8);
      float bulb = length(tq) - w * (1.2 + 0.35 * h2) * (0.6 + 0.4 * dT);
      sd = smin(sd, smin(stream, bulb, w * 0.9), w * 1.2);
    }
  }
  float fw = max(fwidth(sd), 1e-5);
  float a = 1.0 - smoothstep(-1.5 * fw, 1.5 * fw, sd);
  if (a <= 0.002) discard;
  gl_FragColor = vec4(team, 1.0, hsh(seed * 1.73), a);   // premultiplied by the blend: team share, wet, tone
}