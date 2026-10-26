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

precision highp float;
uniform vec3 uCol[8];     // current ink of each zone (neutral / holder), linear
uniform vec4 uSt[8];      // x active 0..1 · y reveal sweep radius (m) · z capture flash 0..1 · w visible 0..1
uniform vec4 uCen[8];     // xz centre, sweep reach (m), held 0..1
uniform vec4 uFl[8];      // x capture-flood front radius (m) · y its strength 0..1 · z contested 0..1 · w contester
uniform vec3 uTeamC[2];         // team inks (linear)
uniform float uTime, uWide, uFarK;
varying vec4 vInfo;
varying vec3 vW;
const vec3 K = vec3(0.0075, 0.0065, 0.012);
float band(float d, float a, float b, float aa) { return smoothstep(a - aa, a + aa, d) * (1.0 - smoothstep(b - aa, b + aa, d)); }
void main() {
  int i = int(vInfo.x + 0.5);
  vec3 col = uCol[i]; vec4 st = uSt[i]; vec4 cen = uCen[i];
  float act = st.x, flash = st.z;
  float r = length(vW.xz - cen.xy);
  float reveal = 1.0 - smoothstep(st.y - 0.9, st.y, r);
  float front = smoothstep(st.y - 1.4, st.y - 0.15, r) * reveal * step(st.y, cen.z + 1.0);   // bright leading edge of the sweep
  float s = vInfo.w;
  vec3 bright = mix(col, vec3(1.0), 0.16 + 0.5 * flash);
  vec4 fl = uFl[i];
  // bolder with distance: today's look within ~11 m, easing up to ≈ 3.5× by ~44 m
  float far = smoothstep(11.0, 44.0, length(vW - cameraPosition)) * uFarK;
  vec4 A = vec4(0.0), Gh = vec4(0.0);
  if (vInfo.y < 0.5) {
    float d = vInfo.z;
    float px = fwidth(d);
    float aa = max(px * 0.75, 0.003);
    // band scale: bolder far away, wider in the map view, and never thinner than ~2.5 px at grazing angles
    float w = clamp(max(max(mix(1.0, 2.2, uWide), mix(1.0, 3.5, far)), px * 9.0), 1.0, 3.6);
    float held = cen.w;
    // active, outside in: a dark keyline | the band | a white pinline | a dark hairline, then an inner glow and a core
    // tint. Neutral, the band is Splat-Zones yellow striped with dark (light AND dark, so it reads on white decking and
    // dark cobbles alike; far away the stripes average to a deep ochre); held, it is the holder's ink, stripes faint.
    float rim = 1.0 - smoothstep(0.09 * w - aa, 0.09 * w + aa, d);
    float ink = band(d, 0.09 * w, 0.4 * w, aa);
    float pin = band(d, 0.4 * w, 0.46 * w, aa);
    float hair = band(d, 0.46 * w, 0.52 * w, aa);
    float q = (s - d) * 1.35 / mix(1.0, w, 0.8) - uTime * 0.45, fq = min(fwidth(q), 0.5);
    float stripe = smoothstep(0.3 - fq, 0.3 + fq, abs(fract(q) - 0.5));            // 1 on the dark stripes (40 %)
    vec3 inkC = mix(mix(bright, K * 1.5, stripe), bright * (1.0 + 0.12 * stripe), held);
    // contested: the band pulses in the contesting team's ink
    inkC = mix(inkC, uTeamC[int(fl.w + 0.5)] * 1.15, fl.z * (0.35 + 0.35 * sin(uTime * 10.0)));
    float g = 1.0 - smoothstep(0.52 * w, 0.52 * w + 1.6 + 0.3 * uWide, d);
    vec3 c = K * rim + inkC * ink + vec3(1.0) * pin + K * hair;
    float a = rim * 0.95 + ink * 0.97 + pin * 0.95 + hair * 0.7;
    float ga = (mix(0.46, 0.34, held) + 0.35 * flash + 0.3 * front) * g * g + mix(0.13, 0.08, held) + 0.1 * uWide;
    A.rgb = c + col * ga * (1.0 - a);
    A.a = a + ga * (1.0 - a);
    A.rgb /= max(A.a, 1e-4);
    A.rgb = mix(A.rgb, vec3(1.0), front * 0.55);
    // capture flood: a bright crest riding the ink front out across the zone, a wet sheen just behind it
    if (fl.y > 0.001) {
      float x = r - fl.x;
      float crest = exp(-x * x * 4.0) * fl.y;
      float wake = (1.0 - smoothstep(0.0, 2.2, -x)) * step(x, 0.0) * fl.y;
      float ca = clamp(crest * 0.85 + wake * 0.3, 0.0, 0.92);
      vec3 cc = mix(bright, vec3(1.0), 0.55 * crest / max(crest + wake, 1e-3));
      float na = ca + A.a * (1.0 - ca);
      A.rgb = (cc * ca + A.rgb * A.a * (1.0 - ca)) / max(na, 1e-4);
      A.a = na;
    }
    // ghost: a thin dashed line
    float gw = clamp(max(max(mix(0.12, 0.3, uWide), mix(0.12, 0.26, far)), px * 2.2), 0.12, 0.6);
    float dash = step(0.42, fract(s / 1.15));
    float gl = (1.0 - smoothstep(gw - aa, gw + aa, d));
    float grim = 1.0 - smoothstep(0.035 - aa, 0.035 + aa, d);
    Gh.rgb = mix(vec3(0.96), K, grim);
    Gh.a = gl * dash * mix(0.42, 0.8, uWide);
  } else {
    // light curtain: a bright foot, a gradient fading upward and thin scan lines drifting up it
    float hgt = vInfo.z;
    float fade = pow(clamp(1.0 - hgt, 0.0, 1.0), 1.5);
    float foot = 1.0 - smoothstep(0.025, 0.07, hgt);
    float scan = smoothstep(0.4, 0.49, abs(fract(hgt * 4.0 - uTime * 0.45) - 0.5));
    float held = cen.w;
    A.rgb = mix(bright, vec3(1.0), mix(0.08, 0.2, held) + 0.3 * scan + 0.3 * flash);
    A.a = ((mix(0.46, 0.36, held) + 0.36 * scan + 0.3 * flash) * fade + 0.6 * foot) * (1.0 - uWide * 0.75) * (1.0 + 0.4 * far);
  }
  float ka = act * reveal;
  vec4 o;
  o.a = A.a * ka + Gh.a * (1.0 - ka);
  o.rgb = (A.rgb * A.a * ka + Gh.rgb * Gh.a * (1.0 - ka)) / max(o.a, 1e-4);
  o.a *= st.w;
  if (o.a < 0.004) discard;
  gl_FragColor = o;
  
// source chunk: tonemapping_fragment
#if defined( TONE_MAPPING )
	gl_FragColor.rgb = toneMapping( gl_FragColor.rgb );
#endif

  
// source chunk: colorspace_fragment
gl_FragColor = linearToOutputTexel( gl_FragColor );

}