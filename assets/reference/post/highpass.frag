#version 450 core
#define varying in
#define texture2D texture
#define textureCube texture
layout(location=0) out vec4 postColor;
#define gl_FragColor postColor
float luminance(const in vec3 rgb){return dot(vec3(0.2126,0.7152,0.0722),rgb);}


		uniform sampler2D tDiffuse;
		uniform vec3 defaultColor;
		uniform float defaultOpacity;
		uniform float luminosityThreshold;
		uniform float smoothWidth;

		varying vec2 vUv;

		void main() {

			vec4 texel = texture2D( tDiffuse, vUv );

			float v = luminance( texel.xyz );

			vec4 outputColor = vec4( defaultColor.rgb, defaultOpacity );

			float alpha = smoothstep( luminosityThreshold, luminosityThreshold + smoothWidth, v );

			gl_FragColor = mix( outputColor, texel, alpha );

		}