#version 450 core
in vec2 vUv;
out vec4 color;
uniform sampler2D tDiffuse;
uniform vec2 resolution;

float luma(vec3 rgb){return dot(rgb,vec3(.299,.587,.114));}

void main(){
    vec2 invResolution=1./resolution;
    vec3 nw=texture(tDiffuse,vUv+vec2(-1.,-1.)*invResolution).rgb;
    vec3 ne=texture(tDiffuse,vUv+vec2(1.,-1.)*invResolution).rgb;
    vec3 sw=texture(tDiffuse,vUv+vec2(-1.,1.)*invResolution).rgb;
    vec3 se=texture(tDiffuse,vUv+vec2(1.,1.)*invResolution).rgb;
    vec3 center=texture(tDiffuse,vUv).rgb;
    float lnw=luma(nw),lne=luma(ne),lsw=luma(sw),lse=luma(se),lc=luma(center);
    float lumaMin=min(lc,min(min(lnw,lne),min(lsw,lse)));
    float lumaMax=max(lc,max(max(lnw,lne),max(lsw,lse)));
    vec2 direction=vec2(-((lnw+lne)-(lsw+lse)),(lnw+lsw)-(lne+lse));
    float reduction=max((lnw+lne+lsw+lse)*.03125,.0078125);
    float reciprocalMin=1./(min(abs(direction.x),abs(direction.y))+reduction);
    direction=clamp(direction*reciprocalMin,vec2(-8.),vec2(8.))*invResolution;
    vec3 a=.5*(texture(tDiffuse,vUv+direction*(1./3.-.5)).rgb+texture(tDiffuse,vUv+direction*(2./3.-.5)).rgb);
    vec3 b=a*.5+.25*(texture(tDiffuse,vUv+direction*-.5).rgb+texture(tDiffuse,vUv+direction*.5).rgb);
    float lb=luma(b);
    color=vec4((lb<lumaMin||lb>lumaMax)?a:b,1.);
}
