#version 450 core
#define varying in
#define texture2D texture
#define textureCube texture
layout(location=0) out vec4 postColor;
#define gl_FragColor postColor
float luminance(const in vec3 rgb){return dot(vec3(0.2126,0.7152,0.0722),rgb);}

    uniform sampler2D tDiffuse; uniform float uSat; uniform float uVignette; uniform float uHurt; uniform vec3 uHurtColor; uniform float uFlash; uniform float uAspect;
    uniform float uVib; uniform float uContrast; uniform vec3 uShadowTint; uniform vec3 uHighTint; uniform float uLift; uniform float uExposure;
    varying vec2 vUv;
    void main(){
      vec4 c = texture2D(tDiffuse, vUv);
      c.rgb *= uExposure;
      float l = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
      // vibrance: muted colours gain saturation, already-saturated ones (team ink) barely move
      float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));
      float chroma = (mx - mn) / max(mx, 1e-4);
      c.rgb = max(mix(vec3(l), c.rgb, uSat + uVib * (1.0 - smoothstep(0.1, 0.7, chroma))), 0.0);
      // contrast in log space around mid grey (keeps HDR highlights ordered), then a cool-shadow / warm-light split tone
      c.rgb = 0.18 * pow(max(c.rgb, vec3(1e-6)) / 0.18, vec3(uContrast)) + uLift;
      // split tone is for the world's neutrals: strongly saturated colours (team ink) keep their exact hue
      float lt = smoothstep(0.015, 0.55, l);
      c.rgb *= mix(vec3(1.0), mix(uShadowTint, uHighTint, lt), 1.0 - 0.85 * smoothstep(0.35, 0.8, chroma));
      vec2 q = (vUv - 0.5) * vec2(uAspect, 1.0);
      float r = length(q);
      float v = smoothstep(0.55, 1.25, r);
      c.rgb *= 1.0 - uVignette * v;
      // low health: the HUD draws the coloured edge; here we only drain saturation + darken the rim slightly
      float lum = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
      c.rgb = mix(c.rgb, vec3(lum), uHurt * 0.45);
      c.rgb *= 1.0 - uHurt * 0.25 * smoothstep(0.4, 1.2, r);
      c.rgb += uFlash;
      gl_FragColor = c;
    }