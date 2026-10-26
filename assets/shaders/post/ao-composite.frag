#version 450 core
in vec2 vUv;
out vec4 color;
uniform sampler2D scene;
uniform sampler2D ao;
uniform sampler2D depth;
uniform float cameraNear;
uniform float cameraFar;
float viewDepth(float z) {
    return cameraNear*cameraFar/(cameraFar-z*(cameraFar-cameraNear));
}
void main() {
    ivec2 fullSize=textureSize(depth,0),lowSize=textureSize(ao,0);
    ivec2 pixel=clamp(ivec2(vUv*vec2(fullSize)),ivec2(0),fullSize-1);
    float center=texelFetch(depth,pixel,0).r;
    float occlusion=1.0;
    if(center<1.0) {
        float centerZ=viewDepth(center);
        vec2 p=vUv*vec2(lowSize)-0.5;
        ivec2 base=ivec2(floor(p));
        vec2 fraction=fract(p);
        float sum=0.0,weightSum=0.0;
        for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
            ivec2 q=clamp(base+ivec2(x,y),ivec2(0),lowSize-1);
            // Match the full-depth texel sampled at this low-resolution AO center.
            vec2 uv=(vec2(q)+0.5)/vec2(lowSize);
            ivec2 source=clamp(ivec2(uv*vec2(fullSize)),ivec2(0),fullSize-1);
            float z=texelFetch(depth,source,0).r;
            float spatial=(x==0?1.0-fraction.x:fraction.x)*(y==0?1.0-fraction.y:fraction.y);
            float w=z>=1.0?0.0:spatial*exp(-abs(viewDepth(z)-centerZ)/max(0.03,centerZ*0.01));
            sum+=texelFetch(ao,q,0).r*w;weightSum+=w;
        }
        // An unsampled thin foreground surface must not inherit background occlusion.
        if(weightSum>1e-5)occlusion=sum/weightSum;
    }
    color=vec4(texture(scene,vUv).rgb*mix(1.0,occlusion,0.8),1.0);
}
