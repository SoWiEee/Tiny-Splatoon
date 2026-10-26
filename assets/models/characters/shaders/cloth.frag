#version 450 core
#define HIGH_PRECISION
#define PHYSICAL
#define USE_COLOR
#define USE_CLEARCOAT
#define USE_SHEEN
#define OPAQUE
#define USE_SKINNING
#define TONE_MAPPING
#define STANDARD 
#define PHYSICAL 
#define texture2D texture
#define textureCube texture
#define texture2DLodEXT textureLod
#define textureCubeLodEXT textureLod
#define texture2DGradEXT textureGrad
uniform mat4 modelMatrix; uniform mat4 modelViewMatrix; uniform mat4 projectionMatrix; uniform mat4 viewMatrix; uniform mat3 normalMatrix; uniform vec3 cameraPosition; uniform bool isOrthographic;
#define varying in
layout(location=0) out vec4 iwFragColor;
#define gl_FragColor iwFragColor
#define gl_FragDepthEXT gl_FragDepth
#define STANDARD
#ifdef PHYSICAL
	#define IOR
	#define USE_SPECULAR
#endif
uniform vec3 diffuse;
uniform vec3 emissive;
uniform float roughness;
uniform float metalness;
uniform float opacity;
#ifdef IOR
	uniform float ior;
#endif
#ifdef USE_SPECULAR
	uniform float specularIntensity;
	uniform vec3 specularColor;
	#ifdef USE_SPECULAR_COLORMAP
		uniform sampler2D specularColorMap;
	#endif
	#ifdef USE_SPECULAR_INTENSITYMAP
		uniform sampler2D specularIntensityMap;
	#endif
#endif
#ifdef USE_CLEARCOAT
	uniform float clearcoat;
	uniform float clearcoatRoughness;
#endif
#ifdef USE_DISPERSION
	uniform float dispersion;
#endif
#ifdef USE_RETROREFLECTION
	uniform float retroreflectivity;
#endif
#ifdef USE_IRIDESCENCE
	uniform float iridescence;
	uniform float iridescenceIOR;
	uniform float iridescenceThicknessMinimum;
	uniform float iridescenceThicknessMaximum;
#endif
#ifdef USE_SHEEN
	uniform vec3 sheenColor;
	uniform float sheenRoughness;
	#ifdef USE_SHEEN_COLORMAP
		uniform sampler2D sheenColorMap;
	#endif
	#ifdef USE_SHEEN_ROUGHNESSMAP
		uniform sampler2D sheenRoughnessMap;
	#endif
#endif
#ifdef USE_ANISOTROPY
	uniform vec2 anisotropyVector;
	#ifdef USE_ANISOTROPYMAP
		uniform sampler2D anisotropyMap;
	#endif
#endif
varying vec3 vViewPosition;

// source chunk: common
#define PI 3.141592653589793
#define PI2 6.283185307179586
#define PI_HALF 1.5707963267948966
#define RECIPROCAL_PI 0.3183098861837907
#define RECIPROCAL_PI2 0.15915494309189535
#define EPSILON 1e-6
#ifndef saturate
#define saturate( a ) clamp( a, 0.0, 1.0 )
#endif
#define whiteComplement( a ) ( 1.0 - saturate( a ) )
float pow2( const in float x ) { return x*x; }
vec3 pow2( const in vec3 x ) { return x*x; }
float pow3( const in float x ) { return x*x*x; }
float pow4( const in float x ) { float x2 = x*x; return x2*x2; }
float max3( const in vec3 v ) { return max( max( v.x, v.y ), v.z ); }
float average( const in vec3 v ) { return dot( v, vec3( 0.3333333 ) ); }
highp float rand( const in vec2 uv ) {
	const highp float a = 12.9898, b = 78.233, c = 43758.5453;
	highp float dt = dot( uv.xy, vec2( a,b ) ), sn = mod( dt, PI );
	return fract( sin( sn ) * c );
}
#ifdef HIGH_PRECISION
	float precisionSafeLength( vec3 v ) { return length( v ); }
#else
	float precisionSafeLength( vec3 v ) {
		float maxComponent = max3( abs( v ) );
		return length( v / maxComponent ) * maxComponent;
	}
#endif
struct IncidentLight {
	vec3 color;
	vec3 direction;
	bool visible;
};
struct ReflectedLight {
	vec3 directDiffuse;
	vec3 directSpecular;
	vec3 indirectDiffuse;
	vec3 indirectSpecular;
};
#ifdef USE_ALPHAHASH
	varying vec3 vPosition;
#endif
vec3 transformDirection( in vec3 dir, in mat4 matrix ) {
	return normalize( ( matrix * vec4( dir, 0.0 ) ).xyz );
}
#define inverseTransformDirection transformDirectionByInverseViewMatrix
vec3 transformNormalByInverseViewMatrix( in vec3 normal, in mat4 viewMatrix ) {
	return normalize( ( vec4( normal, 0.0 ) * viewMatrix ).xyz );
}
vec3 transformDirectionByInverseViewMatrix( in vec3 dir, in mat4 viewMatrix ) {
	return normalize( ( vec4( dir, 0.0 ) * viewMatrix ).xyz );
}
bool isPerspectiveMatrix( mat4 m ) {
	return m[ 2 ][ 3 ] == - 1.0;
}
vec2 equirectUv( in vec3 dir ) {
	float u = atan( dir.z, dir.x ) * RECIPROCAL_PI2 + 0.5;
	float v = asin( clamp( dir.y, - 1.0, 1.0 ) ) * RECIPROCAL_PI + 0.5;
	return vec2( u, v );
}
vec3 BRDF_Lambert( const in vec3 diffuseColor ) {
	return RECIPROCAL_PI * diffuseColor;
}
vec3 F_Schlick( const in vec3 f0, const in float f90, const in float dotVH ) {
	float fresnel = exp2( ( - 5.55473 * dotVH - 6.98316 ) * dotVH );
	return f0 * ( 1.0 - fresnel ) + ( f90 * fresnel );
}
float F_Schlick( const in float f0, const in float f90, const in float dotVH ) {
	float fresnel = exp2( ( - 5.55473 * dotVH - 6.98316 ) * dotVH );
	return f0 * ( 1.0 - fresnel ) + ( f90 * fresnel );
} // validated


// source chunk: tonemapping_pars_fragment
#ifndef saturate
#define saturate( a ) clamp( a, 0.0, 1.0 )
#endif
uniform float toneMappingExposure;
vec3 LinearToneMapping( vec3 color ) {
	return saturate( toneMappingExposure * color );
}
vec3 ReinhardToneMapping( vec3 color ) {
	color *= toneMappingExposure;
	return saturate( color / ( vec3( 1.0 ) + color ) );
}
vec3 CineonToneMapping( vec3 color ) {
	color *= toneMappingExposure;
	color = max( vec3( 0.0 ), color - 0.004 );
	return pow( ( color * ( 6.2 * color + 0.5 ) ) / ( color * ( 6.2 * color + 1.7 ) + 0.06 ), vec3( 2.2 ) );
}
vec3 RRTAndODTFit( vec3 v ) {
	vec3 a = v * ( v + 0.0245786 ) - 0.000090537;
	vec3 b = v * ( 0.983729 * v + 0.4329510 ) + 0.238081;
	return a / b;
}
vec3 ACESFilmicToneMapping( vec3 color ) {
	const mat3 ACESInputMat = mat3(
		vec3( 0.59719, 0.07600, 0.02840 ),		vec3( 0.35458, 0.90834, 0.13383 ),
		vec3( 0.04823, 0.01566, 0.83777 )
	);
	const mat3 ACESOutputMat = mat3(
		vec3(  1.60475, -0.10208, -0.00327 ),		vec3( -0.53108,  1.10813, -0.07276 ),
		vec3( -0.07367, -0.00605,  1.07602 )
	);
	color *= toneMappingExposure / 0.6;
	color = ACESInputMat * color;
	color = RRTAndODTFit( color );
	color = ACESOutputMat * color;
	return saturate( color );
}
const mat3 LINEAR_REC2020_TO_LINEAR_SRGB = mat3(
	vec3( 1.6605, - 0.1246, - 0.0182 ),
	vec3( - 0.5876, 1.1329, - 0.1006 ),
	vec3( - 0.0728, - 0.0083, 1.1187 )
);
const mat3 LINEAR_SRGB_TO_LINEAR_REC2020 = mat3(
	vec3( 0.6274, 0.0691, 0.0164 ),
	vec3( 0.3293, 0.9195, 0.0880 ),
	vec3( 0.0433, 0.0113, 0.8956 )
);
vec3 agxDefaultContrastApprox( vec3 x ) {
	vec3 x2 = x * x;
	vec3 x4 = x2 * x2;
	return + 15.5 * x4 * x2
		- 40.14 * x4 * x
		+ 31.96 * x4
		- 6.868 * x2 * x
		+ 0.4298 * x2
		+ 0.1191 * x
		- 0.00232;
}
vec3 AgXToneMapping( vec3 color ) {
	const mat3 AgXInsetMatrix = mat3(
		vec3( 0.856627153315983, 0.137318972929847, 0.11189821299995 ),
		vec3( 0.0951212405381588, 0.761241990602591, 0.0767994186031903 ),
		vec3( 0.0482516061458583, 0.101439036467562, 0.811302368396859 )
	);
	const mat3 AgXOutsetMatrix = mat3(
		vec3( 1.1271005818144368, - 0.1413297634984383, - 0.14132976349843826 ),
		vec3( - 0.11060664309660323, 1.157823702216272, - 0.11060664309660294 ),
		vec3( - 0.016493938717834573, - 0.016493938717834257, 1.2519364065950405 )
	);
	const float AgxMinEv = - 12.47393;	const float AgxMaxEv = 4.026069;
	color *= toneMappingExposure;
	color = LINEAR_SRGB_TO_LINEAR_REC2020 * color;
	color = AgXInsetMatrix * color;
	color = max( color, 1e-10 );	color = log2( color );
	color = ( color - AgxMinEv ) / ( AgxMaxEv - AgxMinEv );
	color = clamp( color, 0.0, 1.0 );
	color = agxDefaultContrastApprox( color );
	color = AgXOutsetMatrix * color;
	color = pow( max( vec3( 0.0 ), color ), vec3( 2.2 ) );
	color = LINEAR_REC2020_TO_LINEAR_SRGB * color;
	color = clamp( color, 0.0, 1.0 );
	return color;
}
vec3 NeutralToneMapping( vec3 color ) {
	const float StartCompression = 0.8 - 0.04;
	const float Desaturation = 0.15;
	color *= toneMappingExposure;
	float x = min( color.r, min( color.g, color.b ) );
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	color -= offset;
	float peak = max( color.r, max( color.g, color.b ) );
	if ( peak < StartCompression ) return color;
	float d = 1. - StartCompression;
	float newPeak = 1. - d * d / ( peak + d - StartCompression );
	color *= newPeak / peak;
	float g = 1. - 1. / ( Desaturation * ( peak - newPeak ) + 1. );
	return mix( color, vec3( newPeak ), g );
}
vec3 CustomToneMapping( vec3 color ) { return color; }


// source chunk: colorspace_pars_fragment
vec4 LinearTransferOETF( in vec4 value ) {
	return value;
}
vec4 sRGBTransferEOTF( in vec4 value ) {
	return vec4( mix( pow( value.rgb * 0.9478672986 + vec3( 0.0521327014 ), vec3( 2.4 ) ), value.rgb * 0.0773993808, vec3( lessThanEqual( value.rgb, vec3( 0.04045 ) ) ) ), value.a );
}
vec4 sRGBTransferOETF( in vec4 value ) {
	return vec4( mix( pow( value.rgb, vec3( 0.41666 ) ) * 1.055 - vec3( 0.055 ), value.rgb * 12.92, vec3( lessThanEqual( value.rgb, vec3( 0.0031308 ) ) ) ), value.a );
}

vec3 toneMapping(vec3 color) { return NeutralToneMapping(color); }
vec4 linearToOutputTexel(vec4 color) { return sRGBTransferOETF(color); }
uniform vec4 uIwRim; uniform vec3 uIwRimL; uniform vec3 uIwFill;

uniform vec4 uHurt; uniform float uHurtSeed; uniform vec3 uFlash;
varying vec3 vBindPos; varying float vEx;

float iwHash(vec3 p){ p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3)); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
float iwNoise(vec3 x){ vec3 i = floor(x); vec3 f = fract(x); f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(iwHash(i), iwHash(i + vec3(1,0,0)), f.x), mix(iwHash(i + vec3(0,1,0)), iwHash(i + vec3(1,1,0)), f.x), f.y),
             mix(mix(iwHash(i + vec3(0,0,1)), iwHash(i + vec3(1,0,1)), f.x), mix(iwHash(i + vec3(0,1,1)), iwHash(i + vec3(1,1,1)), f.x), f.y), f.z); }
float iwFbm(vec3 p){ return 0.56 * iwNoise(p) + 0.29 * iwNoise(p * 2.07 + 7.1) + 0.15 * iwNoise(p * 4.31 + 3.7); }
float iwBand(float x, float a, float b){ float w = max(fwidth(x) * 0.8, 1e-4); return smoothstep(a - w, a + w, x) - smoothstep(b - w, b + w, x); }
float iwHash2(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123); }
float iwAAw(float d){ return max(fwidth(d), 1e-5); }
// filled SDF (d < 0 inside) and stroke of half-width hw, both antialiased
float iwFill(float d){ float w = iwAAw(d); return 1.0 - smoothstep(-w, w, d); }
float iwStroke(float d, float hw){ float w = iwAAw(d); return 1.0 - smoothstep(hw - w, hw + w, abs(d)); }
// 1 while a pattern of spatial period P (same units as coord) is resolvable (>= ~3 px), fading to 0 when sub-pixel
float iwLod(float coord, float P){ float px = fwidth(coord) / P; return 1.0 - smoothstep(0.22, 0.55, px); }
// dashed stitch along s (period P, duty 0..1)
float iwDash(float s, float P, float duty){ float x = s / P; float f = fract(x); float w = min(max(fwidth(x), 1e-4) * 1.2, 0.3); return smoothstep(0.0, w, f) * (1.0 - smoothstep(duty - w, duty, f)); }
float iwSdBox(vec2 p, vec2 b, float r){ vec2 q = abs(p) - b + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }
float iwSdSeg(vec2 p, vec2 a, vec2 b){ vec2 pa = p - a, ba = b - a; float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0); return length(pa - ba * h); }
float iwSmin(float a, float b, float k){ float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0); return mix(b, a, h) - k * h * (1.0 - h); }


vec3 iwBumpN(vec3 n, float h, vec3 pos){
  vec3 dpx = dFdx(pos), dpy = dFdy(pos);
  vec3 r1 = cross(dpy, n), r2 = cross(n, dpx);
  float det = dot(dpx, r1);
  vec3 g = sign(det) * (dFdx(h) * r1 + dFdy(h) * r2);
  return abs(det) > 1e-14 ? normalize(abs(det) * n - g) : n;
}


uniform vec3 uTeam; uniform vec3 uShirt; uniform vec3 uShorts; uniform vec3 uShoe; uniform vec3 uSole; uniform vec3 uSock; uniform vec3 uStrap; uniform float uPattern;
varying vec3 vCloth; varying vec2 vIwUv; varying float vOcc;
float iwLum(vec3 c){ return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 iwContrast(vec3 c){ return iwLum(c) > 0.42 ? c * 0.74 : c * 1.55 + 0.05; }
// ---- tee fine creases: complements the sculpted fold field of character-outfit.js ------------------------
float iwTeeCrease(vec3 p, float th) {
  float ax = abs(p.x), h = 0.0;
  float side = pow(abs(sin(th * 6.2831)), 2.0);
  float wy = (p.y - 0.705) / 0.0105 + 0.6 * sin(th * 6.2831 * 9.0 + p.y * 40.0);
  h += 0.00032 * side * exp(-pow((p.y - 0.765) / 0.035, 2.0)) * (0.5 + 0.5 * sin(wy * 6.2831));
  vec2 q = vec2(ax - 0.114, p.y - 0.884);
  float r = length(q), ang = atan(q.y, -q.x);
  h += 0.00011 * sin(ang * 17.0 + 1.3 + (p.z > 0.0 ? 0.0 : 2.1) + 2.5 * iwNoise(vec3(ang * 3.0, r * 60.0, p.z * 20.0))) * exp(-pow(r / 0.045, 2.0)) * smoothstep(0.006, 0.02, r) * (0.4 + 0.6 * iwNoise(p * 90.0));
  h += 0.00028 * sin(th * 6.2831 * 23.0 + 2.0 * sin(th * 30.0)) * (1.0 - smoothstep(0.698, 0.726, p.y));
  return h;
}
// ---- tee graphics -------------------------------------------------------------------------------
float iwShirtPattern(vec3 p) {
  float ax = abs(p.x);
  float m = 0.0;
  if (uPattern < 0.5) {            // ringer: no body pattern (trim + big emblem)
    m = 0.0;
  } else if (uPattern < 1.5) {     // double pinstripe
    m = iwBand(p.y, 0.842, 0.852) + iwBand(p.y, 0.866, 0.876);
  } else if (uPattern < 2.5) {     // raglan sleeves + side panel
    float sleeve = smoothstep(0.108, 0.115, ax) * step(0.86, p.y);
    m = max(sleeve, iwBand(ax, 0.098, 0.2) * step(p.y, 0.86) * step(0.74, p.y) * step(abs(p.z + 0.012), 0.05));
  } else {                         // chevron + hem band
    float y0 = 0.925 - 0.55 * ax;
    m = iwBand(p.y - y0, -0.022, 0.0) * step(0.0, p.z);
    m = max(m, iwBand(p.y, 0.726, 0.738));
  }
  return clamp(m, 0.0, 1.0);
}
// ---- outfit patterns 4+ (appended; 0–3 above stay byte-identical so saved looks keep their graphics) -------------
// collar rib / sleeve cuff colours per pattern
vec3 iwTrimCol() {
  float pat = floor(uPattern + 0.5);
  if (pat < 0.5 || pat == 3.0) return uTeam;
  if (pat < 2.5) return iwContrast(uShirt);
  if (pat == 6.0) return uShirt * 0.55;            // camo: dark olive rib
  if (pat == 7.0) return uShirt;                   // dip-dye: undyed collar
  return uTeam;                                    // breton, splatter, jersey, track
}
vec3 iwCuffCol() {
  float pat = floor(uPattern + 0.5);
  if (pat < 0.5 || pat == 3.0) return uTeam;
  if (pat < 2.5) return iwContrast(uShirt);
  if (pat == 4.0 || pat == 5.0) return uShirt;     // breton / splatter: plain cuff
  if (pat == 6.0) return uShirt * 0.55;
  if (pat == 7.0) return uTeam * 0.9;              // dip-dyed ends
  return uTeam;                                    // jersey, track
}
// ink splat stamped on the (roughly cylindrical) torso/sleeve around surface point c: wobbly rim + satellite drops
float iwSplat3(vec3 p, vec3 c, float R, float seed) {
  vec3 d = p - c;
  vec3 n = normalize(vec3(c.x, 0.0, c.z + 0.012));
  vec3 u = normalize(cross(vec3(0.0, 1.0, 0.0), n));
  vec2 q = vec2(dot(d, u), d.y);
  float a = atan(q.y, q.x);
  float wob = 1.0 + 0.13 * sin(5.0 * a + seed) + 0.07 * sin(9.0 * a + seed * 2.3) + 0.04 * sin(15.0 * a + seed * 0.7);
  float sd = length(q) - R * wob;
  for (int k = 0; k < 4; k++) {
    float fk = float(k); float aa = seed * 1.7 + fk * 1.9;
    vec2 o = vec2(cos(aa), sin(aa)) * R * (1.28 + 0.4 * fract(fk * 0.37 + seed));
    sd = min(sd, length(q - o) - R * (0.08 + 0.07 * fract(fk * 0.71 + seed * 0.3)));
  }
  sd = max(sd, abs(dot(d, n)) - 0.045);            // stay on this side of the body
  return iwFill(sd);
}
// athletic block digit (seven segments, rounded); q in digit units (half-width 0.5, half-height 1); SDF in units
float iwDigitSD(vec2 q, int dgt) {
  const int SEG[10] = int[10](0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F);
  int bits = SEG[clamp(dgt, 0, 9)];
  float d = 1e3;
  if ((bits & 1) != 0) d = min(d, iwSdBox(q - vec2(0.0, 0.86), vec2(0.4, 0.14), 0.07));
  if ((bits & 2) != 0) d = min(d, iwSdBox(q - vec2(0.36, 0.44), vec2(0.14, 0.42), 0.07));
  if ((bits & 4) != 0) d = min(d, iwSdBox(q - vec2(0.36, -0.44), vec2(0.14, 0.42), 0.07));
  if ((bits & 8) != 0) d = min(d, iwSdBox(q - vec2(0.0, -0.86), vec2(0.4, 0.14), 0.07));
  if ((bits & 16) != 0) d = min(d, iwSdBox(q - vec2(-0.36, -0.44), vec2(0.14, 0.42), 0.07));
  if ((bits & 32) != 0) d = min(d, iwSdBox(q - vec2(-0.36, 0.44), vec2(0.14, 0.42), 0.07));
  if ((bits & 64) != 0) d = min(d, iwSdBox(q, vec2(0.36, 0.13), 0.07));
  return d;
}
/** Base colour of shirt-slot fragments (tee body = part 1, sleeves = part 2). */
vec3 iwShirtCol(vec3 p, vec2 uv, float part) {
  if (uPattern < 3.5) return mix(uShirt, uTeam, iwShirtPattern(p));
  float pat = floor(uPattern + 0.5);
  vec3 c = uShirt;
  float ax = abs(p.x), zc = p.z + 0.012;
  if (pat == 4.0) {                        // BRETON: team stripes on the body and around the sleeves, plain yoke
    float sy, P, lo, hi;
    if (part < 1.5) { sy = p.y; P = 0.026; lo = 0.703; hi = 0.938; }
    else { sy = uv.y; P = 0.0235; lo = 0.036; hi = vCloth.z - 0.024; }
    float f = abs(fract((sy - lo) / P) - 0.5);
    float band = iwFill((f - 0.21) * P) * step(lo, sy) * step(sy, hi);
    float m = mix(0.42 * step(lo, sy) * step(sy, hi), band, iwLod(sy, P));
    c = mix(uShirt, uTeam, m);
  } else if (pat == 5.0) {                 // SPLATTER: team ink splats, drips and speckles on a dark tee
    float m = 0.0;
    m = max(m, iwSplat3(p, vec3(0.062, 0.93, 0.072), 0.042, 1.3));
    m = max(m, iwSplat3(p, vec3(-0.058, 0.768, 0.086), 0.025, 4.1));
    m = max(m, iwSplat3(p, vec3(-0.096, 0.952, -0.052), 0.04, 2.2));
    m = max(m, iwSplat3(p, vec3(0.03, 0.742, -0.088), 0.027, 5.7));
    m = max(m, iwSplat3(p, vec3(0.2, 0.872, 0.004), 0.028, 3.3));
    if (part < 1.5 && p.z > 0.0) {
      for (int k = 0; k < 3; k++) {
        float fk = float(k);
        float x0 = 0.04 + 0.019 * fk; float y1 = 0.905 - 0.018 * fk; float y0 = y1 - (0.07 + 0.04 * fract(fk * 0.618 + 0.3));
        float w = 0.0056 - 0.0012 * fk;
        float dd = iwSdSeg(vec2(p.x, p.y), vec2(x0, y0), vec2(x0, y1)) - w * mix(1.0, 0.7, (y1 - p.y) / (y1 - y0));
        dd = min(dd, length(vec2(p.x - x0, (p.y - y0) * 0.85)) - w * 1.5);
        m = max(m, iwFill(dd));
      }
    }
    vec3 cp = p * 75.0; vec3 cell = floor(cp); vec3 fr = fract(cp) - 0.5;
    float rnd = iwHash(cell);
    m = max(m, iwFill((length(fr) - 0.24 * rnd) / 75.0) * step(0.87, rnd) * iwLod(p.y * 75.0, 1.0));
    c = mix(uShirt, uTeam, m);
  } else if (pat == 6.0) {                 // SPLAT CAMO: sand + shadow blobs over olive, with a few team-ink patches
    vec3 q = p * vec3(15.0, 11.0, 15.0);
    float n1 = iwFbm(q + 3.1), n2 = iwFbm(q * 1.15 + 11.7), n3 = iwFbm(q * 0.85 + 27.3);
    c = mix(c, mix(uShirt, vec3(0.8, 0.76, 0.62), 0.5), iwFill(0.585 - n2));
    c = mix(c, uShirt * 0.5, iwFill(0.575 - n1));
    c = mix(c, uTeam, iwFill(0.655 - n3));
  } else if (pat == 7.0) {                 // DIP-DYE: team dye soaked up from the hem with a bleeding waterline
    float wav = 0.011 * sin(uv.x * 6.2831 * 3.0 + 1.1) + 0.024 * (iwFbm(p * 26.0) - 0.5);
    vec3 pale = mix(uTeam, uShirt, 0.42);
    if (part < 1.5) {
      float line = 0.792 + wav;
      float k = 1.0 - smoothstep(line - 0.026, line + 0.004, p.y);
      c = mix(uShirt, mix(pale, uTeam * 0.92, 1.0 - smoothstep(0.7, line, p.y)), k);
      float streak = smoothstep(0.62, 0.9, iwNoise(vec3(p.x * 170.0, p.y * 7.0, p.z * 170.0))) * (1.0 - smoothstep(line, line + 0.04, p.y)) * step(line - 0.004, p.y);
      c = mix(c, mix(uShirt, uTeam, 0.3), streak * 0.65);
    } else {
      float L = vCloth.z;
      float k = smoothstep(L - 0.075 + wav, L - 0.035 + wav, uv.y);
      c = mix(uShirt, mix(pale, uTeam * 0.92, smoothstep(L - 0.05, L, uv.y)), k);
    }
  } else if (pat == 8.0) {                 // JERSEY: team side panels with white piping, V-neck insert, shoulder yoke
    if (part < 1.5) {
      float side = step(0.07, ax);
      float panel = iwFill(abs(zc) - 0.043) * side * step(p.y, 0.95);
      float pipe = iwStroke(abs(zc) - 0.043, 0.0017) * side * step(p.y, 0.95);
      float vneck = iwSdSeg(vec2(ax, p.y), vec2(0.056, 1.0), vec2(0.0, 0.93));
      float v = iwFill(vneck - 0.0085) * step(0.0, p.z);
      float vpipe = iwStroke(vneck - 0.0085, 0.0015) * step(0.0, p.z);
      float yoke = smoothstep(0.962, 0.966, p.y) * (1.0 - v);
      c = mix(c, uTeam, max(max(panel, v), yoke));
      c = mix(c, vec3(0.96), max(pipe, vpipe));
    } else {
      float L = vCloth.z;
      float pipe = iwStroke(uv.y - (L - 0.03), 0.0016);
      c = mix(c, uTeam, smoothstep(0.02, 0.024, uv.y) * (1.0 - smoothstep(0.034, 0.038, uv.y)));   // shoulder yoke continues
      c = mix(c, vec3(0.96), pipe);
    }
  } else {                                 // TRACK TOP: twin white stripes down the sides and sleeves, team yoke
    vec3 sc = vec3(0.95);
    if (part < 1.5) {
      float st = (iwStroke(zc - 0.0085, 0.0032) + iwStroke(zc + 0.0085, 0.0032)) * step(0.07, ax);
      float yoke = smoothstep(0.955, 0.959, p.y);
      c = mix(c, uTeam, yoke);
      c = mix(c, sc, clamp(st, 0.0, 1.0));
      c = mix(c, sc, iwStroke(p.y - 0.957, 0.0014));
    } else {
      float dx = (fract(uv.x + 0.5) - 0.5) * 0.31;
      float st = iwStroke(dx - 0.0085, 0.0032) + iwStroke(dx + 0.0085, 0.0032);
      c = mix(c, uTeam, 1.0 - smoothstep(0.03, 0.034, uv.y));
      c = mix(c, sc, clamp(st, 0.0, 1.0) * smoothstep(0.03, 0.034, uv.y));
    }
  }
  return c;
}

// squid badge: ink splat with a white squid glyph; returns (splat coverage, glyph coverage)
vec2 iwEmblem(vec2 q, float R) {
  float a = atan(q.y, q.x); float r = length(q);
  float wob = 1.0 + 0.1 * sin(5.0 * a + 0.6) + 0.06 * sin(9.0 * a + 2.1) + 0.035 * sin(14.0 * a + 1.0);
  float splat = r - R * wob;
  splat = min(splat, length(q - vec2(R * 1.18, R * 0.62)) - R * 0.13);
  splat = min(splat, length(q - vec2(-R * 1.12, -R * 0.7)) - R * 0.1);
  splat = min(splat, length(q - vec2(R * 0.5, -R * 1.22)) - R * 0.08);
  // glyph: one silhouette — arrowhead mantle with swept fins flowing into a tall body, low oval eyes, four tentacles
  vec2 g = q / R;
  float fins = max(abs(g.x) * 1.02 + (g.y - 0.7) * 0.95, -(g.y - 0.22));
  float body = iwSdBox(g - vec2(0.0, 0.06), vec2(0.17, 0.3), 0.15);
  float glyph = iwSmin(fins, body, 0.08);
  float tent = 1e3;
  for (int i = 0; i < 4; i++) {
    float fi = float(i) - 1.5;
    vec2 tq = g - vec2(fi * 0.1, -0.2);
    tq.x -= fi * 0.1 * smoothstep(0.0, -0.3, tq.y) + 0.03 * sin(tq.y * 13.0 + fi) * smoothstep(-0.05, -0.3, tq.y);
    float w = mix(0.052, 0.03, smoothstep(-0.02, -0.3, tq.y));
    tent = min(tent, iwSdBox(tq - vec2(0.0, -0.15), vec2(w, 0.15), 0.028));
  }
  glyph = iwSmin(glyph, tent, 0.05);
  vec2 e1 = (g - vec2(0.075, 0.0)) / vec2(0.04, 0.062), e2 = (g - vec2(-0.075, 0.0)) / vec2(0.04, 0.062);
  float eyes = (min(length(e1), length(e2)) - 1.0) * 0.04;
  glyph = max(glyph, -eyes);
  return vec2(iwFill(splat), iwFill(glyph * R));
}



// source chunk: dithering_pars_fragment
#ifdef DITHERING
	vec3 dithering( vec3 color ) {
		float grid_position = rand( gl_FragCoord.xy );
		vec3 dither_shift_RGB = vec3( 0.25 / 255.0, -0.25 / 255.0, 0.25 / 255.0 );
		dither_shift_RGB = mix( 2.0 * dither_shift_RGB, -2.0 * dither_shift_RGB, grid_position );
		return color + dither_shift_RGB;
	}
#endif


// source chunk: color_pars_fragment
#if defined( USE_COLOR ) || defined( USE_COLOR_ALPHA )
	varying vec4 vColor;
#endif


// source chunk: uv_pars_fragment
#if defined( USE_UV ) || defined( USE_ANISOTROPY )
	varying vec2 vUv;
#endif
#ifdef USE_MAP
	varying vec2 vMapUv;
#endif
#ifdef USE_ALPHAMAP
	varying vec2 vAlphaMapUv;
#endif
#ifdef USE_LIGHTMAP
	varying vec2 vLightMapUv;
#endif
#ifdef USE_AOMAP
	varying vec2 vAoMapUv;
#endif
#ifdef USE_BUMPMAP
	varying vec2 vBumpMapUv;
#endif
#ifdef USE_NORMALMAP
	varying vec2 vNormalMapUv;
#endif
#ifdef USE_EMISSIVEMAP
	varying vec2 vEmissiveMapUv;
#endif
#ifdef USE_METALNESSMAP
	varying vec2 vMetalnessMapUv;
#endif
#ifdef USE_ROUGHNESSMAP
	varying vec2 vRoughnessMapUv;
#endif
#ifdef USE_ANISOTROPYMAP
	varying vec2 vAnisotropyMapUv;
#endif
#ifdef USE_CLEARCOATMAP
	varying vec2 vClearcoatMapUv;
#endif
#ifdef USE_CLEARCOAT_NORMALMAP
	varying vec2 vClearcoatNormalMapUv;
#endif
#ifdef USE_CLEARCOAT_ROUGHNESSMAP
	varying vec2 vClearcoatRoughnessMapUv;
#endif
#ifdef USE_IRIDESCENCEMAP
	varying vec2 vIridescenceMapUv;
#endif
#ifdef USE_IRIDESCENCE_THICKNESSMAP
	varying vec2 vIridescenceThicknessMapUv;
#endif
#ifdef USE_SHEEN_COLORMAP
	varying vec2 vSheenColorMapUv;
#endif
#ifdef USE_SHEEN_ROUGHNESSMAP
	varying vec2 vSheenRoughnessMapUv;
#endif
#ifdef USE_SPECULARMAP
	varying vec2 vSpecularMapUv;
#endif
#ifdef USE_SPECULAR_COLORMAP
	varying vec2 vSpecularColorMapUv;
#endif
#ifdef USE_SPECULAR_INTENSITYMAP
	varying vec2 vSpecularIntensityMapUv;
#endif
#ifdef USE_TRANSMISSIONMAP
	uniform mat3 transmissionMapTransform;
	varying vec2 vTransmissionMapUv;
#endif
#ifdef USE_THICKNESSMAP
	uniform mat3 thicknessMapTransform;
	varying vec2 vThicknessMapUv;
#endif


// source chunk: map_pars_fragment
#ifdef USE_MAP
	uniform sampler2D map;
#endif


// source chunk: alphamap_pars_fragment
#ifdef USE_ALPHAMAP
	uniform sampler2D alphaMap;
#endif


// source chunk: alphatest_pars_fragment
#ifdef USE_ALPHATEST
	uniform float alphaTest;
#endif


// source chunk: alphahash_pars_fragment
#ifdef USE_ALPHAHASH
	const float ALPHA_HASH_SCALE = 0.05;
	float hash2D( vec2 value ) {
		return fract( 1.0e4 * sin( 17.0 * value.x + 0.1 * value.y ) * ( 0.1 + abs( sin( 13.0 * value.y + value.x ) ) ) );
	}
	float hash3D( vec3 value ) {
		return hash2D( vec2( hash2D( value.xy ), value.z ) );
	}
	float getAlphaHashThreshold( vec3 position ) {
		float maxDeriv = max(
			length( dFdx( position.xyz ) ),
			length( dFdy( position.xyz ) )
		);
		float pixScale = 1.0 / ( ALPHA_HASH_SCALE * maxDeriv );
		vec2 pixScales = vec2(
			exp2( floor( log2( pixScale ) ) ),
			exp2( ceil( log2( pixScale ) ) )
		);
		vec2 alpha = vec2(
			hash3D( floor( pixScales.x * position.xyz ) ),
			hash3D( floor( pixScales.y * position.xyz ) )
		);
		float lerpFactor = fract( log2( pixScale ) );
		float x = ( 1.0 - lerpFactor ) * alpha.x + lerpFactor * alpha.y;
		float a = min( lerpFactor, 1.0 - lerpFactor );
		vec3 cases = vec3(
			x * x / ( 2.0 * a * ( 1.0 - a ) ),
			( x - 0.5 * a ) / ( 1.0 - a ),
			1.0 - ( ( 1.0 - x ) * ( 1.0 - x ) / ( 2.0 * a * ( 1.0 - a ) ) )
		);
		float threshold = ( x < ( 1.0 - a ) )
			? ( ( x < a ) ? cases.x : cases.y )
			: cases.z;
		return clamp( threshold , 1.0e-6, 1.0 );
	}
#endif


// source chunk: aomap_pars_fragment
#ifdef USE_AOMAP
	uniform sampler2D aoMap;
	uniform float aoMapIntensity;
#endif


// source chunk: lightmap_pars_fragment
#ifdef USE_LIGHTMAP
	uniform sampler2D lightMap;
	uniform float lightMapIntensity;
#endif


// source chunk: emissivemap_pars_fragment
#ifdef USE_EMISSIVEMAP
	uniform sampler2D emissiveMap;
#endif


// source chunk: iridescence_fragment
#ifdef USE_IRIDESCENCE
	const mat3 XYZ_TO_REC709 = mat3(
		 3.2404542, -0.9692660,  0.0556434,
		-1.5371385,  1.8760108, -0.2040259,
		-0.4985314,  0.0415560,  1.0572252
	);
	vec3 Fresnel0ToIor( vec3 fresnel0 ) {
		vec3 sqrtF0 = sqrt( fresnel0 );
		return ( vec3( 1.0 ) + sqrtF0 ) / ( vec3( 1.0 ) - sqrtF0 );
	}
	vec3 IorToFresnel0( vec3 transmittedIor, float incidentIor ) {
		return pow2( ( transmittedIor - vec3( incidentIor ) ) / ( transmittedIor + vec3( incidentIor ) ) );
	}
	float IorToFresnel0( float transmittedIor, float incidentIor ) {
		return pow2( ( transmittedIor - incidentIor ) / ( transmittedIor + incidentIor ));
	}
	vec3 evalSensitivity( float OPD, vec3 shift ) {
		float phase = 2.0 * PI * OPD * 1.0e-9;
		vec3 val = vec3( 5.4856e-13, 4.4201e-13, 5.2481e-13 );
		vec3 pos = vec3( 1.6810e+06, 1.7953e+06, 2.2084e+06 );
		vec3 var = vec3( 4.3278e+09, 9.3046e+09, 6.6121e+09 );
		vec3 xyz = val * sqrt( 2.0 * PI * var ) * cos( pos * phase + shift ) * exp( - pow2( phase ) * var );
		xyz.x += 9.7470e-14 * sqrt( 2.0 * PI * 4.5282e+09 ) * cos( 2.2399e+06 * phase + shift[ 0 ] ) * exp( - 4.5282e+09 * pow2( phase ) );
		xyz /= 1.0685e-7;
		vec3 rgb = XYZ_TO_REC709 * xyz;
		return rgb;
	}
	vec3 evalIridescence( float outsideIOR, float eta2, float cosTheta1, float thinFilmThickness, vec3 baseF0 ) {
		vec3 I;
		float iridescenceIOR = mix( outsideIOR, eta2, smoothstep( 0.0, 0.03, thinFilmThickness ) );
		float sinTheta2Sq = pow2( outsideIOR / iridescenceIOR ) * ( 1.0 - pow2( cosTheta1 ) );
		float cosTheta2Sq = 1.0 - sinTheta2Sq;
		if ( cosTheta2Sq < 0.0 ) {
			return vec3( 1.0 );
		}
		float cosTheta2 = sqrt( cosTheta2Sq );
		float R0 = IorToFresnel0( iridescenceIOR, outsideIOR );
		float R12 = F_Schlick( R0, 1.0, cosTheta1 );
		float T121 = 1.0 - R12;
		float phi12 = 0.0;
		if ( iridescenceIOR < outsideIOR ) phi12 = PI;
		float phi21 = PI - phi12;
		vec3 baseIOR = Fresnel0ToIor( clamp( baseF0, 0.0, 0.9999 ) );		vec3 R1 = IorToFresnel0( baseIOR, iridescenceIOR );
		vec3 R23 = F_Schlick( R1, 1.0, cosTheta2 );
		vec3 phi23 = vec3( 0.0 );
		if ( baseIOR[ 0 ] < iridescenceIOR ) phi23[ 0 ] = PI;
		if ( baseIOR[ 1 ] < iridescenceIOR ) phi23[ 1 ] = PI;
		if ( baseIOR[ 2 ] < iridescenceIOR ) phi23[ 2 ] = PI;
		float OPD = 2.0 * iridescenceIOR * thinFilmThickness * cosTheta2;
		vec3 phi = vec3( phi21 ) + phi23;
		vec3 R123 = clamp( R12 * R23, 1e-5, 0.9999 );
		vec3 r123 = sqrt( R123 );
		vec3 Rs = pow2( T121 ) * R23 / ( vec3( 1.0 ) - R123 );
		vec3 C0 = R12 + Rs;
		I = C0;
		vec3 Cm = Rs - T121;
		for ( int m = 1; m <= 2; ++ m ) {
			Cm *= r123;
			vec3 Sm = 2.0 * evalSensitivity( float( m ) * OPD, float( m ) * phi );
			I += Cm * Sm;
		}
		return max( I, vec3( 0.0 ) );
	}
#endif


// source chunk: cube_uv_reflection_fragment
#ifdef ENVMAP_TYPE_CUBE_UV
	#define cubeUV_minMipLevel 4.0
	#define cubeUV_minTileSize 16.0
	float getFace( vec3 direction ) {
		vec3 absDirection = abs( direction );
		float face = - 1.0;
		if ( absDirection.x > absDirection.z ) {
			if ( absDirection.x > absDirection.y )
				face = direction.x > 0.0 ? 0.0 : 3.0;
			else
				face = direction.y > 0.0 ? 1.0 : 4.0;
		} else {
			if ( absDirection.z > absDirection.y )
				face = direction.z > 0.0 ? 2.0 : 5.0;
			else
				face = direction.y > 0.0 ? 1.0 : 4.0;
		}
		return face;
	}
	vec2 getUV( vec3 direction, float face ) {
		vec2 uv;
		if ( face == 0.0 ) {
			uv = vec2( direction.z, direction.y ) / abs( direction.x );
		} else if ( face == 1.0 ) {
			uv = vec2( - direction.x, - direction.z ) / abs( direction.y );
		} else if ( face == 2.0 ) {
			uv = vec2( - direction.x, direction.y ) / abs( direction.z );
		} else if ( face == 3.0 ) {
			uv = vec2( - direction.z, direction.y ) / abs( direction.x );
		} else if ( face == 4.0 ) {
			uv = vec2( - direction.x, direction.z ) / abs( direction.y );
		} else {
			uv = vec2( direction.x, direction.y ) / abs( direction.z );
		}
		return 0.5 * ( uv + 1.0 );
	}
	vec3 bilinearCubeUV( sampler2D envMap, vec3 direction, float mipInt ) {
		float face = getFace( direction );
		float filterInt = max( cubeUV_minMipLevel - mipInt, 0.0 );
		mipInt = max( mipInt, cubeUV_minMipLevel );
		float faceSize = exp2( mipInt );
		highp vec2 uv = getUV( direction, face ) * ( faceSize - 2.0 ) + 1.0;
		if ( face > 2.0 ) {
			uv.y += faceSize;
			face -= 3.0;
		}
		uv.x += face * faceSize;
		uv.x += filterInt * 3.0 * cubeUV_minTileSize;
		uv.y += 4.0 * ( exp2( CUBEUV_MAX_MIP ) - faceSize );
		uv.x *= CUBEUV_TEXEL_WIDTH;
		uv.y *= CUBEUV_TEXEL_HEIGHT;
		#ifdef texture2DGradEXT
			return texture2DGradEXT( envMap, uv, vec2( 0.0 ), vec2( 0.0 ) ).rgb;
		#else
			return texture2D( envMap, uv ).rgb;
		#endif
	}
	#define cubeUV_r0 1.0
	#define cubeUV_m0 - 2.0
	#define cubeUV_r1 0.8
	#define cubeUV_m1 - 1.0
	#define cubeUV_r4 0.4
	#define cubeUV_m4 2.0
	#define cubeUV_r5 0.305
	#define cubeUV_m5 3.0
	#define cubeUV_r6 0.21
	#define cubeUV_m6 4.0
	float roughnessToMip( float roughness ) {
		float mip = 0.0;
		if ( roughness >= cubeUV_r1 ) {
			mip = ( cubeUV_r0 - roughness ) * ( cubeUV_m1 - cubeUV_m0 ) / ( cubeUV_r0 - cubeUV_r1 ) + cubeUV_m0;
		} else if ( roughness >= cubeUV_r4 ) {
			mip = ( cubeUV_r1 - roughness ) * ( cubeUV_m4 - cubeUV_m1 ) / ( cubeUV_r1 - cubeUV_r4 ) + cubeUV_m1;
		} else if ( roughness >= cubeUV_r5 ) {
			mip = ( cubeUV_r4 - roughness ) * ( cubeUV_m5 - cubeUV_m4 ) / ( cubeUV_r4 - cubeUV_r5 ) + cubeUV_m4;
		} else if ( roughness >= cubeUV_r6 ) {
			mip = ( cubeUV_r5 - roughness ) * ( cubeUV_m6 - cubeUV_m5 ) / ( cubeUV_r5 - cubeUV_r6 ) + cubeUV_m5;
		} else {
			mip = - 2.0 * log2( 1.16 * roughness );		}
		return mip;
	}
	vec4 textureCubeUV( sampler2D envMap, vec3 sampleDir, float roughness ) {
		float mip = clamp( roughnessToMip( roughness ), cubeUV_m0, CUBEUV_MAX_MIP );
		float mipF = fract( mip );
		float mipInt = floor( mip );
		vec3 color0 = bilinearCubeUV( envMap, sampleDir, mipInt );
		if ( mipF == 0.0 ) {
			return vec4( color0, 1.0 );
		} else {
			vec3 color1 = bilinearCubeUV( envMap, sampleDir, mipInt + 1.0 );
			return vec4( mix( color0, color1, mipF ), 1.0 );
		}
	}
#endif


// source chunk: envmap_common_pars_fragment
#ifdef USE_ENVMAP
	uniform float envMapIntensity;
	uniform mat3 envMapRotation;
	#ifdef ENVMAP_TYPE_CUBE
		uniform samplerCube envMap;
	#else
		uniform sampler2D envMap;
	#endif
#endif


// source chunk: envmap_physical_pars_fragment
#ifdef USE_ENVMAP
	vec3 getIBLIrradiance( const in vec3 normal ) {
		#ifdef ENVMAP_TYPE_CUBE_UV
			vec3 worldNormal = transformNormalByInverseViewMatrix( normal, viewMatrix );
			vec4 envMapColor = textureCubeUV( envMap, envMapRotation * worldNormal, 1.0 );
			return PI * envMapColor.rgb * envMapIntensity;
		#else
			return vec3( 0.0 );
		#endif
	}
	vec3 getIBLRadiance( const in vec3 viewDir, const in vec3 normal, const in float roughness ) {
		#ifdef ENVMAP_TYPE_CUBE_UV
			vec3 reflectVec = reflect( - viewDir, normal );
			reflectVec = normalize( mix( reflectVec, normal, pow4( roughness ) ) );
			reflectVec = transformDirectionByInverseViewMatrix( reflectVec, viewMatrix );
			vec4 envMapColor = textureCubeUV( envMap, envMapRotation * reflectVec, roughness );
			return envMapColor.rgb * envMapIntensity;
		#else
			return vec3( 0.0 );
		#endif
	}
	#ifdef USE_RETROREFLECTION
		vec3 getIBLRetroRadiance( const in vec3 viewDir, const in vec3 normal, const in float roughness ) {
			#ifdef ENVMAP_TYPE_CUBE_UV
				vec3 retroVec = normalize( mix( viewDir, normal, pow4( roughness ) ) );
				retroVec = transformDirectionByInverseViewMatrix( retroVec, viewMatrix );
				vec4 envMapColor = textureCubeUV( envMap, envMapRotation * retroVec, roughness );
				return envMapColor.rgb * envMapIntensity;
			#else
				return vec3( 0.0 );
			#endif
		}
	#endif
	#ifdef USE_ANISOTROPY
		vec3 getIBLAnisotropyRadiance( const in vec3 viewDir, const in vec3 normal, const in float roughness, const in vec3 bitangent, const in float anisotropy ) {
			#ifdef ENVMAP_TYPE_CUBE_UV
				vec3 bentNormal = cross( bitangent, viewDir );
				bentNormal = normalize( cross( bentNormal, bitangent ) );
				bentNormal = normalize( mix( bentNormal, normal, pow2( pow2( 1.0 - anisotropy * ( 1.0 - roughness ) ) ) ) );
				return getIBLRadiance( viewDir, bentNormal, roughness );
			#else
				return vec3( 0.0 );
			#endif
		}
		#ifdef USE_RETROREFLECTION
			vec3 getIBLAnisotropyRetroRadiance( const in vec3 viewDir, const in vec3 normal, const in float roughness, const in vec3 bitangent, const in float anisotropy ) {
				#ifdef ENVMAP_TYPE_CUBE_UV
					vec3 bentNormal = cross( bitangent, viewDir );
					bentNormal = normalize( cross( bentNormal, bitangent ) );
					bentNormal = normalize( mix( bentNormal, normal, pow2( pow2( 1.0 - anisotropy * ( 1.0 - roughness ) ) ) ) );
					return getIBLRetroRadiance( viewDir, bentNormal, roughness );
				#else
					return vec3( 0.0 );
				#endif
			}
		#endif
	#endif
#endif


// source chunk: fog_pars_fragment
#ifdef USE_FOG
	uniform vec3 fogColor;
	varying float vFogDepth;
	#ifdef FOG_EXP2
		uniform float fogDensity;
	#else
		uniform float fogNear;
		uniform float fogFar;
	#endif
#endif


// source chunk: lights_pars_begin
uniform bool receiveShadow;
uniform vec3 ambientLightColor;
#if defined( USE_LIGHT_PROBES )
	uniform vec3 lightProbe[ 9 ];
#endif
vec3 shGetIrradianceAt( in vec3 normal, in vec3 shCoefficients[ 9 ] ) {
	float x = normal.x, y = normal.y, z = normal.z;
	vec3 result = shCoefficients[ 0 ] * 0.886227;
	result += shCoefficients[ 1 ] * 2.0 * 0.511664 * y;
	result += shCoefficients[ 2 ] * 2.0 * 0.511664 * z;
	result += shCoefficients[ 3 ] * 2.0 * 0.511664 * x;
	result += shCoefficients[ 4 ] * 2.0 * 0.429043 * x * y;
	result += shCoefficients[ 5 ] * 2.0 * 0.429043 * y * z;
	result += shCoefficients[ 6 ] * ( 0.743125 * z * z - 0.247708 );
	result += shCoefficients[ 7 ] * 2.0 * 0.429043 * x * z;
	result += shCoefficients[ 8 ] * 0.429043 * ( x * x - y * y );
	return result;
}
vec3 getLightProbeIrradiance( const in vec3 lightProbe[ 9 ], const in vec3 normal ) {
	vec3 worldNormal = transformNormalByInverseViewMatrix( normal, viewMatrix );
	vec3 irradiance = shGetIrradianceAt( worldNormal, lightProbe );
	return irradiance;
}
vec3 getAmbientLightIrradiance( const in vec3 ambientLightColor ) {
	vec3 irradiance = ambientLightColor;
	return irradiance;
}
float getDistanceAttenuation( const in float lightDistance, const in float cutoffDistance, const in float decayExponent ) {
	float distanceFalloff = 1.0 / max( pow( lightDistance, decayExponent ), 0.01 );
	if ( cutoffDistance > 0.0 ) {
		distanceFalloff *= pow2( saturate( 1.0 - pow4( lightDistance / cutoffDistance ) ) );
	}
	return distanceFalloff;
}
float getSpotAttenuation( const in float coneCosine, const in float penumbraCosine, const in float angleCosine ) {
	return smoothstep( coneCosine, penumbraCosine, angleCosine );
}
#if 0 > 0
	struct SunLight {
		vec3 direction;
		vec3 color;
	};
	uniform SunLight sunLights[ 0 ];
	void getSunLightInfo( const in SunLight sunLight, out IncidentLight light ) {
		light.color = sunLight.color;
		light.direction = sunLight.direction;
		light.visible = true;
	}
#endif
#if 1 > 0
	struct DirectionalLight {
		vec3 direction;
		vec3 color;
	};
	uniform DirectionalLight directionalLights[ 1 ];
	void getDirectionalLightInfo( const in DirectionalLight directionalLight, out IncidentLight light ) {
		light.color = directionalLight.color;
		light.direction = directionalLight.direction;
		light.visible = true;
	}
#endif
#if 0 > 0
	struct PointLight {
		vec3 position;
		vec3 color;
		float distance;
		float decay;
	};
	uniform PointLight pointLights[ 0 ];
	void getPointLightInfo( const in PointLight pointLight, const in vec3 geometryPosition, out IncidentLight light ) {
		vec3 lVector = pointLight.position - geometryPosition;
		light.direction = normalize( lVector );
		float lightDistance = length( lVector );
		light.color = pointLight.color;
		light.color *= getDistanceAttenuation( lightDistance, pointLight.distance, pointLight.decay );
		light.visible = ( light.color != vec3( 0.0 ) );
	}
#endif
#if 0 > 0
	struct SpotLight {
		vec3 position;
		vec3 direction;
		vec3 color;
		float distance;
		float decay;
		float coneCos;
		float penumbraCos;
	};
	uniform SpotLight spotLights[ 0 ];
	void getSpotLightInfo( const in SpotLight spotLight, const in vec3 geometryPosition, out IncidentLight light ) {
		vec3 lVector = spotLight.position - geometryPosition;
		light.direction = normalize( lVector );
		float angleCos = dot( light.direction, spotLight.direction );
		float spotAttenuation = getSpotAttenuation( spotLight.coneCos, spotLight.penumbraCos, angleCos );
		if ( spotAttenuation > 0.0 ) {
			float lightDistance = length( lVector );
			light.color = spotLight.color * spotAttenuation;
			light.color *= getDistanceAttenuation( lightDistance, spotLight.distance, spotLight.decay );
			light.visible = ( light.color != vec3( 0.0 ) );
		} else {
			light.color = vec3( 0.0 );
			light.visible = false;
		}
	}
#endif
#if 0 > 0
	struct RectAreaLight {
		vec3 color;
		vec3 position;
		vec3 halfWidth;
		vec3 halfHeight;
	};
	uniform sampler2D ltc_1;	uniform sampler2D ltc_2;
	uniform RectAreaLight rectAreaLights[ 0 ];
#endif
#if 1 > 0
	struct HemisphereLight {
		vec3 direction;
		vec3 skyColor;
		vec3 groundColor;
	};
	uniform HemisphereLight hemisphereLights[ 1 ];
	vec3 getHemisphereLightIrradiance( const in HemisphereLight hemiLight, const in vec3 normal ) {
		float dotNL = dot( normal, hemiLight.direction );
		float hemiDiffuseWeight = 0.5 * dotNL + 0.5;
		vec3 irradiance = mix( hemiLight.groundColor, hemiLight.skyColor, hemiDiffuseWeight );
		return irradiance;
	}
#endif

// source chunk: lightprobes_pars_fragment
#ifdef USE_LIGHT_PROBES_GRID
uniform highp sampler3D probesSH;
uniform vec3 probesMin;
uniform vec3 probesMax;
uniform vec3 probesResolution;
vec3 getLightProbeGridIrradiance( vec3 worldPos, vec3 worldNormal ) {
	vec3 res = probesResolution;
	vec3 gridRange = probesMax - probesMin;
	vec3 resMinusOne = res - 1.0;
	vec3 probeSpacing = gridRange / resMinusOne;
	vec3 samplePos = worldPos + worldNormal * probeSpacing * 0.5;
	vec3 uvw = clamp( ( samplePos - probesMin ) / gridRange, 0.0, 1.0 );
	uvw = uvw * resMinusOne / res + 0.5 / res;
	float nz          = res.z;
	float paddedSlices = nz + 2.0;
	float atlasDepth  = 7.0 * paddedSlices;
	float uvZBase     = uvw.z * nz + 1.0;
	vec4 s0 = texture( probesSH, vec3( uvw.xy, ( uvZBase                       ) / atlasDepth ) );
	vec4 s1 = texture( probesSH, vec3( uvw.xy, ( uvZBase +       paddedSlices   ) / atlasDepth ) );
	vec4 s2 = texture( probesSH, vec3( uvw.xy, ( uvZBase + 2.0 * paddedSlices   ) / atlasDepth ) );
	vec4 s3 = texture( probesSH, vec3( uvw.xy, ( uvZBase + 3.0 * paddedSlices   ) / atlasDepth ) );
	vec4 s4 = texture( probesSH, vec3( uvw.xy, ( uvZBase + 4.0 * paddedSlices   ) / atlasDepth ) );
	vec4 s5 = texture( probesSH, vec3( uvw.xy, ( uvZBase + 5.0 * paddedSlices   ) / atlasDepth ) );
	vec4 s6 = texture( probesSH, vec3( uvw.xy, ( uvZBase + 6.0 * paddedSlices   ) / atlasDepth ) );
	vec3 c0 = s0.xyz;
	vec3 c1 = vec3( s0.w, s1.xy );
	vec3 c2 = vec3( s1.zw, s2.x );
	vec3 c3 = s2.yzw;
	vec3 c4 = s3.xyz;
	vec3 c5 = vec3( s3.w, s4.xy );
	vec3 c6 = vec3( s4.zw, s5.x );
	vec3 c7 = s5.yzw;
	vec3 c8 = s6.xyz;
	float x = worldNormal.x, y = worldNormal.y, z = worldNormal.z;
	vec3 result = c0 * 0.886227;
	result += c1 * 2.0 * 0.511664 * y;
	result += c2 * 2.0 * 0.511664 * z;
	result += c3 * 2.0 * 0.511664 * x;
	result += c4 * 2.0 * 0.429043 * x * y;
	result += c5 * 2.0 * 0.429043 * y * z;
	result += c6 * ( 0.743125 * z * z - 0.247708 );
	result += c7 * 2.0 * 0.429043 * x * z;
	result += c8 * 0.429043 * ( x * x - y * y );
	return max( result, vec3( 0.0 ) );
}
#endif



// source chunk: normal_pars_fragment
#ifndef FLAT_SHADED
	varying vec3 vNormal;
	#ifdef USE_TANGENT
		varying vec3 vTangent;
		varying vec3 vBitangent;
	#endif
#endif


// source chunk: lights_physical_pars_fragment
uniform sampler2D dfgLUT;
struct PhysicalMaterial {
	vec3 diffuseColor;
	vec3 diffuseContribution;
	vec3 specularColor;
	vec3 specularColorBlended;
	float roughness;
	float metalness;
	float specularF90;
	float dispersion;
	vec2 dfg;
	vec3 multiScatteringCompensation;
	#ifdef USE_RETROREFLECTION
		float retroreflectivity;
	#endif
	#ifdef USE_CLEARCOAT
		float clearcoat;
		float clearcoatRoughness;
		vec3 clearcoatF0;
		float clearcoatF90;
	#endif
	#ifdef USE_IRIDESCENCE
		float iridescence;
		float iridescenceIOR;
		float iridescenceThickness;
		vec3 iridescenceFresnel;
		vec3 iridescenceF0Dielectric;
		vec3 iridescenceF0Metallic;
	#endif
	#ifdef USE_SHEEN
		vec3 sheenColor;
		float sheenRoughness;
	#endif
	#ifdef IOR
		float ior;
	#endif
	#ifdef USE_TRANSMISSION
		float transmission;
		float transmissionAlpha;
		float thickness;
		float attenuationDistance;
		vec3 attenuationColor;
	#endif
	#ifdef USE_ANISOTROPY
		float anisotropy;
		float alphaT;
		vec3 anisotropyT;
		vec3 anisotropyB;
	#endif
};
vec3 clearcoatSpecularDirect = vec3( 0.0 );
vec3 clearcoatSpecularIndirect = vec3( 0.0 );
vec3 sheenSpecularDirect = vec3( 0.0 );
vec3 sheenSpecularIndirect = vec3(0.0 );
vec3 Schlick_to_F0( const in vec3 f, const in float f90, const in float dotVH ) {
    float x = clamp( 1.0 - dotVH, 0.0, 1.0 );
    float x2 = x * x;
    float x5 = clamp( x * x2 * x2, 0.0, 0.9999 );
    return ( f - vec3( f90 ) * x5 ) / ( 1.0 - x5 );
}
float V_GGX_SmithCorrelated( const in float alpha, const in float dotNL, const in float dotNV ) {
	float a2 = pow2( alpha );
	float gv = dotNL * sqrt( a2 + ( 1.0 - a2 ) * pow2( dotNV ) );
	float gl = dotNV * sqrt( a2 + ( 1.0 - a2 ) * pow2( dotNL ) );
	return 0.5 / max( gv + gl, EPSILON );
}
float D_GGX( const in float alpha, const in float dotNH ) {
	float a2 = pow2( alpha );
	float denom = pow2( dotNH ) * ( a2 - 1.0 ) + 1.0;
	return RECIPROCAL_PI * a2 / pow2( denom );
}
#ifdef USE_ANISOTROPY
	float V_GGX_SmithCorrelated_Anisotropic( const in float alphaT, const in float alphaB, const in float dotTV, const in float dotBV, const in float dotTL, const in float dotBL, const in float dotNV, const in float dotNL ) {
		float gv = dotNL * length( vec3( alphaT * dotTV, alphaB * dotBV, dotNV ) );
		float gl = dotNV * length( vec3( alphaT * dotTL, alphaB * dotBL, dotNL ) );
		return 0.5 / max( gv + gl, EPSILON );
	}
	float D_GGX_Anisotropic( const in float alphaT, const in float alphaB, const in float dotNH, const in float dotTH, const in float dotBH ) {
		float a2 = alphaT * alphaB;
		highp vec3 v = vec3( alphaB * dotTH, alphaT * dotBH, a2 * dotNH );
		highp float v2 = dot( v, v );
		float w2 = a2 / v2;
		return RECIPROCAL_PI * a2 * pow2 ( w2 );
	}
#endif
#ifdef USE_CLEARCOAT
	vec3 BRDF_GGX_Clearcoat( const in vec3 lightDir, const in vec3 viewDir, const in vec3 normal, const in PhysicalMaterial material) {
		vec3 f0 = material.clearcoatF0;
		float f90 = material.clearcoatF90;
		float roughness = material.clearcoatRoughness;
		float alpha = pow2( roughness );
		vec3 halfDir = normalize( lightDir + viewDir );
		float dotNL = saturate( dot( normal, lightDir ) );
		float dotNV = saturate( dot( normal, viewDir ) );
		float dotNH = saturate( dot( normal, halfDir ) );
		float dotVH = saturate( dot( viewDir, halfDir ) );
		vec3 F = F_Schlick( f0, f90, dotVH );
		float V = V_GGX_SmithCorrelated( alpha, dotNL, dotNV );
		float D = D_GGX( alpha, dotNH );
		return F * ( V * D );
	}
#endif
vec3 BRDF_GGX( const in vec3 lightDir, const in vec3 viewDir, const in vec3 normal, const in PhysicalMaterial material ) {
	vec3 f0 = material.specularColorBlended;
	float f90 = material.specularF90;
	float roughness = material.roughness;
	float alpha = pow2( roughness );
	vec3 halfDir = normalize( lightDir + viewDir );
	float dotNL = saturate( dot( normal, lightDir ) );
	float dotNV = saturate( dot( normal, viewDir ) );
	float dotNH = saturate( dot( normal, halfDir ) );
	float dotVH = saturate( dot( viewDir, halfDir ) );
	vec3 F = F_Schlick( f0, f90, dotVH );
	#ifdef USE_IRIDESCENCE
		F = mix( F, material.iridescenceFresnel, material.iridescence );
	#endif
	#ifdef USE_ANISOTROPY
		float dotTL = dot( material.anisotropyT, lightDir );
		float dotTV = dot( material.anisotropyT, viewDir );
		float dotTH = dot( material.anisotropyT, halfDir );
		float dotBL = dot( material.anisotropyB, lightDir );
		float dotBV = dot( material.anisotropyB, viewDir );
		float dotBH = dot( material.anisotropyB, halfDir );
		float V = V_GGX_SmithCorrelated_Anisotropic( material.alphaT, alpha, dotTV, dotBV, dotTL, dotBL, dotNV, dotNL );
		float D = D_GGX_Anisotropic( material.alphaT, alpha, dotNH, dotTH, dotBH );
	#else
		float V = V_GGX_SmithCorrelated( alpha, dotNL, dotNV );
		float D = D_GGX( alpha, dotNH );
	#endif
	return F * ( V * D );
}
vec2 LTC_Uv( const in vec3 N, const in vec3 V, const in float roughness ) {
	const float LUT_SIZE = 64.0;
	const float LUT_SCALE = ( LUT_SIZE - 1.0 ) / LUT_SIZE;
	const float LUT_BIAS = 0.5 / LUT_SIZE;
	float dotNV = saturate( dot( N, V ) );
	vec2 uv = vec2( roughness, sqrt( 1.0 - dotNV ) );
	uv = uv * LUT_SCALE + LUT_BIAS;
	return uv;
}
float LTC_ClippedSphereFormFactor( const in vec3 f ) {
	float l = length( f );
	return max( ( l * l + f.z ) / ( l + 1.0 ), 0.0 );
}
vec3 LTC_EdgeVectorFormFactor( const in vec3 v1, const in vec3 v2 ) {
	float x = dot( v1, v2 );
	float y = abs( x );
	float a = 0.8543985 + ( 0.4965155 + 0.0145206 * y ) * y;
	float b = 3.4175940 + ( 4.1616724 + y ) * y;
	float v = a / b;
	float theta_sintheta = ( x > 0.0 ) ? v : 0.5 * inversesqrt( max( 1.0 - x * x, 1e-7 ) ) - v;
	return cross( v1, v2 ) * theta_sintheta;
}
vec3 LTC_Evaluate( const in vec3 N, const in vec3 V, const in vec3 P, const in mat3 mInv, const in vec3 rectCoords[ 4 ] ) {
	vec3 v1 = rectCoords[ 1 ] - rectCoords[ 0 ];
	vec3 v2 = rectCoords[ 3 ] - rectCoords[ 0 ];
	vec3 lightNormal = cross( v1, v2 );
	if( dot( lightNormal, P - rectCoords[ 0 ] ) < 0.0 ) return vec3( 0.0 );
	vec3 T1, T2;
	T1 = normalize( V - N * dot( V, N ) );
	T2 = - cross( N, T1 );
	mat3 mat = mInv * transpose( mat3( T1, T2, N ) );
	vec3 coords[ 4 ];
	coords[ 0 ] = mat * ( rectCoords[ 0 ] - P );
	coords[ 1 ] = mat * ( rectCoords[ 1 ] - P );
	coords[ 2 ] = mat * ( rectCoords[ 2 ] - P );
	coords[ 3 ] = mat * ( rectCoords[ 3 ] - P );
	coords[ 0 ] = normalize( coords[ 0 ] );
	coords[ 1 ] = normalize( coords[ 1 ] );
	coords[ 2 ] = normalize( coords[ 2 ] );
	coords[ 3 ] = normalize( coords[ 3 ] );
	vec3 vectorFormFactor = vec3( 0.0 );
	vectorFormFactor += LTC_EdgeVectorFormFactor( coords[ 0 ], coords[ 1 ] );
	vectorFormFactor += LTC_EdgeVectorFormFactor( coords[ 1 ], coords[ 2 ] );
	vectorFormFactor += LTC_EdgeVectorFormFactor( coords[ 2 ], coords[ 3 ] );
	vectorFormFactor += LTC_EdgeVectorFormFactor( coords[ 3 ], coords[ 0 ] );
	float result = LTC_ClippedSphereFormFactor( vectorFormFactor );
	return vec3( result );
}
#if defined( USE_SHEEN )
float D_Charlie( float roughness, float dotNH ) {
	float alpha = pow2( roughness );
	float invAlpha = 1.0 / alpha;
	float cos2h = dotNH * dotNH;
	float sin2h = max( 1.0 - cos2h, 0.0078125 );
	return ( 2.0 + invAlpha ) * pow( sin2h, invAlpha * 0.5 ) / ( 2.0 * PI );
}
float V_Neubelt( float dotNV, float dotNL ) {
	return saturate( 1.0 / ( 4.0 * ( dotNL + dotNV - dotNL * dotNV ) ) );
}
vec3 BRDF_Sheen( const in vec3 lightDir, const in vec3 viewDir, const in vec3 normal, vec3 sheenColor, const in float sheenRoughness ) {
	vec3 halfDir = normalize( lightDir + viewDir );
	float dotNL = saturate( dot( normal, lightDir ) );
	float dotNV = saturate( dot( normal, viewDir ) );
	float dotNH = saturate( dot( normal, halfDir ) );
	float D = D_Charlie( sheenRoughness, dotNH );
	float V = V_Neubelt( dotNV, dotNL );
	return sheenColor * ( D * V );
}
#endif
float IBLSheenBRDF( const in vec3 normal, const in vec3 viewDir, const in float roughness ) {
	float dotNV = saturate( dot( normal, viewDir ) );
	float r2 = roughness * roughness;
	float rInv = 1.0 / ( roughness + 0.1 );
	float a = -1.9362 + 1.0678 * roughness + 0.4573 * r2 - 0.8469 * rInv;
	float b = -0.6014 + 0.5538 * roughness - 0.4670 * r2 - 0.1255 * rInv;
	float DG = exp( a * dotNV + b );
	return saturate( DG );
}
vec3 EnvironmentBRDF( const in vec3 normal, const in vec3 viewDir, const in vec3 specularColor, const in float specularF90, const in float roughness ) {
	float dotNV = saturate( dot( normal, viewDir ) );
	vec2 fab = texture2D( dfgLUT, vec2( roughness, dotNV ) ).rg;
	return specularColor * fab.x + specularF90 * fab.y;
}
#ifdef USE_IRIDESCENCE
void computeMultiscatteringIridescence( const in vec2 fab, const in vec3 specularColor, const in float specularF90, const in float iridescence, const in vec3 iridescenceF0, inout vec3 singleScatter, inout vec3 multiScatter ) {
#else
void computeMultiscattering( const in vec2 fab, const in vec3 specularColor, const in float specularF90, inout vec3 singleScatter, inout vec3 multiScatter ) {
#endif
	#ifdef USE_IRIDESCENCE
		vec3 Fr = mix( specularColor, iridescenceF0, iridescence );
	#else
		vec3 Fr = specularColor;
	#endif
	vec3 FssEss = Fr * fab.x + specularF90 * fab.y;
	float Ess = fab.x + fab.y;
	float Ems = 1.0 - Ess;
	vec3 Favg = Fr + ( 1.0 - Fr ) * 0.047619;	vec3 Fms = FssEss * Favg / ( 1.0 - Ems * Favg );
	singleScatter += FssEss;
	multiScatter += Fms * Ems;
}
#if 0 > 0
	void RE_Direct_RectArea_Physical( const in RectAreaLight rectAreaLight, const in vec3 geometryPosition, const in vec3 geometryNormal, const in vec3 geometryViewDir, const in vec3 geometryClearcoatNormal, const in PhysicalMaterial material, inout ReflectedLight reflectedLight ) {
		vec3 normal = geometryNormal;
		vec3 viewDir = geometryViewDir;
		vec3 position = geometryPosition;
		vec3 lightPos = rectAreaLight.position;
		vec3 halfWidth = rectAreaLight.halfWidth;
		vec3 halfHeight = rectAreaLight.halfHeight;
		vec3 lightColor = rectAreaLight.color;
		float roughness = material.roughness;
		vec3 rectCoords[ 4 ];
		rectCoords[ 0 ] = lightPos + halfWidth - halfHeight;		rectCoords[ 1 ] = lightPos - halfWidth - halfHeight;
		rectCoords[ 2 ] = lightPos - halfWidth + halfHeight;
		rectCoords[ 3 ] = lightPos + halfWidth + halfHeight;
		vec2 uv = LTC_Uv( normal, viewDir, roughness );
		vec4 t1 = texture2D( ltc_1, uv );
		vec4 t2 = texture2D( ltc_2, uv );
		mat3 mInv = mat3(
			vec3( t1.x, 0, t1.y ),
			vec3(    0, 1,    0 ),
			vec3( t1.z, 0, t1.w )
		);
		vec3 fresnel = ( material.specularColorBlended * t2.x + ( material.specularF90 - material.specularColorBlended ) * t2.y );
		reflectedLight.directSpecular += lightColor * fresnel * LTC_Evaluate( normal, viewDir, position, mInv, rectCoords );
		reflectedLight.directDiffuse += lightColor * material.diffuseContribution * LTC_Evaluate( normal, viewDir, position, mat3( 1.0 ), rectCoords );
		#ifdef USE_CLEARCOAT
			vec3 Ncc = geometryClearcoatNormal;
			vec2 uvClearcoat = LTC_Uv( Ncc, viewDir, material.clearcoatRoughness );
			vec4 t1Clearcoat = texture2D( ltc_1, uvClearcoat );
			vec4 t2Clearcoat = texture2D( ltc_2, uvClearcoat );
			mat3 mInvClearcoat = mat3(
				vec3( t1Clearcoat.x, 0, t1Clearcoat.y ),
				vec3(             0, 1,             0 ),
				vec3( t1Clearcoat.z, 0, t1Clearcoat.w )
			);
			vec3 fresnelClearcoat = material.clearcoatF0 * t2Clearcoat.x + ( material.clearcoatF90 - material.clearcoatF0 ) * t2Clearcoat.y;
			clearcoatSpecularDirect += lightColor * fresnelClearcoat * LTC_Evaluate( Ncc, viewDir, position, mInvClearcoat, rectCoords );
		#endif
	}
#endif
void RE_Direct_Physical( const in IncidentLight directLight, const in vec3 geometryPosition, const in vec3 geometryNormal, const in vec3 geometryViewDir, const in vec3 geometryClearcoatNormal, const in PhysicalMaterial material, inout ReflectedLight reflectedLight ) {
	float dotNL = saturate( dot( geometryNormal, directLight.direction ) );
	vec3 irradiance = dotNL * directLight.color;
	#ifdef USE_CLEARCOAT
		float dotNLcc = saturate( dot( geometryClearcoatNormal, directLight.direction ) );
		vec3 ccIrradiance = dotNLcc * directLight.color;
		clearcoatSpecularDirect += ccIrradiance * BRDF_GGX_Clearcoat( directLight.direction, geometryViewDir, geometryClearcoatNormal, material );
	#endif
	#ifdef USE_SHEEN
 
 		sheenSpecularDirect += irradiance * BRDF_Sheen( directLight.direction, geometryViewDir, geometryNormal, material.sheenColor, material.sheenRoughness );
 
 		float sheenAlbedoV = IBLSheenBRDF( geometryNormal, geometryViewDir, material.sheenRoughness );
 		float sheenAlbedoL = IBLSheenBRDF( geometryNormal, directLight.direction, material.sheenRoughness );
 
 		float sheenEnergyComp = 1.0 - max3( material.sheenColor ) * max( sheenAlbedoV, sheenAlbedoL );
 
 		irradiance *= sheenEnergyComp;
 
 	#endif
	vec3 specularBRDF = BRDF_GGX( directLight.direction, geometryViewDir, geometryNormal, material );
	#ifdef USE_RETROREFLECTION
		vec3 retroViewDir = reflect( - geometryViewDir, geometryNormal );
		vec3 retroSpecularBRDF = BRDF_GGX( directLight.direction, retroViewDir, geometryNormal, material );
		specularBRDF = mix( specularBRDF, retroSpecularBRDF, saturate( material.retroreflectivity ) );
	#endif
	reflectedLight.directSpecular += irradiance * specularBRDF * material.multiScatteringCompensation;
	vec3 halfDir = normalize( directLight.direction + geometryViewDir );
	float dotVH = saturate( dot( geometryViewDir, halfDir ) );
	vec3 F = F_Schlick( material.specularColor, material.specularF90, dotVH );
	#ifdef USE_RETROREFLECTION
		vec3 retroHalfDir = normalize( directLight.direction + retroViewDir );
		float dotRetroVH = saturate( dot( retroViewDir, retroHalfDir ) );
		vec3 retroF = F_Schlick( material.specularColor, material.specularF90, dotRetroVH );
		F = mix( F, retroF, saturate( material.retroreflectivity ) );
	#endif
	reflectedLight.directDiffuse += irradiance * BRDF_Lambert( material.diffuseContribution ) * ( 1.0 - F );
}
void RE_IndirectDiffuse_Physical( const in vec3 irradiance, const in vec3 geometryPosition, const in vec3 geometryNormal, const in vec3 geometryViewDir, const in vec3 geometryClearcoatNormal, const in PhysicalMaterial material, inout ReflectedLight reflectedLight ) {
	vec3 singleScattering = vec3( 0.0 );
	vec3 multiScattering = vec3( 0.0 );
	#ifdef USE_IRIDESCENCE
		computeMultiscatteringIridescence( material.dfg, material.specularColor, material.specularF90, material.iridescence, material.iridescenceF0Dielectric, singleScattering, multiScattering );
	#else
		computeMultiscattering( material.dfg, material.specularColor, material.specularF90, singleScattering, multiScattering );
	#endif
	vec3 diffuse = irradiance * BRDF_Lambert( material.diffuseContribution ) * ( 1.0 - singleScattering - multiScattering );
	#ifdef USE_SHEEN
		float sheenAlbedo = IBLSheenBRDF( geometryNormal, geometryViewDir, material.sheenRoughness );
		sheenSpecularIndirect += irradiance * material.sheenColor * sheenAlbedo * RECIPROCAL_PI;
		float sheenEnergyComp = 1.0 - max3( material.sheenColor ) * sheenAlbedo;
		diffuse *= sheenEnergyComp;
	#endif
	reflectedLight.indirectDiffuse += diffuse;
}
void RE_IndirectSpecular_Physical( const in vec3 radiance, const in vec3 irradiance, const in vec3 clearcoatRadiance, const in vec3 geometryPosition, const in vec3 geometryNormal, const in vec3 geometryViewDir, const in vec3 geometryClearcoatNormal, const in PhysicalMaterial material, inout ReflectedLight reflectedLight) {
	#ifdef USE_CLEARCOAT
		clearcoatSpecularIndirect += clearcoatRadiance * EnvironmentBRDF( geometryClearcoatNormal, geometryViewDir, material.clearcoatF0, material.clearcoatF90, material.clearcoatRoughness );
	#endif
	#ifdef USE_SHEEN
		sheenSpecularIndirect += irradiance * material.sheenColor * IBLSheenBRDF( geometryNormal, geometryViewDir, material.sheenRoughness ) * RECIPROCAL_PI;
 	#endif
	vec3 singleScatteringDielectric = vec3( 0.0 );
	vec3 multiScatteringDielectric = vec3( 0.0 );
	vec3 singleScatteringMetallic = vec3( 0.0 );
	vec3 multiScatteringMetallic = vec3( 0.0 );
	#ifdef USE_IRIDESCENCE
		computeMultiscatteringIridescence( material.dfg, material.specularColor, material.specularF90, material.iridescence, material.iridescenceF0Dielectric, singleScatteringDielectric, multiScatteringDielectric );
		computeMultiscatteringIridescence( material.dfg, material.diffuseColor, material.specularF90, material.iridescence, material.iridescenceF0Metallic, singleScatteringMetallic, multiScatteringMetallic );
	#else
		computeMultiscattering( material.dfg, material.specularColor, material.specularF90, singleScatteringDielectric, multiScatteringDielectric );
		computeMultiscattering( material.dfg, material.diffuseColor, material.specularF90, singleScatteringMetallic, multiScatteringMetallic );
	#endif
	vec3 singleScattering = mix( singleScatteringDielectric, singleScatteringMetallic, material.metalness );
	vec3 multiScattering = mix( multiScatteringDielectric, multiScatteringMetallic, material.metalness );
	vec3 totalScatteringDielectric = singleScatteringDielectric + multiScatteringDielectric;
	vec3 diffuse = material.diffuseContribution * ( 1.0 - totalScatteringDielectric );
	vec3 cosineWeightedIrradiance = irradiance * RECIPROCAL_PI;
	vec3 indirectSpecular = radiance * singleScattering;
	indirectSpecular += multiScattering * cosineWeightedIrradiance;
	vec3 indirectDiffuse = diffuse * cosineWeightedIrradiance;
	#ifdef USE_SHEEN
		float sheenAlbedo = IBLSheenBRDF( geometryNormal, geometryViewDir, material.sheenRoughness );
		float sheenEnergyComp = 1.0 - max3( material.sheenColor ) * sheenAlbedo;
		indirectSpecular *= sheenEnergyComp;
		indirectDiffuse *= sheenEnergyComp;
	#endif
	reflectedLight.indirectSpecular += indirectSpecular;
	reflectedLight.indirectDiffuse += indirectDiffuse;
}
#define RE_Direct				RE_Direct_Physical
#define RE_Direct_RectArea		RE_Direct_RectArea_Physical
#define RE_IndirectDiffuse		RE_IndirectDiffuse_Physical
#define RE_IndirectSpecular		RE_IndirectSpecular_Physical
float computeSpecularOcclusion( const in float dotNV, const in float ambientOcclusion, const in float roughness ) {
	return saturate( pow( dotNV + ambientOcclusion, exp2( - 16.0 * roughness - 1.0 ) ) - 1.0 + ambientOcclusion );
}


// source chunk: transmission_pars_fragment
#ifdef USE_TRANSMISSION
	uniform float transmission;
	uniform float thickness;
	uniform float attenuationDistance;
	uniform vec3 attenuationColor;
	#ifdef USE_TRANSMISSIONMAP
		uniform sampler2D transmissionMap;
	#endif
	#ifdef USE_THICKNESSMAP
		uniform sampler2D thicknessMap;
	#endif
	uniform vec2 transmissionSamplerSize;
	uniform sampler2D transmissionSamplerMap;
	uniform mat4 modelMatrix;
	uniform mat4 projectionMatrix;
	varying vec3 vWorldPosition;
	float w0( float a ) {
		return ( 1.0 / 6.0 ) * ( a * ( a * ( - a + 3.0 ) - 3.0 ) + 1.0 );
	}
	float w1( float a ) {
		return ( 1.0 / 6.0 ) * ( a *  a * ( 3.0 * a - 6.0 ) + 4.0 );
	}
	float w2( float a ){
		return ( 1.0 / 6.0 ) * ( a * ( a * ( - 3.0 * a + 3.0 ) + 3.0 ) + 1.0 );
	}
	float w3( float a ) {
		return ( 1.0 / 6.0 ) * ( a * a * a );
	}
	float g0( float a ) {
		return w0( a ) + w1( a );
	}
	float g1( float a ) {
		return w2( a ) + w3( a );
	}
	float h0( float a ) {
		return - 1.0 + w1( a ) / ( w0( a ) + w1( a ) );
	}
	float h1( float a ) {
		return 1.0 + w3( a ) / ( w2( a ) + w3( a ) );
	}
	vec4 bicubic( sampler2D tex, vec2 uv, vec4 texelSize, float lod ) {
		uv = uv * texelSize.zw + 0.5;
		vec2 iuv = floor( uv );
		vec2 fuv = fract( uv );
		float g0x = g0( fuv.x );
		float g1x = g1( fuv.x );
		float h0x = h0( fuv.x );
		float h1x = h1( fuv.x );
		float h0y = h0( fuv.y );
		float h1y = h1( fuv.y );
		vec2 p0 = ( vec2( iuv.x + h0x, iuv.y + h0y ) - 0.5 ) * texelSize.xy;
		vec2 p1 = ( vec2( iuv.x + h1x, iuv.y + h0y ) - 0.5 ) * texelSize.xy;
		vec2 p2 = ( vec2( iuv.x + h0x, iuv.y + h1y ) - 0.5 ) * texelSize.xy;
		vec2 p3 = ( vec2( iuv.x + h1x, iuv.y + h1y ) - 0.5 ) * texelSize.xy;
		return g0( fuv.y ) * ( g0x * textureLod( tex, p0, lod ) + g1x * textureLod( tex, p1, lod ) ) +
			g1( fuv.y ) * ( g0x * textureLod( tex, p2, lod ) + g1x * textureLod( tex, p3, lod ) );
	}
	vec4 textureBicubic( sampler2D sampler, vec2 uv, float lod ) {
		vec2 fLodSize = vec2( textureSize( sampler, int( lod ) ) );
		vec2 cLodSize = vec2( textureSize( sampler, int( lod + 1.0 ) ) );
		vec2 fLodSizeInv = 1.0 / fLodSize;
		vec2 cLodSizeInv = 1.0 / cLodSize;
		vec4 fSample = bicubic( sampler, uv, vec4( fLodSizeInv, fLodSize ), floor( lod ) );
		vec4 cSample = bicubic( sampler, uv, vec4( cLodSizeInv, cLodSize ), ceil( lod ) );
		return mix( fSample, cSample, fract( lod ) );
	}
	vec3 getVolumeTransmissionRay( const in vec3 n, const in vec3 v, const in float thickness, const in float ior, const in mat4 modelMatrix ) {
		vec3 refractionVector = refract( - v, normalize( n ), 1.0 / ior );
		vec3 modelScale;
		modelScale.x = length( vec3( modelMatrix[ 0 ].xyz ) );
		modelScale.y = length( vec3( modelMatrix[ 1 ].xyz ) );
		modelScale.z = length( vec3( modelMatrix[ 2 ].xyz ) );
		return normalize( refractionVector ) * thickness * modelScale;
	}
	float applyIorToRoughness( const in float roughness, const in float ior ) {
		return roughness * clamp( ior * 2.0 - 2.0, 0.0, 1.0 );
	}
	vec4 getTransmissionSample( const in vec2 fragCoord, const in float roughness, const in float ior ) {
		float lod = log2( transmissionSamplerSize.x ) * applyIorToRoughness( roughness, ior );
		return textureBicubic( transmissionSamplerMap, fragCoord.xy, lod );
	}
	vec3 volumeAttenuation( const in float transmissionDistance, const in vec3 attenuationColor, const in float attenuationDistance ) {
		if ( isinf( attenuationDistance ) ) {
			return vec3( 1.0 );
		} else {
			vec3 attenuationCoefficient = -log( attenuationColor ) / attenuationDistance;
			vec3 transmittance = exp( - attenuationCoefficient * transmissionDistance );			return transmittance;
		}
	}
	vec4 getIBLVolumeRefraction( const in vec3 n, const in vec3 v, const in float roughness, const in vec3 diffuseColor,
		const in vec3 specularColor, const in float specularF90, const in vec3 position, const in mat4 modelMatrix,
		const in mat4 viewMatrix, const in mat4 projMatrix, const in float dispersion, const in float ior, const in float thickness,
		const in vec3 attenuationColor, const in float attenuationDistance ) {
		vec4 transmittedLight;
		vec3 transmittance;
		#ifdef USE_DISPERSION
			float halfSpread = ( ior - 1.0 ) * 0.025 * dispersion;
			vec3 iors = vec3( ior - halfSpread, ior, ior + halfSpread );
			for ( int i = 0; i < 3; i ++ ) {
				vec3 transmissionRay = getVolumeTransmissionRay( n, v, thickness, iors[ i ], modelMatrix );
				vec3 refractedRayExit = position + transmissionRay;
				vec4 ndcPos = projMatrix * viewMatrix * vec4( refractedRayExit, 1.0 );
				vec2 refractionCoords = ndcPos.xy / ndcPos.w;
				refractionCoords += 1.0;
				refractionCoords /= 2.0;
				vec4 transmissionSample = getTransmissionSample( refractionCoords, roughness, iors[ i ] );
				transmittedLight[ i ] = transmissionSample[ i ];
				transmittedLight.a += transmissionSample.a;
				transmittance[ i ] = diffuseColor[ i ] * volumeAttenuation( length( transmissionRay ), attenuationColor, attenuationDistance )[ i ];
			}
			transmittedLight.a /= 3.0;
		#else
			vec3 transmissionRay = getVolumeTransmissionRay( n, v, thickness, ior, modelMatrix );
			vec3 refractedRayExit = position + transmissionRay;
			vec4 ndcPos = projMatrix * viewMatrix * vec4( refractedRayExit, 1.0 );
			vec2 refractionCoords = ndcPos.xy / ndcPos.w;
			refractionCoords += 1.0;
			refractionCoords /= 2.0;
			transmittedLight = getTransmissionSample( refractionCoords, roughness, ior );
			transmittance = diffuseColor * volumeAttenuation( length( transmissionRay ), attenuationColor, attenuationDistance );
		#endif
		vec3 attenuatedColor = transmittance * transmittedLight.rgb;
		vec3 F = EnvironmentBRDF( n, v, specularColor, specularF90, roughness );
		float transmittanceFactor = ( transmittance.r + transmittance.g + transmittance.b ) / 3.0;
		return vec4( ( 1.0 - F ) * attenuatedColor, 1.0 - ( 1.0 - transmittedLight.a ) * transmittanceFactor );
	}
#endif


// source chunk: shadowmap_pars_fragment
#if 0 > 0
	varying vec4 vSpotLightCoord[ 0 ];
#endif
#if 0 > 0
	uniform sampler2D spotLightMap[ 0 ];
#endif
#ifdef USE_SHADOWMAP
	#if 0 > 0
		#define SUN_LIGHT_CASCADES 2
		#if defined( SHADOWMAP_TYPE_PCF )
			uniform sampler2DShadow sunShadowMap[ 0 ];
		#else
			uniform sampler2D sunShadowMap[ 0 ];
		#endif
		uniform mat4 sunShadowMatrix[ 0 * SUN_LIGHT_CASCADES ];
		uniform vec4 sunShadowCascade[ 0 * SUN_LIGHT_CASCADES ];
		varying vec4 vSunShadowWorldPosition;
		varying vec3 vSunShadowWorldNormal;
		struct SunLightShadow {
			float shadowIntensity;
			float shadowBias;
			float shadowNormalBias;
			float shadowRadius;
			vec2 shadowMapSize;
		};
		uniform SunLightShadow sunLightShadows[ 0 ];
	#endif
	#if 0 > 0
		#if defined( SHADOWMAP_TYPE_PCF )
			uniform sampler2DShadow directionalShadowMap[ 0 ];
		#else
			uniform sampler2D directionalShadowMap[ 0 ];
		#endif
		varying vec4 vDirectionalShadowCoord[ 0 ];
		struct DirectionalLightShadow {
			float shadowIntensity;
			float shadowBias;
			float shadowNormalBias;
			float shadowRadius;
			vec2 shadowMapSize;
		};
		uniform DirectionalLightShadow directionalLightShadows[ 0 ];
	#endif
	#if 0 > 0
		#if defined( SHADOWMAP_TYPE_PCF )
			uniform sampler2DShadow spotShadowMap[ 0 ];
		#else
			uniform sampler2D spotShadowMap[ 0 ];
		#endif
		struct SpotLightShadow {
			float shadowIntensity;
			float shadowBias;
			float shadowNormalBias;
			float shadowRadius;
			vec2 shadowMapSize;
		};
		uniform SpotLightShadow spotLightShadows[ 0 ];
	#endif
	#if 0 > 0
		#if defined( SHADOWMAP_TYPE_PCF )
			uniform samplerCubeShadow pointShadowMap[ 0 ];
		#elif defined( SHADOWMAP_TYPE_BASIC )
			uniform samplerCube pointShadowMap[ 0 ];
		#endif
		varying vec4 vPointShadowCoord[ 0 ];
		struct PointLightShadow {
			float shadowIntensity;
			float shadowBias;
			float shadowNormalBias;
			float shadowRadius;
			vec2 shadowMapSize;
			float shadowCameraNear;
			float shadowCameraFar;
		};
		uniform PointLightShadow pointLightShadows[ 0 ];
	#endif
	#if defined( SHADOWMAP_TYPE_PCF )
		float interleavedGradientNoise( vec2 position ) {
			return fract( 52.9829189 * fract( dot( position, vec2( 0.06711056, 0.00583715 ) ) ) );
		}
		vec2 vogelDiskSample( int sampleIndex, int samplesCount, float phi ) {
			const float goldenAngle = 2.399963229728653;
			float r = sqrt( ( float( sampleIndex ) + 0.5 ) / float( samplesCount ) );
			float theta = float( sampleIndex ) * goldenAngle + phi;
			return vec2( cos( theta ), sin( theta ) ) * r;
		}
	#endif
	#if defined( SHADOWMAP_TYPE_PCF )
		float getShadow( sampler2DShadow shadowMap, vec2 shadowMapSize, float shadowIntensity, float shadowBias, float shadowRadius, vec4 shadowCoord ) {
			float shadow = 1.0;
			shadowCoord.xyz /= shadowCoord.w;
			shadowCoord.z += shadowBias;
			bool inFrustum = shadowCoord.x >= 0.0 && shadowCoord.x <= 1.0 && shadowCoord.y >= 0.0 && shadowCoord.y <= 1.0;
			bool frustumTest = inFrustum && shadowCoord.z <= 1.0;
			if ( frustumTest ) {
				vec2 texelSize = vec2( 1.0 ) / shadowMapSize;
				float radius = shadowRadius * texelSize.x;
				float phi = interleavedGradientNoise( gl_FragCoord.xy ) * PI2;
				shadow = (
					texture( shadowMap, vec3( shadowCoord.xy + vogelDiskSample( 0, 5, phi ) * radius, shadowCoord.z ) ) +
					texture( shadowMap, vec3( shadowCoord.xy + vogelDiskSample( 1, 5, phi ) * radius, shadowCoord.z ) ) +
					texture( shadowMap, vec3( shadowCoord.xy + vogelDiskSample( 2, 5, phi ) * radius, shadowCoord.z ) ) +
					texture( shadowMap, vec3( shadowCoord.xy + vogelDiskSample( 3, 5, phi ) * radius, shadowCoord.z ) ) +
					texture( shadowMap, vec3( shadowCoord.xy + vogelDiskSample( 4, 5, phi ) * radius, shadowCoord.z ) )
				) * 0.2;
			}
			return mix( 1.0, shadow, shadowIntensity );
		}
	#elif defined( SHADOWMAP_TYPE_VSM )
		float getShadow( sampler2D shadowMap, vec2 shadowMapSize, float shadowIntensity, float shadowBias, float shadowRadius, vec4 shadowCoord ) {
			float shadow = 1.0;
			shadowCoord.xyz /= shadowCoord.w;
			#ifdef USE_REVERSED_DEPTH_BUFFER
				shadowCoord.z -= shadowBias;
			#else
				shadowCoord.z += shadowBias;
			#endif
			bool inFrustum = shadowCoord.x >= 0.0 && shadowCoord.x <= 1.0 && shadowCoord.y >= 0.0 && shadowCoord.y <= 1.0;
			bool frustumTest = inFrustum && shadowCoord.z <= 1.0;
			if ( frustumTest ) {
				vec2 distribution = texture2D( shadowMap, shadowCoord.xy ).rg;
				float mean = distribution.x;
				float variance = distribution.y * distribution.y;
				#ifdef USE_REVERSED_DEPTH_BUFFER
					float hard_shadow = step( mean, shadowCoord.z );
				#else
					float hard_shadow = step( shadowCoord.z, mean );
				#endif
				
				if ( hard_shadow == 1.0 ) {
					shadow = 1.0;
				} else {
					variance = max( variance, 0.0000001 );
					float d = shadowCoord.z - mean;
					float p_max = variance / ( variance + d * d );
					p_max = clamp( ( p_max - 0.3 ) / 0.65, 0.0, 1.0 );
					shadow = max( hard_shadow, p_max );
				}
			}
			return mix( 1.0, shadow, shadowIntensity );
		}
	#else
		float getShadow( sampler2D shadowMap, vec2 shadowMapSize, float shadowIntensity, float shadowBias, float shadowRadius, vec4 shadowCoord ) {
			float shadow = 1.0;
			shadowCoord.xyz /= shadowCoord.w;
			#ifdef USE_REVERSED_DEPTH_BUFFER
				shadowCoord.z -= shadowBias;
			#else
				shadowCoord.z += shadowBias;
			#endif
			bool inFrustum = shadowCoord.x >= 0.0 && shadowCoord.x <= 1.0 && shadowCoord.y >= 0.0 && shadowCoord.y <= 1.0;
			bool frustumTest = inFrustum && shadowCoord.z <= 1.0;
			if ( frustumTest ) {
				float depth = texture2D( shadowMap, shadowCoord.xy ).r;
				#ifdef USE_REVERSED_DEPTH_BUFFER
					shadow = step( depth, shadowCoord.z );
				#else
					shadow = step( shadowCoord.z, depth );
				#endif
			}
			return mix( 1.0, shadow, shadowIntensity );
		}
	#endif
	#if 0 > 0
		float getSunShadow(
			#if defined( SHADOWMAP_TYPE_PCF )
				sampler2DShadow shadowMap,
			#else
				sampler2D shadowMap,
			#endif
			SunLightShadow sunLightShadow,
			int shadowIndex
		) {
			vec4 shadowWorldPosition = vec4( vSunShadowWorldPosition.xyz + vSunShadowWorldNormal * sunLightShadow.shadowNormalBias, 1.0 );
			float viewDepth = vSunShadowWorldPosition.w;
			int cascadeOffset = shadowIndex * SUN_LIGHT_CASCADES;
			float shadow = 1.0;
			for ( int i = SUN_LIGHT_CASCADES - 1; i >= 0; i -- ) {
				vec4 cascade = sunShadowCascade[ cascadeOffset + i ];
				if ( viewDepth >= cascade.x && viewDepth < cascade.y ) {
					float cascadeShadow = getShadow(
						shadowMap,
						sunLightShadow.shadowMapSize,
						sunLightShadow.shadowIntensity,
						sunLightShadow.shadowBias,
						sunLightShadow.shadowRadius,
						sunShadowMatrix[ cascadeOffset + i ] * shadowWorldPosition
					);
					shadow = mix( cascadeShadow, shadow, smoothstep( cascade.z, cascade.y, viewDepth ) );
				}
			}
			return shadow;
		}
	#endif
	#if 0 > 0
	#if defined( SHADOWMAP_TYPE_PCF )
	float getPointShadow( samplerCubeShadow shadowMap, vec2 shadowMapSize, float shadowIntensity, float shadowBias, float shadowRadius, vec4 shadowCoord, float shadowCameraNear, float shadowCameraFar ) {
		float shadow = 1.0;
		vec3 lightToPosition = shadowCoord.xyz;
		vec3 bd3D = normalize( lightToPosition );
		vec3 absVec = abs( lightToPosition );
		float viewSpaceZ = max( max( absVec.x, absVec.y ), absVec.z );
		if ( viewSpaceZ - shadowCameraFar <= 0.0 && viewSpaceZ - shadowCameraNear >= 0.0 ) {
			#ifdef USE_REVERSED_DEPTH_BUFFER
				float dp = ( shadowCameraNear * ( shadowCameraFar - viewSpaceZ ) ) / ( viewSpaceZ * ( shadowCameraFar - shadowCameraNear ) );
				dp -= shadowBias;
			#else
				float dp = ( shadowCameraFar * ( viewSpaceZ - shadowCameraNear ) ) / ( viewSpaceZ * ( shadowCameraFar - shadowCameraNear ) );
				dp += shadowBias;
			#endif
			float texelSize = shadowRadius / shadowMapSize.x;
			vec3 absDir = abs( bd3D );
			vec3 tangent = absDir.x > absDir.z ? vec3( 0.0, 1.0, 0.0 ) : vec3( 1.0, 0.0, 0.0 );
			tangent = normalize( cross( bd3D, tangent ) );
			vec3 bitangent = cross( bd3D, tangent );
			float phi = interleavedGradientNoise( gl_FragCoord.xy ) * PI2;
			vec2 sample0 = vogelDiskSample( 0, 5, phi );
			vec2 sample1 = vogelDiskSample( 1, 5, phi );
			vec2 sample2 = vogelDiskSample( 2, 5, phi );
			vec2 sample3 = vogelDiskSample( 3, 5, phi );
			vec2 sample4 = vogelDiskSample( 4, 5, phi );
			shadow = (
				texture( shadowMap, vec4( bd3D + ( tangent * sample0.x + bitangent * sample0.y ) * texelSize, dp ) ) +
				texture( shadowMap, vec4( bd3D + ( tangent * sample1.x + bitangent * sample1.y ) * texelSize, dp ) ) +
				texture( shadowMap, vec4( bd3D + ( tangent * sample2.x + bitangent * sample2.y ) * texelSize, dp ) ) +
				texture( shadowMap, vec4( bd3D + ( tangent * sample3.x + bitangent * sample3.y ) * texelSize, dp ) ) +
				texture( shadowMap, vec4( bd3D + ( tangent * sample4.x + bitangent * sample4.y ) * texelSize, dp ) )
			) * 0.2;
		}
		return mix( 1.0, shadow, shadowIntensity );
	}
	#elif defined( SHADOWMAP_TYPE_BASIC )
	float getPointShadow( samplerCube shadowMap, vec2 shadowMapSize, float shadowIntensity, float shadowBias, float shadowRadius, vec4 shadowCoord, float shadowCameraNear, float shadowCameraFar ) {
		float shadow = 1.0;
		vec3 lightToPosition = shadowCoord.xyz;
		vec3 absVec = abs( lightToPosition );
		float viewSpaceZ = max( max( absVec.x, absVec.y ), absVec.z );
		if ( viewSpaceZ - shadowCameraFar <= 0.0 && viewSpaceZ - shadowCameraNear >= 0.0 ) {
			float dp = ( shadowCameraFar * ( viewSpaceZ - shadowCameraNear ) ) / ( viewSpaceZ * ( shadowCameraFar - shadowCameraNear ) );
			dp += shadowBias;
			vec3 bd3D = normalize( lightToPosition );
			float depth = textureCube( shadowMap, bd3D ).r;
			#ifdef USE_REVERSED_DEPTH_BUFFER
				depth = 1.0 - depth;
			#endif
			shadow = step( dp, depth );
		}
		return mix( 1.0, shadow, shadowIntensity );
	}
	#endif
	#endif
#endif


// source chunk: bumpmap_pars_fragment
#ifdef USE_BUMPMAP
	uniform sampler2D bumpMap;
	uniform float bumpScale;
	vec2 dHdxy_fwd() {
		vec2 dSTdx = dFdx( vBumpMapUv );
		vec2 dSTdy = dFdy( vBumpMapUv );
		float Hll = bumpScale * texture2D( bumpMap, vBumpMapUv ).x;
		float dBx = bumpScale * texture2D( bumpMap, vBumpMapUv + dSTdx ).x - Hll;
		float dBy = bumpScale * texture2D( bumpMap, vBumpMapUv + dSTdy ).x - Hll;
		return vec2( dBx, dBy );
	}
	vec3 perturbNormalArb( vec3 surf_pos, vec3 surf_norm, vec2 dHdxy, float faceDirection ) {
		vec3 vSigmaX = normalize( dFdx( surf_pos.xyz ) );
		vec3 vSigmaY = normalize( dFdy( surf_pos.xyz ) );
		vec3 vN = surf_norm;
		vec3 R1 = cross( vSigmaY, vN );
		vec3 R2 = cross( vN, vSigmaX );
		float fDet = dot( vSigmaX, R1 ) * faceDirection;
		vec3 vGrad = sign( fDet ) * ( dHdxy.x * R1 + dHdxy.y * R2 );
		return normalize( abs( fDet ) * surf_norm - vGrad );
	}
#endif


// source chunk: normalmap_pars_fragment
#ifdef USE_NORMALMAP
	uniform sampler2D normalMap;
	uniform vec2 normalScale;
#endif
#ifdef USE_NORMALMAP_OBJECTSPACE
	uniform mat3 normalMatrix;
#endif
#if ! defined ( USE_TANGENT ) && ( defined ( USE_NORMALMAP_TANGENTSPACE ) || defined ( USE_CLEARCOAT_NORMALMAP ) || defined( USE_ANISOTROPY ) )
	mat3 getTangentFrame( vec3 eye_pos, vec3 surf_norm, vec2 uv ) {
		vec3 q0 = dFdx( eye_pos.xyz );
		vec3 q1 = dFdy( eye_pos.xyz );
		vec2 st0 = dFdx( uv.st );
		vec2 st1 = dFdy( uv.st );
		vec3 N = surf_norm;
		vec3 q1perp = cross( q1, N );
		vec3 q0perp = cross( N, q0 );
		vec3 T = q1perp * st0.x + q0perp * st1.x;
		vec3 B = q1perp * st0.y + q0perp * st1.y;
		float det = max( dot( T, T ), dot( B, B ) );
		float scale = ( det == 0.0 ) ? 0.0 : inversesqrt( det );
		return mat3( T * scale, B * scale, N );
	}
#endif


// source chunk: clearcoat_pars_fragment
#ifdef USE_CLEARCOATMAP
	uniform sampler2D clearcoatMap;
#endif
#ifdef USE_CLEARCOAT_NORMALMAP
	uniform sampler2D clearcoatNormalMap;
	uniform vec2 clearcoatNormalScale;
#endif
#ifdef USE_CLEARCOAT_ROUGHNESSMAP
	uniform sampler2D clearcoatRoughnessMap;
#endif


// source chunk: iridescence_pars_fragment
#ifdef USE_IRIDESCENCEMAP
	uniform sampler2D iridescenceMap;
#endif
#ifdef USE_IRIDESCENCE_THICKNESSMAP
	uniform sampler2D iridescenceThicknessMap;
#endif


// source chunk: roughnessmap_pars_fragment
#ifdef USE_ROUGHNESSMAP
	uniform sampler2D roughnessMap;
#endif


// source chunk: metalnessmap_pars_fragment
#ifdef USE_METALNESSMAP
	uniform sampler2D metalnessMap;
#endif


// source chunk: logdepthbuf_pars_fragment
#if defined( USE_LOGARITHMIC_DEPTH_BUFFER )
	uniform float logDepthBufFC;
	varying float vFragDepth;
	varying float vIsPerspective;
#endif


// source chunk: clipping_planes_pars_fragment
#if 0 > 0
	varying vec3 vClipPosition;
	uniform vec4 clippingPlanes[ 0 ];
#endif

void main() {
	vec4 diffuseColor = vec4( diffuse, opacity );
	
// source chunk: clipping_planes_fragment
#if 0 > 0
	vec4 plane;
	#ifdef ALPHA_TO_COVERAGE
		float distanceToPlane, distanceGradient;
		float clipOpacity = 1.0;
		
		#if 0 < 0
			float unionClipOpacity = 1.0;
			
			clipOpacity *= 1.0 - unionClipOpacity;
		#endif
		diffuseColor.a *= clipOpacity;
		if ( diffuseColor.a == 0.0 ) discard;
	#else
		
		#if 0 < 0
			bool clipped = true;
			
			if ( clipped ) discard;
		#endif
	#endif
#endif

	ReflectedLight reflectedLight = ReflectedLight( vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ) );
	vec3 totalEmissiveRadiance = emissive;
	
// source chunk: logdepthbuf_fragment
#if defined( USE_LOGARITHMIC_DEPTH_BUFFER )
	gl_FragDepth = vIsPerspective == 0.0 ? gl_FragCoord.z : log2( vFragDepth ) * logDepthBufFC * 0.5;
#endif

	
// source chunk: map_fragment
#ifdef USE_MAP
	vec4 sampledDiffuseColor = texture2D( map, vMapUv );
	#ifdef DECODE_VIDEO_TEXTURE
		sampledDiffuseColor = sRGBTransferEOTF( sampledDiffuseColor );
	#endif
	diffuseColor *= sampledDiffuseColor;
#endif

	
// source chunk: color_fragment
#if defined( USE_COLOR ) || defined( USE_COLOR_ALPHA )
	diffuseColor *= vColor;
#endif


        float iwH = 0.0; float iwAO = 1.0 - clamp(vOcc, 0.0, 0.95); float iwCls = floor(vCloth.y + 0.5); float iwPart = floor(vCloth.x + 0.5);
        float iwGloss = 0.0; float iwShine = 0.0; // extra clearcoat / sheen tweaks from decorations
        {
          float slot = floor(vEx + 0.5);
          vec3 p = vBindPos; vec2 uv = vIwUv;
          vec3 base = vec3(1.0);
          if (slot == 1.0) base = uTeam;
          else if (slot == 2.0) base = iwShirtCol(p, uv, iwPart);
          else if (slot == 3.0) base = uShorts;
          else if (slot == 4.0) base = uShoe;
          else if (slot == 5.0) base = uSock;
          else if (slot == 6.0) base = uSole;
          else if (slot == 7.0) base = uStrap;
          else if (slot == 8.0) base = iwContrast(uShoe);
          else if (slot == 9.0) base = mix(uSole * 0.3 + vec3(0.035), vec3(0.09, 0.09, 0.1), 0.35);
          else if (slot == 10.0) base = vec3(0.78, 0.8, 0.84);
          else if (slot == 11.0) base = uStrap * 0.55 + vec3(0.02);
          else if (slot == 12.0) base = mix(uSock, vec3(0.96), 0.6);
          else if (slot == 13.0) base = uTeam * 0.62;
          else if (slot == 14.0) base = iwTrimCol();

          // ---------------- part decorations ----------------
          if (iwPart == 1.0) { // TEE body: uv = (theta/2pi, y)
            float circ = 0.68;
            float th = uv.x;
            // side seams (x = +-90 deg) with twin stitching
            for (int s = 0; s < 2; s++) {
              float c = s == 0 ? 0.25 : 0.75;
              float dx = (th - c) * circ;
              float seam = iwStroke(dx, 0.0008);
              iwH -= 0.0005 * exp(-pow(dx / 0.0016, 2.0));
              float st = iwStroke(abs(dx) - 0.004, 0.0005) * iwDash(p.y, 0.0055, 0.6);
              base *= 1.0 - 0.12 * seam;
              base = mix(base, base * 0.8, st * iwLod(p.y, 0.0055));
              iwH += 0.00025 * st;
            }
            // double-needle hem stitching + hem fold
            float hy = p.y - vCloth.z;
            float st2 = (iwStroke(hy - 0.013, 0.0005) + iwStroke(hy - 0.019, 0.0005)) * iwDash(th * circ, 0.0055, 0.62);
            base = mix(base, base * 0.8, st2 * iwLod(th * circ, 0.0055));
            iwH += 0.0003 * st2 - 0.0006 * exp(-pow((hy - 0.024) / 0.002, 2.0));
            // fine creases riding on the sculpted folds (geometry carries the big ones): waist compression, underarm
            // crease fans, short vertical hem creases — per pixel, faded with footprint
            iwH += iwTeeCrease(p, th) * iwLod(p.y, 0.012);
            // chest emblem (pattern-dependent placement)
            if (p.z > 0.0) {
              vec2 q; float R; float on = 1.0;
              if (uPattern < 0.5) { q = vec2(p.x, p.y - 0.826); R = 0.038; }
              else if (uPattern < 1.5) { q = vec2(p.x, p.y - 0.927); R = 0.0135; }
              else if (uPattern < 2.5) { q = vec2(p.x, p.y - 0.818); R = 0.032; }
              else if (uPattern < 3.5) { q = vec2(p.x, p.y - 0.8); R = 0.028; }
              else if (uPattern < 4.5) { q = vec2(p.x, p.y - 0.927); R = 0.0135; }          // breton: small logo under the collar
              else if (uPattern < 5.5) { q = vec2(0.0); R = 1.0; on = 0.0; }                  // splatter: the splats are the graphic
              else if (uPattern < 6.5) { q = vec2(p.x - 0.05, p.y - 0.905); R = 0.0125; }     // camo: chest patch
              else if (uPattern < 7.5) { q = vec2(p.x, p.y - 0.862); R = 0.03; }              // dip-dye: emblem above the dye line
              else if (uPattern < 8.5) { q = vec2(0.0); R = 1.0; on = 0.0; }                  // jersey: number instead
              else { q = vec2(p.x - 0.048, p.y - 0.925); R = 0.0115; }                        // track: small chest logo
              if (on > 0.5) {
                vec2 e = iwEmblem(q, R);
                vec3 ink = uTeam;
                base = mix(base, ink, e.x);
                base = mix(base, uShirt * 1.02 + 0.02, e.y * e.x);
                iwH += 0.00018 * e.x;             // screen print sits slightly proud
                iwShine += 0.35 * e.x;            // plastisol print: a touch glossier
              }
              if (uPattern > 7.5 && uPattern < 8.5) {
                // jersey number (stable per character: derived from the per-character seed), team fill + dark outline
                float no = floor(fract(uHurtSeed * 0.6180339 + 0.137) * 98.0) + 1.0;
                float d1 = floor(no / 10.0), d0 = no - d1 * 10.0;
                vec2 nq = vec2(p.x, p.y - 0.8) / 0.029;
                float sd = d1 > 0.5 ? min(iwDigitSD(nq - vec2(-0.6, 0.0), int(d1)), iwDigitSD(nq - vec2(0.6, 0.0), int(d0))) : iwDigitSD(nq, int(d0));
                sd *= 0.029;
                float nf = iwFill(sd), no2 = iwFill(sd - 0.0034);
                base = mix(base, vec3(0.09, 0.1, 0.12), no2);
                base = mix(base, uTeam, nf);
                iwH += 0.00016 * no2; iwShine += 0.3 * no2;
              } else if (uPattern > 8.5) {
                // track top: centre-front zip — dark tape, metal teeth, pull tab at the collar
                float zx = abs(p.x);
                float tape = iwFill(zx - 0.0046) * step(p.y, 0.994);
                base = mix(base, uShirt * 0.45 + 0.01, tape);
                float teeth = iwFill(zx - 0.0021) * mix(0.6, step(0.5, fract(p.y / 0.0034)), iwLod(p.y, 0.0034)) * step(p.y, 0.992);
                base = mix(base, vec3(0.74, 0.76, 0.8), teeth);
                float pull = iwFill(iwSdBox(vec2(p.x - 0.0036, p.y - 0.968), vec2(0.0034, 0.0088), 0.0022));
                base = mix(base, vec3(0.8, 0.82, 0.86), pull);
                iwH += 0.00025 * (teeth + pull) - 0.0002 * tape;
                iwShine += teeth + pull;
              }
            }
            // lower-back print (visible around the tank from the gameplay camera)
            if (p.z < 0.0 && uPattern > 0.5 && uPattern < 3.5) {
              float band = iwBand(p.y, 0.735, 0.748) * step(0.06, abs(p.x));
              base = mix(base, uTeam, band * 0.9);
            }
            // tank-strap pressure shading on the shoulders (strap sits on top)
            iwAO *= 1.0 - 0.18 * exp(-pow((abs(p.x) - 0.075) / 0.018, 2.0)) * smoothstep(0.9, 0.98, p.y);
          } else if (iwPart == 2.0) { // SLEEVE: uv = (theta, s along from shoulder)
            float s = uv.y, L = vCloth.z;
            float cuff = smoothstep(L - 0.02, L - 0.018, s);
            base = mix(base, slot == 2.0 ? iwCuffCol() : base, cuff);
            float circ = 0.3;
            float st = iwStroke(s - (L - 0.024), 0.0005) * iwDash(uv.x * circ, 0.005, 0.6);
            float st2 = iwStroke(s - 0.012, 0.0005) * iwDash(uv.x * circ, 0.005, 0.6);
            base = mix(base, base * 0.78, (st + st2) * iwLod(uv.x * circ, 0.005));
            iwH += 0.0003 * (st + st2);
            // rib on the cuff
            float rib = sin(uv.x * 6.2831 * 70.0);
            iwH += cuff * 0.00035 * rib * iwLod(uv.x * circ, circ / 70.0);
            // crease folds at the armpit side
            iwH += 0.0012 * sin(uv.x * 6.2831 * 3.0 + s * 60.0) * smoothstep(0.03, 0.0, s);
          } else if (iwPart == 3.0) { // COLLAR rib: uv = (theta, across)
            float rib = sin(uv.x * 6.2831 * 90.0);
            iwH += 0.0004 * rib * iwLod(uv.x * 0.3, 0.3 / 90.0);
            iwAO *= mix(0.8, 1.0, smoothstep(0.0, 0.35, uv.y));
          } else if (iwPart == 4.0 || iwPart == 5.0) { // SHORTS: uv = (theta, y | s)
            float circ = iwPart == 4.0 ? 0.66 : 0.46;
            float th = uv.x;
            // side seams + a team side stripe on some outfits
            float dx0 = (th - 0.25) * circ, dx1 = (th - 0.75) * circ;
            float dxs = iwPart == 4.0 ? min(abs(dx0), abs(dx1)) : abs(th - 0.5) * circ;   // legs: uv.x 0 = inseam, 0.5 = outer seam
            iwH -= 0.0005 * exp(-pow(dxs / 0.0016, 2.0));
            float stripe = 0.0; vec3 stripeC = uTeam;
            if (uPattern > 0.5 && uPattern < 2.5) stripe = iwStroke(dxs, 0.0065);
            else if (uPattern > 7.5 && uPattern < 8.5) stripe = iwStroke(dxs, 0.011);                                   // jersey: wide team panel
            else if (uPattern > 8.5) { stripe = iwStroke(dxs - 0.0075, 0.0026); stripeC = vec3(0.95); }                // track: twin white stripes
            base = mix(base, stripeC, stripe);
            float ss = iwStroke(dxs - 0.0045, 0.00045) * iwDash(p.y, 0.005, 0.6);
            base = mix(base, base * 0.75 + 0.02, ss * iwLod(p.y, 0.005));
            if (iwPart == 4.0) {
              vec2 f = vec2(p.x, p.y);
              // front: fly J-stitch + slant pocket openings
              if (p.z > 0.0) {
                float fly = iwSdSeg(f, vec2(0.012, 0.745), vec2(0.012, 0.64));
                fly = min(fly, iwSdSeg(f, vec2(0.012, 0.64), vec2(0.0, 0.625)));
                float fs = iwStroke(fly, 0.0005) * iwDash(p.y + p.x, 0.005, 0.6);
                base = mix(base, base * 0.72 + 0.02, fs * iwLod(p.y, 0.005));
                iwH -= 0.0006 * exp(-pow(abs(p.x) / 0.0018, 2.0)) * step(0.63, p.y);
                for (int s = -1; s <= 1; s += 2) {
                  float sx = float(s);
                  float pk = iwSdSeg(f, vec2(sx * 0.062, 0.752), vec2(sx * 0.112, 0.668));
                  iwH -= 0.0009 * exp(-pow(pk / 0.0018, 2.0));
                  float ps = iwStroke(pk - 0.004, 0.00045) * iwDash(p.y, 0.005, 0.6);
                  base = mix(base, base * 0.72 + 0.02, ps * iwLod(p.y, 0.005));
                  iwAO *= 1.0 - 0.25 * exp(-pow(pk / 0.003, 2.0));
                }
              } else {
                // back patch pockets with a little team label on the right one
                for (int s = -1; s <= 1; s += 2) {
                  float sx = float(s);
                  float pb = iwSdBox(f - vec2(sx * 0.058, 0.668), vec2(0.036, 0.04), 0.01);
                  iwH += 0.0008 * (1.0 - smoothstep(-0.002, 0.0015, pb));
                  float ps = (iwStroke(pb + 0.0035, 0.00045)) * iwDash(p.x + p.y, 0.005, 0.6);
                  base = mix(base, base * 0.72 + 0.02, ps * iwLod(p.y, 0.005));
                  iwAO *= 1.0 - 0.3 * exp(-pow(pb / 0.0022, 2.0)) * step(0.0, pb);
                  if (s < 0) { float lab = iwSdBox(f - vec2(sx * 0.03, 0.692), vec2(0.006, 0.012), 0.002); base = mix(base, uTeam, iwFill(lab)); }
                }
                // seat seam
                iwH -= 0.0006 * exp(-pow(p.x / 0.0017, 2.0));
              }
              // waistband stitch (mostly under the tee hem)
              float ws = iwStroke(p.y - 0.735, 0.0005) * iwDash(th * circ, 0.005, 0.6);
              base = mix(base, base * 0.75, ws);
            } else {
              // leg: inseam
              float dxi = min(th, 1.0 - th) * circ;
              iwH -= 0.0004 * exp(-pow(dxi / 0.0015, 2.0));
              // hem stitch above the cuff
              float L = vCloth.z;
              float hs = iwStroke(uv.y - (L - 0.02), 0.00045) * iwDash(th * circ, 0.005, 0.6);
              base = mix(base, base * 0.72 + 0.02, hs * iwLod(th * circ, 0.005));
              // relaxed folds
              iwH += 0.0013 * sin(th * 6.2831 * 5.0 + uv.y * 30.0) * smoothstep(0.02, 0.1, uv.y);
            }
          } else if (iwPart == 6.0) { // SOCK: uv = (theta, s from top)
            float rib = sin(uv.x * 6.2831 * 56.0);
            iwH += 0.00045 * rib * iwLod(uv.x * 0.22, 0.22 / 56.0);
            float s = uv.y;
            float stripes = iwBand(s, 0.018, 0.026) + iwBand(s, 0.033, 0.041);
            base = mix(base, uTeam, stripes);
            iwH += 0.0012 * sin(s * 190.0 + uv.x * 18.0) * smoothstep(0.05, 0.1, s) * smoothstep(0.14, 0.1, s); // scrunch
          } else if (iwPart == 7.0) { // SHOE UPPER (base layer): uv = (phi/2pi from the toe, t sole->collar, > 1 = lining)
            float phi = uv.x, t = uv.y;
            float front = cos(phi * 6.2831);
            // perforated toe box (vamp top, between the mudguard and the throat)
            float perfZ = smoothstep(0.72, 0.82, front) * smoothstep(0.47, 0.51, t) * smoothstep(0.62, 0.58, t);
            vec2 pp = vec2(p.x, p.z) * 260.0; pp.x += 0.5 * step(0.5, fract(pp.y * 0.5)); vec2 pc = fract(pp) - 0.5;
            float perf = iwFill(length(pc) - 0.16) * perfZ * iwLod(p.z * 260.0, 1.0) * iwLod(p.x * 260.0, 1.0);
            iwAO *= 1.0 - 0.55 * perf; iwH -= 0.00018 * perf;
            // fine pebbled grain, faded with pixel footprint (no shimmer)
            iwH += 0.00005 * (iwNoise(p * 1400.0) - 0.5) * iwLod(p.z * 1400.0, 2.0);
            iwAO *= mix(0.7, 1.0, smoothstep(0.0, 0.14, t));   // contact darkening at the sole line
            if (t > 1.001) iwAO *= 0.55;                       // lining inside the collar
          } else if (iwPart == 20.0) { // SHOE OVERLAYS (vCloth.z: 0 mudguard, 1 heel counter, 2 eyestay, 3 side panel); uv = patch (u, v)
            float ov = floor(vCloth.z + 0.5);
            vec2 q = uv;
            // twin-needle stitching inset from the free edges
            float st = 0.0;
            if (ov < 0.5) st = (iwStroke(q.y - 0.86, 0.018) + iwStroke(q.y - 0.79, 0.018)) * iwDash(q.x * 0.14, 0.0035, 0.6);
            else if (ov < 1.5) st = iwStroke(q.y - 0.88, 0.02) * iwDash(q.x * 0.12, 0.0035, 0.6);
            else if (ov < 2.5) st = (iwStroke(q.x - 0.84, 0.03) + iwStroke(q.x - 0.15, 0.03)) * iwDash(q.y * 0.06, 0.0035, 0.6);
            else st = (iwStroke(q.y - 0.8, 0.045) + iwStroke(q.y - 0.2, 0.045)) * iwDash(q.x * 0.1, 0.0035, 0.6) * step(0.06, q.x) * step(q.x, 0.97);
            st = clamp(st, 0.0, 1.0) * step(0.05, q.y) * iwLod(q.x * 0.1, 0.0035);
            base = mix(base, base * 0.62 + 0.04, st * 0.9);
            iwH += 0.00018 * st;
            iwH += 0.00004 * (iwNoise(p * 1600.0) - 0.5) * iwLod(p.z * 1600.0, 2.0);
            if (ov > 2.5) iwShine += 0.45;                     // glossy synthetic side panel
          } else if (iwPart == 8.0) { // MIDSOLE / cupsole wall: uv = (phi, h 0..1)
            float phi = uv.x, h = uv.y;
            float front = cos(phi * 6.2831), side = sin(phi * 6.2831);
            // thread in the sculpted stitch channel (h = 0.64)
            float st = iwStroke(h - 0.64, 0.035) * iwDash(phi * 0.62, 0.0042, 0.62) * iwLod(phi * 0.62, 0.0042);
            base = mix(base, base * 0.62 + vec3(0.02), st);
            iwH += 0.00018 * st;
            // team pinstripe near the top edge
            float pin = iwStroke(h - 0.82, 0.035) * step(abs(front), 0.96);
            base = mix(base, uTeam, pin * 0.95);
            // fine horizontal sidewall ribs (cupsole texture) below the channel
            iwH += 0.00016 * sin(h * 6.2831 * 9.0) * smoothstep(0.08, 0.2, h) * smoothstep(0.58, 0.5, h);
            // heel window (team) on both sides
            float win = iwSdBox(vec2(front + 0.64, h - 0.34), vec2(0.16, 0.14), 0.08);
            float wm = iwFill(win * 0.05) * step(0.3, abs(side));
            base = mix(base, uTeam * 0.85, wm);
            iwH -= 0.0005 * wm; iwShine += wm;
            // foam micro texture + grime near the ground and scuffs on the toe
            iwH += 0.00008 * (iwNoise(p * 700.0) - 0.5) * iwLod(p.z * 700.0, 1.0);
            float grime = (1.0 - smoothstep(0.0, 0.35, h)) * (0.55 + 0.45 * iwFbm(p * 90.0));
            float scuff = smoothstep(0.62, 0.8, iwFbm(p * 140.0 + 3.0)) * smoothstep(0.3, 0.9, front);
            base *= 1.0 - 0.13 * grime - 0.1 * scuff;
          } else if (iwPart == 9.0) { // OUTSOLE: uv = foot-local (x, z)
            vec2 f = uv;
            float edgeZone = step(0.5, vCloth.z);
            // herringbone traction pattern on the bottom
            float herr = abs(fract(f.y * 55.0 + abs(f.x) * 22.0) - 0.5);
            float lug = smoothstep(0.18, 0.26, herr);
            iwH += 0.0012 * lug * (1.0 - edgeZone) * iwLod(f.y * 55.0, 1.0);
            iwAO *= mix(0.6, 1.0, mix(lug, 1.0, edgeZone));
            // worn rubber: glazed at the toe + heel strike, dust on the wall
            float wear = smoothstep(0.05, 0.11, f.y) + smoothstep(-0.03, -0.07, f.y);
            base = mix(base, base * 1.35 + 0.04, 0.35 * wear * (1.0 - edgeZone) * lug);
            base = mix(base, base * 0.8 + vec3(0.03, 0.028, 0.025), edgeZone * (0.4 + 0.6 * iwFbm(p * 120.0)) * 0.5);
          } else if (iwPart == 10.0) { // STRAP webbing: uv = (along m, across -1..1)
            float edge = abs(uv.y);
            float st = iwStroke(edge - 0.72, 0.035) * iwDash(uv.x, 0.005, 0.62);
            base = mix(base, base * 0.6 + 0.06, st * iwLod(uv.x, 0.005));
            iwH += 0.00025 * st;
            iwAO *= mix(1.0, 0.8, smoothstep(0.8, 1.0, edge));
          } else if (iwPart == 11.0) { // TONGUE: uv = (across -1..1, along 0..1); team tab + logo at the top
            float tab = smoothstep(0.78, 0.8, uv.y);
            base = mix(base, uTeam, tab);
            vec2 e = iwEmblem(vec2(uv.x * 0.018, (uv.y - 0.9) * 0.02), 0.0065);
            base = mix(base, vec3(0.97), e.x * tab);
            float st = iwStroke(abs(uv.x) - 0.82, 0.03) * iwDash(uv.y * 0.05, 0.004, 0.6);
            base = mix(base, base * 0.7, st);
            vec2 mp = vec2(uv.x * 0.02, uv.y * 0.05) * 700.0; // mesh
            float mh = iwFill(length(fract(mp) - 0.5) - 0.2) * (1.0 - tab) * iwLod(uv.y * 0.05 * 700.0, 1.0);
            iwAO *= 1.0 - 0.35 * mh;
          } else if (iwPart == 12.0) { // TANK CAP: uv = (theta, h); knurled grip band
            float kn = sin(uv.x * 6.2831 * 48.0);
            float band = step(0.18, uv.y) * step(uv.y, 0.82);
            iwH += 0.0005 * kn * band * iwLod(uv.x * 0.42, 0.42 / 48.0);
          } else if (iwPart == 13.0) { // GAUGE face: uv = polar (angle 0..1, r 0..1)
            float a = uv.x, r = uv.y;
            vec3 face = vec3(0.95, 0.94, 0.9);
            float ticks = iwStroke(fract(a * 24.0) - 0.5, 0.06) * step(0.72, r) * step(r, 0.9) * step(0.1, a) * step(a, 0.9);
            float red = step(0.72, r) * step(r, 0.9) * step(0.78, a) * step(a, 0.9);
            face = mix(face, vec3(0.1), ticks);
            face = mix(face, vec3(0.85, 0.15, 0.12), red);
            base = mix(face, vec3(0.05), smoothstep(0.93, 0.97, r));
          } else if (iwPart == 15.0) { // shorts CUFF: rolled hem, rib-less twill + stitch
            float st = iwStroke(uv.y - 0.5, 0.05) * iwDash(uv.x * 0.46, 0.005, 0.6);
            base = mix(base, base * 0.75, st);
          } else if (iwPart == 17.0) { // COLLAR PAD: soft quilting lines
            iwH += 0.0005 * sin(uv.x * 6.2831 * 14.0);
          } else if (iwPart == 18.0) { // PLATE: quilted back pad
            vec2 q = vec2(uv.x, uv.y) * 38.0;
            float quilt = iwStroke(fract(q.x + q.y) - 0.5, 0.03) + iwStroke(fract(q.x - q.y) - 0.5, 0.03);
            iwH -= 0.0004 * quilt; iwAO *= 1.0 - 0.18 * quilt;
          } else if (iwPart == 16.0) { // drawcord: twisted
            iwH += 0.0003 * sin(uv.x * 900.0 + uv.y * 6.2831 * 3.0);
          }

          // ---------------- material-class micro detail ----------------
          if (iwCls == 0.0) {        // jersey knit: columns of tiny V loops (~1.1 mm wales), gone before it can shimmer
            float wale = (atan(p.x, p.z + 0.012) * 0.105 + p.x * 0.0) * 900.0, course = p.y * 1150.0;
            float v = abs(fract(wale) - 0.5) * 2.0;
            float k = sin((course + v * 0.9) * 6.2831);
            iwH += 0.000028 * k * iwLod(course, 1.0) * iwLod(wale, 1.0);
          } else if (iwCls == 1.0) { // twill: fine 45-degree wales (~1 mm) over a slubby yarn, faded with footprint
            float w = (p.y * 0.8 + (p.x + p.z) * 0.6) * 6400.0;
            float k = sin(w + 1.3 * iwNoise(p * 420.0));
            iwH += 0.000035 * k * iwLod(w, 6.2831);
            iwH += 0.00004 * (iwNoise(p * 260.0) - 0.5) * iwLod(p.y * 260.0, 1.0);
          } else if (iwCls == 8.0) { // webbing: cross ribs
            float k = sin(uv.x * 2600.0);
            iwH += 0.00004 * k * iwLod(uv.x * 2600.0, 6.2831);
          } else if (iwCls == 10.0) { // lace braid
            iwH += 0.00012 * sin(uv.x * 1800.0) * iwLod(uv.x * 1800.0, 6.2831);
          }
          diffuseColor.rgb *= base;
        }
      
  float iwHurtM = 0.0;
  if (uHurt.w > 0.002) {
    vec3 hp = vBindPos * vec3(10.0, 6.5, 10.0) + vec3(uHurtSeed);
    hp.y += 0.35 * iwNoise(vBindPos * 23.0);
    float n = iwFbm(hp);
    float th = mix(1.0, 0.47, clamp(uHurt.w, 0.0, 1.0));
    float w = max(fwidth(n) * 0.9, 0.004);
    iwHurtM = smoothstep(th - w, th + w, n);
    float rim = smoothstep(th - 0.05, th - w, n) * (1.0 - iwHurtM);
    diffuseColor.rgb = mix(diffuseColor.rgb, diffuseColor.rgb * 0.8, rim * 0.6);
    diffuseColor.rgb = mix(diffuseColor.rgb, uHurt.rgb, iwHurtM);
  }

	
// source chunk: alphamap_fragment
#ifdef USE_ALPHAMAP
	diffuseColor.a *= texture2D( alphaMap, vAlphaMapUv ).g;
#endif

	
// source chunk: alphatest_fragment
#ifdef USE_ALPHATEST
	#ifdef ALPHA_TO_COVERAGE
	diffuseColor.a = smoothstep( alphaTest, alphaTest + fwidth( diffuseColor.a ), diffuseColor.a );
	if ( diffuseColor.a == 0.0 ) discard;
	#else
	if ( diffuseColor.a < alphaTest ) discard;
	#endif
#endif

	
// source chunk: alphahash_fragment
#ifdef USE_ALPHAHASH
	if ( diffuseColor.a < getAlphaHashThreshold( vPosition ) ) discard;
#endif

	
// source chunk: roughnessmap_fragment
float roughnessFactor = roughness;
#ifdef USE_ROUGHNESSMAP
	vec4 texelRoughness = texture2D( roughnessMap, vRoughnessMapUv );
	roughnessFactor *= texelRoughness.g;
#endif


        {
          float r = 0.84;
          if (iwCls == 1.0) r = 0.8; else if (iwCls == 2.0) r = 0.86; else if (iwCls == 3.0) r = 0.46; else if (iwCls == 4.0) r = 0.62;
          else if (iwCls == 5.0) r = 0.86; else if (iwCls == 6.0) r = 0.3; else if (iwCls == 7.0) r = 0.28; else if (iwCls == 8.0) r = 0.7;
          else if (iwCls == 9.0) r = 0.74; else if (iwCls == 10.0) r = 0.8; else if (iwCls == 11.0) r = 0.78; else if (iwCls == 12.0) r = 0.5;
          roughnessFactor = mix(r, r * 0.55, clamp(iwShine, 0.0, 1.0));
          roughnessFactor = mix(roughnessFactor, 0.16, iwHurtM);
        }
      
	
// source chunk: metalnessmap_fragment
float metalnessFactor = metalness;
#ifdef USE_METALNESSMAP
	vec4 texelMetalness = texture2D( metalnessMap, vMetalnessMapUv );
	metalnessFactor *= texelMetalness.b;
#endif

metalnessFactor = iwCls == 7.0 ? 1.0 : 0.0;
	
// source chunk: normal_fragment_begin
float faceDirection = gl_FrontFacing ? 1.0 : - 1.0;
#ifdef FLAT_SHADED
	vec3 fdx = dFdx( vViewPosition );
	vec3 fdy = dFdy( vViewPosition );
	vec3 normal = normalize( cross( fdx, fdy ) );
#else
	vec3 normal = normalize( vNormal );
	#ifdef DOUBLE_SIDED
		normal *= faceDirection;
	#endif
#endif
#if defined( USE_NORMALMAP_TANGENTSPACE ) || defined( USE_CLEARCOAT_NORMALMAP ) || defined( USE_ANISOTROPY )
	#ifdef USE_TANGENT
		mat3 tbn = mat3( normalize( vTangent ), normalize( vBitangent ), normal );
	#else
		mat3 tbn = getTangentFrame( - vViewPosition, normal,
		#if defined( USE_NORMALMAP )
			vNormalMapUv
		#elif defined( USE_CLEARCOAT_NORMALMAP )
			vClearcoatNormalMapUv
		#else
			vUv
		#endif
		);
	#endif
	#ifdef DOUBLE_SIDED
		tbn[0] *= faceDirection;
		tbn[1] *= faceDirection;
	#endif
#endif
#ifdef USE_CLEARCOAT_NORMALMAP
	#ifdef USE_TANGENT
		mat3 tbn2 = mat3( normalize( vTangent ), normalize( vBitangent ), normal );
	#else
		mat3 tbn2 = getTangentFrame( - vViewPosition, normal, vClearcoatNormalMapUv );
	#endif
	#ifdef DOUBLE_SIDED
		tbn2[0] *= faceDirection;
		tbn2[1] *= faceDirection;
	#endif
#endif
vec3 nonPerturbedNormal = normal;

	
// source chunk: normal_fragment_maps
#ifdef USE_NORMALMAP_OBJECTSPACE
	normal = texture2D( normalMap, vNormalMapUv ).xyz * 2.0 - 1.0;
	#ifdef FLIP_SIDED
		normal = - normal;
	#endif
	#ifdef DOUBLE_SIDED
		normal = normal * faceDirection;
	#endif
	normal = normalize( normalMatrix * normal );
#elif defined( USE_NORMALMAP_TANGENTSPACE )
	vec3 mapN = texture2D( normalMap, vNormalMapUv ).xyz * 2.0 - 1.0;
	#if defined( USE_PACKED_NORMALMAP )
		mapN = vec3( mapN.xy, sqrt( saturate( 1.0 - dot( mapN.xy, mapN.xy ) ) ) );
	#endif
	mapN.xy *= normalScale;
	normal = normalize( tbn * mapN );
#elif defined( USE_BUMPMAP )
	normal = perturbNormalArb( - vViewPosition, normal, dHdxy_fwd(), faceDirection );
#endif

normal = iwBumpN(normal, iwH, -vViewPosition);
	
// source chunk: clearcoat_normal_fragment_begin
#ifdef USE_CLEARCOAT
	vec3 clearcoatNormal = nonPerturbedNormal;
#endif

	
// source chunk: clearcoat_normal_fragment_maps
#ifdef USE_CLEARCOAT_NORMALMAP
	vec3 clearcoatMapN = texture2D( clearcoatNormalMap, vClearcoatNormalMapUv ).xyz * 2.0 - 1.0;
	clearcoatMapN.xy *= clearcoatNormalScale;
	clearcoatNormal = normalize( tbn2 * clearcoatMapN );
#endif

	
// source chunk: emissivemap_fragment
#ifdef USE_EMISSIVEMAP
	vec4 emissiveColor = texture2D( emissiveMap, vEmissiveMapUv );
	#ifdef DECODE_VIDEO_TEXTURE_EMISSIVE
		emissiveColor = sRGBTransferEOTF( emissiveColor );
	#endif
	totalEmissiveRadiance *= emissiveColor.rgb;
#endif

totalEmissiveRadiance += uFlash;
	
// source chunk: lights_physical_fragment
PhysicalMaterial material;
material.diffuseColor = diffuseColor.rgb;
material.diffuseContribution = diffuseColor.rgb * ( 1.0 - metalnessFactor );
material.metalness = metalnessFactor;
vec3 dxy = max( abs( dFdx( nonPerturbedNormal ) ), abs( dFdy( nonPerturbedNormal ) ) );
float geometryRoughness = max( max( dxy.x, dxy.y ), dxy.z );
material.roughness = max( roughnessFactor, 0.0525 );material.roughness += geometryRoughness;
material.roughness = min( material.roughness, 1.0 );
#ifdef IOR
	material.ior = ior;
	#ifdef USE_SPECULAR
		float specularIntensityFactor = specularIntensity;
		vec3 specularColorFactor = specularColor;
		#ifdef USE_SPECULAR_COLORMAP
			specularColorFactor *= texture2D( specularColorMap, vSpecularColorMapUv ).rgb;
		#endif
		#ifdef USE_SPECULAR_INTENSITYMAP
			specularIntensityFactor *= texture2D( specularIntensityMap, vSpecularIntensityMapUv ).a;
		#endif
		material.specularF90 = mix( specularIntensityFactor, 1.0, metalnessFactor );
	#else
		float specularIntensityFactor = 1.0;
		vec3 specularColorFactor = vec3( 1.0 );
		material.specularF90 = 1.0;
	#endif
	material.specularColor = min( pow2( ( material.ior - 1.0 ) / ( material.ior + 1.0 ) ) * specularColorFactor, vec3( 1.0 ) ) * specularIntensityFactor;
	material.specularColorBlended = mix( material.specularColor, diffuseColor.rgb, metalnessFactor );
#else
	material.specularColor = vec3( 0.04 );
	material.specularColorBlended = mix( material.specularColor, diffuseColor.rgb, metalnessFactor );
	material.specularF90 = 1.0;
#endif
#ifdef USE_CLEARCOAT
	material.clearcoat = clearcoat;
	material.clearcoatRoughness = clearcoatRoughness;
	material.clearcoatF0 = vec3( 0.04 );
	material.clearcoatF90 = 1.0;
	#ifdef USE_CLEARCOATMAP
		material.clearcoat *= texture2D( clearcoatMap, vClearcoatMapUv ).x;
	#endif
	#ifdef USE_CLEARCOAT_ROUGHNESSMAP
		material.clearcoatRoughness *= texture2D( clearcoatRoughnessMap, vClearcoatRoughnessMapUv ).y;
	#endif
	material.clearcoat = saturate( material.clearcoat );	material.clearcoatRoughness = max( material.clearcoatRoughness, 0.0525 );
	material.clearcoatRoughness += geometryRoughness;
	material.clearcoatRoughness = min( material.clearcoatRoughness, 1.0 );
#endif
#ifdef USE_DISPERSION
	material.dispersion = dispersion;
#endif
#ifdef USE_RETROREFLECTION
	material.retroreflectivity = retroreflectivity;
#endif
#ifdef USE_IRIDESCENCE
	material.iridescence = iridescence;
	material.iridescenceIOR = iridescenceIOR;
	#ifdef USE_IRIDESCENCEMAP
		material.iridescence *= texture2D( iridescenceMap, vIridescenceMapUv ).r;
	#endif
	#ifdef USE_IRIDESCENCE_THICKNESSMAP
		material.iridescenceThickness = (iridescenceThicknessMaximum - iridescenceThicknessMinimum) * texture2D( iridescenceThicknessMap, vIridescenceThicknessMapUv ).g + iridescenceThicknessMinimum;
	#else
		material.iridescenceThickness = iridescenceThicknessMaximum;
	#endif
#endif
#ifdef USE_SHEEN
	material.sheenColor = sheenColor;
	#ifdef USE_SHEEN_COLORMAP
		material.sheenColor *= texture2D( sheenColorMap, vSheenColorMapUv ).rgb;
	#endif
	material.sheenRoughness = clamp( sheenRoughness, 0.0001, 1.0 );
	#ifdef USE_SHEEN_ROUGHNESSMAP
		material.sheenRoughness *= texture2D( sheenRoughnessMap, vSheenRoughnessMapUv ).a;
	#endif
#endif
#ifdef USE_ANISOTROPY
	#ifdef USE_ANISOTROPYMAP
		mat2 anisotropyMat = mat2( anisotropyVector.x, anisotropyVector.y, - anisotropyVector.y, anisotropyVector.x );
		vec3 anisotropyPolar = texture2D( anisotropyMap, vAnisotropyMapUv ).rgb;
		vec2 anisotropyV = anisotropyMat * normalize( 2.0 * anisotropyPolar.rg - vec2( 1.0 ) ) * anisotropyPolar.b;
	#else
		vec2 anisotropyV = anisotropyVector;
	#endif
	material.anisotropy = length( anisotropyV );
	if( material.anisotropy == 0.0 ) {
		anisotropyV = vec2( 1.0, 0.0 );
	} else {
		anisotropyV /= material.anisotropy;
		material.anisotropy = saturate( material.anisotropy );
	}
	material.alphaT = mix( pow2( material.roughness ), 1.0, pow2( material.anisotropy ) );
	material.anisotropyT = tbn[ 0 ] * anisotropyV.x + tbn[ 1 ] * anisotropyV.y;
	material.anisotropyB = tbn[ 1 ] * anisotropyV.x - tbn[ 0 ] * anisotropyV.y;
#endif


        {
          float cc = 0.0, ccr = 0.3, sh = 0.0, shr = 0.6;
          if (iwCls == 0.0) { sh = 0.55; shr = 0.55; }        // jersey
          else if (iwCls == 1.0) { sh = 0.35; shr = 0.5; }    // twill
          else if (iwCls == 2.0) { sh = 0.6; shr = 0.45; }    // rib knit
          else if (iwCls == 3.0) { cc = 0.35; ccr = 0.28; sh = 0.1; }
          else if (iwCls == 4.0) { cc = 0.05; ccr = 0.5; }
          else if (iwCls == 6.0) { cc = 1.0; ccr = 0.12; }
          else if (iwCls == 7.0) { cc = 0.4; ccr = 0.15; }
          else if (iwCls == 8.0) { sh = 0.45; shr = 0.4; }
          else if (iwCls == 9.0) { sh = 0.4; shr = 0.4; }
          else if (iwCls == 10.0) { sh = 0.5; shr = 0.4; }
          else if (iwCls == 11.0) { sh = 0.7; shr = 0.35; }
          cc = max(cc, iwShine * 0.6); ccr = mix(ccr, 0.15, clamp(iwShine, 0.0, 1.0));
          cc = mix(cc, 1.0, iwHurtM);
          #ifdef USE_CLEARCOAT
            material.clearcoat = cc; material.clearcoatRoughness = max(ccr, 0.06);
          #endif
          #ifdef USE_SHEEN
            material.sheenColor = mix(vec3(1.0), diffuseColor.rgb * 1.3 + 0.12, 0.6) * sh * (1.0 - iwHurtM);
            material.sheenRoughness = shr;
          #endif
        }
      
	
// source chunk: lights_fragment_begin

vec3 geometryPosition = - vViewPosition;
vec3 geometryNormal = normal;
vec3 geometryViewDir = ( isOrthographic ) ? vec3( 0, 0, 1 ) : normalize( vViewPosition );
vec3 geometryClearcoatNormal = vec3( 0.0 );
#ifdef USE_CLEARCOAT
	geometryClearcoatNormal = clearcoatNormal;
#endif
#ifdef USE_IRIDESCENCE
	float dotNVi = saturate( dot( normal, geometryViewDir ) );
	if ( material.iridescenceThickness == 0.0 ) {
		material.iridescence = 0.0;
	} else {
		material.iridescence = saturate( material.iridescence );
	}
	if ( material.iridescence > 0.0 ) {
		vec3 iridescenceFresnelDielectric = evalIridescence( 1.0, material.iridescenceIOR, dotNVi, material.iridescenceThickness, material.specularColor );
		vec3 iridescenceFresnelMetallic = evalIridescence( 1.0, material.iridescenceIOR, dotNVi, material.iridescenceThickness, material.diffuseColor );
		material.iridescenceFresnel = mix( iridescenceFresnelDielectric, iridescenceFresnelMetallic, material.metalness );
		material.iridescenceF0Dielectric = Schlick_to_F0( iridescenceFresnelDielectric, 1.0, dotNVi );
		material.iridescenceF0Metallic = Schlick_to_F0( iridescenceFresnelMetallic, 1.0, dotNVi );
	}
#endif
#ifdef STANDARD
	float dotNVms = saturate( dot( geometryNormal, geometryViewDir ) );
	material.dfg = texture2D( dfgLUT, vec2( material.roughness, dotNVms ) ).rg;
	#if ( 0 > 0 || 1 > 0 || 0 > 0 || 0 > 0 )
		float EssMs = material.dfg.x + material.dfg.y;
		material.multiScatteringCompensation = 1.0 + material.specularColorBlended * ( 1.0 / EssMs - 1.0 );
	#endif
#endif
IncidentLight directLight;
#if ( 0 > 0 ) && defined( RE_Direct )
	PointLight pointLight;
	#if defined( USE_SHADOWMAP ) && 0 > 0
	PointLightShadow pointLightShadow;
	#endif
	
#endif
#if ( 0 > 0 ) && defined( RE_Direct )
	SpotLight spotLight;
	vec4 spotColor;
	vec3 spotLightCoord;
	bool inSpotLightMap;
	#if defined( USE_SHADOWMAP ) && 0 > 0
	SpotLightShadow spotLightShadow;
	#endif
	
#endif
#if ( 0 > 0 ) && defined( RE_Direct )
	SunLight sunLight;
	#if defined( USE_SHADOWMAP ) && 0 > 0
	SunLightShadow sunLightShadow;
	#endif
	
#endif
#if ( 1 > 0 ) && defined( RE_Direct )
	DirectionalLight directionalLight;
	#if defined( USE_SHADOWMAP ) && 0 > 0
	DirectionalLightShadow directionalLightShadow;
	#endif
	
		directionalLight = directionalLights[0];
		getDirectionalLightInfo( directionalLight, directLight );
		#if defined( USE_SHADOWMAP ) && ( 0 < 0 )
		directionalLightShadow = directionalLightShadows[0];
		directLight.color *= ( directLight.visible && receiveShadow ) ? getShadow( directionalShadowMap[0], directionalLightShadow.shadowMapSize, directionalLightShadow.shadowIntensity, directionalLightShadow.shadowBias, directionalLightShadow.shadowRadius, vDirectionalShadowCoord[0] ) : 1.0;
		#endif
		RE_Direct( directLight, geometryPosition, geometryNormal, geometryViewDir, geometryClearcoatNormal, material, reflectedLight );
	
#endif
#if ( 0 > 0 ) && defined( RE_Direct_RectArea )
	RectAreaLight rectAreaLight;
	
#endif
#if defined( RE_IndirectDiffuse )
	vec3 iblIrradiance = vec3( 0.0 );
	vec3 irradiance = getAmbientLightIrradiance( ambientLightColor );
	#if defined( USE_LIGHT_PROBES )
		irradiance += getLightProbeIrradiance( lightProbe, geometryNormal );
	#endif
	#if ( 1 > 0 )
		
			irradiance += getHemisphereLightIrradiance( hemisphereLights[0], geometryNormal );
		
	#endif
	#ifdef USE_LIGHT_PROBES_GRID
		vec3 probeWorldPos = ( ( vec4( geometryPosition, 1.0 ) - viewMatrix[ 3 ] ) * viewMatrix ).xyz;
		vec3 probeWorldNormal = transformNormalByInverseViewMatrix( geometryNormal, viewMatrix );
		irradiance += getLightProbeGridIrradiance( probeWorldPos, probeWorldNormal );
	#endif
#endif
#if defined( RE_IndirectSpecular )
	vec3 radiance = vec3( 0.0 );
	vec3 clearcoatRadiance = vec3( 0.0 );
#endif

	
// source chunk: lights_fragment_maps
#if defined( RE_IndirectDiffuse )
	#ifdef USE_LIGHTMAP
		vec4 lightMapTexel = texture2D( lightMap, vLightMapUv );
		vec3 lightMapIrradiance = lightMapTexel.rgb * lightMapIntensity;
		irradiance += lightMapIrradiance;
	#endif
	#if defined( USE_ENVMAP ) && defined( ENVMAP_TYPE_CUBE_UV )
		#if defined( STANDARD ) || defined( LAMBERT ) || defined( PHONG )
			iblIrradiance += getIBLIrradiance( geometryNormal );
		#endif
	#endif
#endif
#if defined( USE_ENVMAP ) && defined( RE_IndirectSpecular )
	#ifdef USE_ANISOTROPY
		vec3 iblRadiance = getIBLAnisotropyRadiance( geometryViewDir, geometryNormal, material.roughness, material.anisotropyB, material.anisotropy );
	#else
		vec3 iblRadiance = getIBLRadiance( geometryViewDir, geometryNormal, material.roughness );
	#endif
	#ifdef USE_RETROREFLECTION
		#ifdef USE_ANISOTROPY
			vec3 retroIBLRadiance = getIBLAnisotropyRetroRadiance( geometryViewDir, geometryNormal, material.roughness, material.anisotropyB, material.anisotropy );
		#else
			vec3 retroIBLRadiance = getIBLRetroRadiance( geometryViewDir, geometryNormal, material.roughness );
		#endif
		iblRadiance = mix( iblRadiance, retroIBLRadiance, saturate( material.retroreflectivity ) );
	#endif
	radiance += iblRadiance;
	#ifdef USE_CLEARCOAT
		clearcoatRadiance += getIBLRadiance( geometryViewDir, geometryClearcoatNormal, material.clearcoatRoughness );
	#endif
#endif

	
// source chunk: lights_fragment_end
#if defined( RE_IndirectDiffuse )
	#if defined( LAMBERT ) || defined( PHONG )
		irradiance += iblIrradiance;
	#endif
	RE_IndirectDiffuse( irradiance, geometryPosition, geometryNormal, geometryViewDir, geometryClearcoatNormal, material, reflectedLight );
#endif
#if defined( RE_IndirectSpecular )
	RE_IndirectSpecular( radiance, iblIrradiance, clearcoatRadiance, geometryPosition, geometryNormal, geometryViewDir, geometryClearcoatNormal, material, reflectedLight );
#endif

	
// source chunk: aomap_fragment
#ifdef USE_AOMAP
	float ambientOcclusion = ( texture2D( aoMap, vAoMapUv ).r - 1.0 ) * aoMapIntensity + 1.0;
	reflectedLight.indirectDiffuse *= ambientOcclusion;
	#if defined( USE_CLEARCOAT ) 
		clearcoatSpecularIndirect *= ambientOcclusion;
	#endif
	#if defined( USE_SHEEN ) 
		sheenSpecularIndirect *= ambientOcclusion;
	#endif
	#if defined( USE_ENVMAP ) && defined( STANDARD )
		float dotNV = saturate( dot( geometryNormal, geometryViewDir ) );
		reflectedLight.indirectSpecular *= computeSpecularOcclusion( dotNV, ambientOcclusion, material.roughness );
	#endif
#endif

reflectedLight.indirectDiffuse *= iwAO; reflectedLight.directDiffuse *= mix(1.0, iwAO, 0.35); reflectedLight.indirectSpecular *= mix(1.0, iwAO, 0.7);
	vec3 totalDiffuse = reflectedLight.directDiffuse + reflectedLight.indirectDiffuse;
	vec3 totalSpecular = reflectedLight.directSpecular + reflectedLight.indirectSpecular;
	
// source chunk: transmission_fragment
#ifdef USE_TRANSMISSION
	material.transmission = transmission;
	material.transmissionAlpha = 1.0;
	material.thickness = thickness;
	material.attenuationDistance = attenuationDistance;
	material.attenuationColor = attenuationColor;
	#ifdef USE_TRANSMISSIONMAP
		material.transmission *= texture2D( transmissionMap, vTransmissionMapUv ).r;
	#endif
	#ifdef USE_THICKNESSMAP
		material.thickness *= texture2D( thicknessMap, vThicknessMapUv ).g;
	#endif
	vec3 pos = vWorldPosition;
	vec3 v = normalize( cameraPosition - pos );
	vec3 n = transformNormalByInverseViewMatrix( normal, viewMatrix );
	vec4 transmitted = getIBLVolumeRefraction(
		n, v, material.roughness, material.diffuseContribution, material.specularColorBlended, material.specularF90,
		pos, modelMatrix, viewMatrix, projectionMatrix, material.dispersion, material.ior, material.thickness,
		material.attenuationColor, material.attenuationDistance );
	material.transmissionAlpha = mix( material.transmissionAlpha, transmitted.a, material.transmission );
	totalDiffuse = mix( totalDiffuse, transmitted.rgb, material.transmission );
#endif

	vec3 outgoingLight = totalDiffuse + totalSpecular + totalEmissiveRadiance;
	#ifdef USE_SHEEN
 
		outgoingLight = outgoingLight + sheenSpecularDirect + sheenSpecularIndirect;
 
 	#endif
	#ifdef USE_CLEARCOAT
		float dotNVcc = saturate( dot( geometryClearcoatNormal, geometryViewDir ) );
		vec3 Fcc = F_Schlick( material.clearcoatF0, material.clearcoatF90, dotNVcc );
		outgoingLight = outgoingLight * ( 1.0 - material.clearcoat * Fcc ) + ( clearcoatSpecularDirect + clearcoatSpecularIndirect ) * material.clearcoat;
	#endif
	{
        vec3 iwV = normalize(vViewPosition);
        float iwF = pow(1.0 - clamp(dot(normal, iwV), 0.0, 1.0), uIwRim.w);
        float iwS = clamp(dot(normal, uIwRimL) * 0.5 + 0.5, 0.0, 1.0);
        outgoingLight += uIwRim.rgb * (0.700 * iwF * (0.3 + 0.7 * iwS * iwS));
        // soft camera-side fill (a big bounce card above the lens): lifts faces out of low / back-lit sun
        float iwFl = clamp(dot(normal, normalize(vec3(0.25, 0.45, 1.0))) * 0.6 + 0.4, 0.0, 1.0);
        outgoingLight += uIwFill * diffuseColor.rgb * iwFl * iwFl;
      }
      
// source chunk: opaque_fragment
#ifdef OPAQUE
diffuseColor.a = 1.0;
#endif
#ifdef USE_TRANSMISSION
diffuseColor.a *= material.transmissionAlpha;
#endif
gl_FragColor = vec4( outgoingLight, diffuseColor.a );

	
// source chunk: tonemapping_fragment
#if defined( TONE_MAPPING )
	gl_FragColor.rgb = toneMapping( gl_FragColor.rgb );
#endif

	
// source chunk: colorspace_fragment
gl_FragColor = linearToOutputTexel( gl_FragColor );

	
// source chunk: fog_fragment
#ifdef USE_FOG
	#ifdef FOG_EXP2
		float fogFactor = 1.0 - exp( - fogDensity * fogDensity * vFogDepth * vFogDepth );
	#else
		float fogFactor = smoothstep( fogNear, fogFar, vFogDepth );
	#endif
	gl_FragColor.rgb = mix( gl_FragColor.rgb, fogColor, fogFactor );
#endif

	
// source chunk: premultiplied_alpha_fragment
#ifdef PREMULTIPLIED_ALPHA
	gl_FragColor.rgb *= gl_FragColor.a;
#endif

	
// source chunk: dithering_fragment
#ifdef DITHERING
	gl_FragColor.rgb = dithering( gl_FragColor.rgb );
#endif

}