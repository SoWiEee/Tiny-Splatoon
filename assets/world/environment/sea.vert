#version 450 core
#define HIGH_PRECISION
#define OPAQUE
#define USE_SHADOWMAP
#define SHADOWMAP_TYPE_PCF

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;


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


// source chunk: shadowmap_pars_vertex
#if 0 > 0
	uniform mat4 spotLightMatrix[ 0 ];
	varying vec4 vSpotLightCoord[ 0 ];
#endif
#ifdef USE_SHADOWMAP
	#if 0 > 0
		varying vec4 vSunShadowWorldPosition;
		varying vec3 vSunShadowWorldNormal;
	#endif
	#if 1 > 0
		uniform mat4 directionalShadowMatrix[ 1 ];
		varying vec4 vDirectionalShadowCoord[ 1 ];
		struct DirectionalLightShadow {
			float shadowIntensity;
			float shadowBias;
			float shadowNormalBias;
			float shadowRadius;
			vec2 shadowMapSize;
		};
		uniform DirectionalLightShadow directionalLightShadows[ 1 ];
	#endif
	#if 0 > 0
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
		uniform mat4 pointShadowMatrix[ 0 ];
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
#endif

uniform float uTime;

uniform vec4 uRects[48];
uniform vec2 uRectAx[48];
uniform int uRectCount;
// axis-aligned rect given as (minX, minZ, maxX, maxZ) — the arena bounds
float sdRect(vec2 p, vec4 r) {
  vec2 c = (r.xy + r.zw) * 0.5; vec2 h = (r.zw - r.xy) * 0.5;
  vec2 q = abs(p - c) - h;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}
// oriented rect: r = (centre, half extents), a = local x axis
float sdORect(vec2 p, vec4 r, vec2 a) {
  vec2 d = p - r.xy;
  vec2 q = abs(vec2(dot(d, a), dot(d, vec2(-a.y, a.x)))) - r.zw;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}
#ifdef DECK_FIELD
// more rects than uniform slots: the deck union (R) and the marina hull union (G) baked into a signed-distance field
// (Environment._bakeDeckField), exact within uDeckFieldK.y of any rect; past that, the distance to the rects' AABB
uniform sampler2D uDeckField;
uniform vec4 uDeckFieldRect;   // field origin (x, z), 1 / field size (x, z)
uniform vec4 uDeckFieldK;      // x: texel (m), y: exact reach (m)
uniform vec4 uDeckBox;         // AABB of the deck rects (minX, minZ, maxX, maxZ)
uniform vec4 uWetBox;          // AABB of the hull rects
vec2 deckField(vec2 p) { return textureLod(uDeckField, clamp((p - uDeckFieldRect.xy) * uDeckFieldRect.zw, 0.0, 1.0), 0.0).rg; }
float sdDeck(vec2 p) { return max(deckField(p).r, sdRect(p, uDeckBox)); }
#else
float sdDeck(vec2 p) {
  float d = 1e5;
  for (int i = 0; i < 48; i++) { if (i >= uRectCount) break; d = min(d, sdORect(p, uRects[i], uRectAx[i])); }
  return d;
}
#endif
#ifdef MARINA
uniform vec4 uWet[24];
uniform vec2 uWetAx[24];
uniform int uWetCount;
uniform vec4 uArena;        // arena bounds (minX, minZ, maxX, maxZ): the harbour basin around it is sheltered
float basinK(vec2 p) { return smoothstep(60.0, 210.0, sdRect(p, uArena)); }   // 0 in the basin → 1 open sea
#ifdef DECK_FIELD
float sdWet(vec2 p) { return max(deckField(p).g, sdRect(p, uWetBox)); }
#else
float sdWet(vec2 p) {
  float d = 1e5;
  for (int i = 0; i < 24; i++) { if (i >= uWetCount) break; d = min(d, sdORect(p, uWet[i], uWetAx[i])); }
  return d;
}
#endif
#endif


// long gentle swell; returns height (unit amplitude) and writes gradient
float swell(vec2 p, float t, out vec2 g) {
  float h = 0.0; g = vec2(0.0);
  vec2 k; float ph;
  k = vec2(0.110, 0.047); ph = dot(p, k) + t * 0.95;        h += 0.45 * sin(ph); g += 0.45 * k * cos(ph);
  k = vec2(-0.052, 0.097); ph = dot(p, k) + t * 1.13 + 1.7; h += 0.35 * sin(ph); g += 0.35 * k * cos(ph);
  k = vec2(0.173, -0.141); ph = dot(p, k) + t * 1.61 + 4.1; h += 0.20 * sin(ph); g += 0.20 * k * cos(ph);
  return h;
}
float swellAmp(float dDeck) { return mix(0.035, 0.16, smoothstep(3.0, 70.0, dDeck)); }

varying vec3 vWorld;
varying vec2 vSwellGrad;
varying float vDeckD;
void main() {
  vec4 worldPosition = modelMatrix * vec4(position, 1.0);
  float dd = sdDeck(worldPosition.xz);
#ifdef MARINA
  dd = min(dd, sdWet(worldPosition.xz));
#endif
  float amp = swellAmp(dd);
#ifdef MARINA
  amp *= mix(0.45, 1.0, basinK(worldPosition.xz));
#endif
  vec2 g;
  float h = swell(worldPosition.xz, uTime, g);
  worldPosition.y += h * amp;
  vWorld = worldPosition.xyz;
  vSwellGrad = g * amp;
  vDeckD = dd;
  vec4 mvPosition = viewMatrix * worldPosition;
  vec3 transformedNormal = normalMatrix * vec3(0.0, 1.0, 0.0);
  
// source chunk: shadowmap_vertex
#if ( defined( USE_SHADOWMAP ) && ( 1 > 0 || 0 > 0 || 0 > 0 ) ) || ( 0 > 0 )
	#ifdef HAS_NORMAL
		vec3 shadowWorldNormal = transformNormalByInverseViewMatrix( transformedNormal, viewMatrix );
	#else
		vec3 shadowWorldNormal = vec3( 0.0 );
	#endif
	vec4 shadowWorldPosition;
#endif
#if defined( USE_SHADOWMAP )
	#if 0 > 0
		vSunShadowWorldPosition = vec4( worldPosition.xyz, - mvPosition.z );
		vSunShadowWorldNormal = shadowWorldNormal;
	#endif
	#if 1 > 0
		
			shadowWorldPosition = worldPosition + vec4( shadowWorldNormal * directionalLightShadows[0].shadowNormalBias, 0 );
			vDirectionalShadowCoord[0] = directionalShadowMatrix[0] * shadowWorldPosition;
		
	#endif
	#if 0 > 0
		
	#endif
#endif
#if 0 > 0
	
#endif

  gl_Position = projectionMatrix * mvPosition;
}
