#version 450 core
#define HIGH_PRECISION
#define DOUBLE_SIDED

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying in
layout(location=0)out vec4 iwFragColor;
#define gl_FragColor iwFragColor
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
vec4 LinearTransferOETF( in vec4 value ) {
	return value;
}
vec4 sRGBTransferEOTF( in vec4 value ) {
	return vec4( mix( pow( value.rgb * 0.9478672986 + vec3( 0.0521327014 ), vec3( 2.4 ) ), value.rgb * 0.0773993808, vec3( lessThanEqual( value.rgb, vec3( 0.04045 ) ) ) ), value.a );
}
vec4 sRGBTransferOETF( in vec4 value ) {
	return vec4( mix( pow( value.rgb, vec3( 0.41666 ) ) * 1.055 - vec3( 0.055 ), value.rgb * 12.92, vec3( lessThanEqual( value.rgb, vec3( 0.0031308 ) ) ) ), value.a );
}
vec3 toneMapping(vec3 color){return NeutralToneMapping(color);}
vec4 linearToOutputTexel(vec4 color){return LinearTransferOETF(color);}


float fxHash(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float fxNoise(vec2 p){ vec2 i = floor(p), f = fract(p); vec2 u = f*f*(3.0-2.0*f);
  return mix(mix(fxHash(i), fxHash(i+vec2(1.0,0.0)), u.x), mix(fxHash(i+vec2(0.0,1.0)), fxHash(i+vec2(1.0,1.0)), u.x), u.y); }

uniform float uTime;
varying vec2 vUv;
varying vec4 vCol;
varying vec4 vMisc;
varying float vT;
void main() {
  float r = length(vUv);
  if (r > 1.0) discard;
  float ang = atan(vUv.y, vUv.x);
  float seed = vMisc.x * 53.0;
  float wob = fxNoise(vec2(cos(ang), sin(ang)) * 3.0 + seed) - 0.5;
  float a = 0.0;
  vec3 col = vCol.rgb;
  float st = vMisc.y;
  if (st < 0.5) {
    // ink shockwave: thick wobbly band that thins as it expands, plus droplet beads on the rim
    float outer = 0.93 + wob * 0.1;
    float w = mix(0.34, 0.07, vT) * vMisc.z;
    float band = smoothstep(outer + 0.02, outer - 0.02, r) * smoothstep(outer - w - 0.03, outer - w + 0.02, r);
    float beads = smoothstep(0.35, 0.0, length(vec2(fract(ang / 6.2831 * 18.0 + seed) - 0.5, (r - outer) * 7.0))) * step(0.45, fxHash(vec2(floor(ang / 6.2831 * 18.0 + seed), seed)));
    float fill = pow(1.0 - vT, 4.0) * 0.45 * smoothstep(outer, outer * 0.3, r);
    a = max(max(band, beads * (1.0 - vT)), fill);
    col *= 0.9 + 0.35 * smoothstep(outer - w, outer, r);
    a *= pow(1.0 - vT, 1.3);
  } else if (st < 1.5) {
    // water / swim ripple: thin soft outline
    float outer = 0.9 + wob * 0.06;
    a = smoothstep(outer + 0.06, outer, r) * smoothstep(outer - 0.16, outer - 0.03, r) * (1.0 - vT) * (1.0 - vT);
    col = mix(col, vec3(1.0), 0.35);
  } else if (st < 2.5) {
    // splash disc: quick filled blot
    a = smoothstep(0.95 + wob * 0.2, 0.7 + wob * 0.2, r) * pow(1.0 - vT, 2.0);
  } else if (st < 3.5) {
    // bomb danger zone: bold rim, rotating dashes, pulses that quicken with the fuse (vT), tinted fill, dark outline
    float spin = uTime * (0.5 + vT * 1.8);
    float rim = smoothstep(0.045, 0.012, abs(r - 0.955));
    float dashes = step(0.45, fract(ang / 6.2831853 * 16.0 - spin)) * smoothstep(0.04, 0.012, abs(r - 0.87));
    float ph = fract(uTime * (1.0 + vT * 4.0));
    float wave = smoothstep(0.07, 0.0, abs(r - ph * 0.95)) * (1.0 - ph) * 0.85;
    float fill = (0.1 + 0.24 * vT) * (0.55 + 0.45 * smoothstep(0.0, 0.95, r)) * step(r, 0.955);
    float dark = smoothstep(0.03, 0.0, abs(r - 0.99));
    a = max(max(rim, dashes * 0.9), max(wave, fill));
    col = mix(col, vec3(1.0), rim * 0.3 + wave * 0.25);
    col = mix(col, vec3(0.04), dark * 0.7); a = max(a, dark * 0.5);
  } else if (st < 4.5) {
    // super-jump target: rim, inner ring, four rotating inward chevrons, contracting pulse, blinking core
    float spin = uTime * 1.2;
    float outer = smoothstep(0.045, 0.012, abs(r - 0.94));
    float dark = smoothstep(0.035, 0.0, abs(r - 0.99));
    float inner = smoothstep(0.03, 0.008, abs(r - 0.56)) * 0.85;
    float sa = ang - spin;
    float seg = mod(sa + 0.3927, 1.5707963) - 0.7853981;
    vec2 pl = vec2(r - 0.76, seg * 0.76);
    float chev = smoothstep(0.04, 0.0, abs(pl.x + abs(pl.y) * 0.9 - 0.02)) * step(abs(pl.y), 0.14);
    float ph = fract(uTime * 1.1);
    float wave = smoothstep(0.05, 0.0, abs(r - (1.0 - ph) * 0.9)) * ph * 0.85;
    float core = smoothstep(0.2, 0.14, r) * (0.7 + 0.3 * sin(uTime * 9.0));
    float fill = 0.14 * step(r, 0.94);
    a = max(max(outer, inner), max(max(chev, wave), max(core, fill)));
    col = mix(col, vec3(1.0), outer * 0.25 + core * 0.45 + chev * 0.25);
    col = mix(col, vec3(0.04), dark * 0.7); a = max(a, dark * 0.55);
  } else if (st < 5.5) {
    // dust ring: soft noisy band
    float nn = fxNoise(vUv * 5.0 + seed);
    float outer = 0.86 + wob * 0.16;
    float band = smoothstep(outer, outer - 0.3, r) * smoothstep(outer - 0.78, outer - 0.25, r);
    a = band * (0.55 + 0.45 * nn) * pow(1.0 - vT, 1.6);
    col *= 0.9 + 0.2 * nn;
  } else if (st < 6.5) {
    // glossy splat blot with spiky rim + highlight
    float spikes = fxNoise(vec2(ang * 2.6 + seed, seed));
    float edge = 0.55 + 0.38 * spikes + wob * 0.2;
    a = smoothstep(edge, edge - 0.14, r) * pow(1.0 - vT, 1.5);
    float hl = smoothstep(0.35, 0.0, length(vUv - vec2(-0.22, 0.26)));
    col = col * (0.82 + 0.3 * (1.0 - r)) + vec3(0.3) * hl;
  } else {
    // thin shockwave (air bursts / pulses)
    float w = mix(0.14, 0.035, vT) * max(vMisc.z, 0.3);
    float d = (r - 0.93) / w;
    a = exp(-d * d) * pow(1.0 - vT, 1.1);
    col = mix(col, vec3(1.0), 0.4);
  }
  gl_FragColor = vec4(col, a * vCol.a);
  if (gl_FragColor.a < 0.004) discard;
  
  
// source chunk: tonemapping_fragment
#if defined( TONE_MAPPING )
	gl_FragColor.rgb = toneMapping( gl_FragColor.rgb );
#endif

  
// source chunk: colorspace_fragment
gl_FragColor = linearToOutputTexel( gl_FragColor );


}
