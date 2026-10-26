#include "RenderPipeline.hpp"
#include "CharacterShader.hpp"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace restoration {
namespace {
const char* fullscreen=R"GLSL(#version 450 core
out vec2 vUv;
void main(){vUv=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(vUv*2.-1.,0.,1.);}
)GLSL";
const char* multiply=R"GLSL(#version 450 core
in vec2 vUv;out vec4 color;uniform sampler2D scene;uniform sampler2D ao;
void main(){color=vec4(texture(scene,vUv).rgb*mix(vec3(1),texture(ao,vUv).rgb,.8),1);}
)GLSL";
const char* add=R"GLSL(#version 450 core
in vec2 vUv;out vec4 color;uniform sampler2D scene;uniform sampler2D bloom;
void main(){color=vec4(texture(scene,vUv).rgb+texture(bloom,vUv).rgb,1);}
)GLSL";
struct Target {
    GLuint fbo=0,color=0,depth=0;int width=0,height=0;
    ~Target(){clear();}
    void clear(){if(depth)glDeleteTextures(1,&depth);if(color)glDeleteTextures(1,&color);if(fbo)glDeleteFramebuffers(1,&fbo);fbo=color=depth=0;}
    void resize(int w,int h,int samples=0,bool withDepth=false){
        clear();width=w;height=h;
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        const GLenum type=samples?GL_TEXTURE_2D_MULTISAMPLE:GL_TEXTURE_2D;
        auto texture=[&](GLuint& id,GLenum internal,GLenum attachment){
            glGenTextures(1,&id);glBindTexture(type,id);
            if(samples)glTexImage2DMultisample(type,samples,internal,w,h,GL_TRUE);
            else{glTexStorage2D(type,1,internal,w,h);glTexParameteri(type,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(type,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(type,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(type,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
            glFramebufferTexture2D(GL_FRAMEBUFFER,attachment,type,id,0);
        };
        texture(color,GL_RGBA16F,GL_COLOR_ATTACHMENT0);
        if(withDepth)texture(depth,GL_DEPTH_COMPONENT32F,GL_DEPTH_ATTACHMENT);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete HDR framebuffer");
    }
};
void bindTexture(const CharacterShader& shader,const char* uniform,GLuint texture,int unit){
    glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,texture);shader.uniform(uniform,double(unit));
}
std::vector<double> matrix(const glm::mat4& m){const auto* p=glm::value_ptr(m);return {p,p+16};}
GLuint noiseTexture(const std::filesystem::path& path,int width){
    const auto data=CharacterShader::readSource(path);
    if(data.size()!=static_cast<std::size_t>(width*width*4))throw std::runtime_error("Invalid post noise fixture");
    GLuint id=0;glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,width,0,GL_RGBA,GL_UNSIGNED_BYTE,data.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    return id;
}
}
GradeSettings GradeSettings::Theme(const std::string& theme){
    GradeSettings g;
    if(theme=="sunset"){g.exposure=1;g.saturation=1.05f;g.vibrance=.1f;g.contrast=1.07f;g.vignette=.28f;g.shadowTint={.88f,.96f,1.16f};g.highlightTint={1.07f,1,.88f};g.bloomStrength=.4f;g.bloomRadius=.55f;g.bloomThreshold=1.7f;}
    else if(theme=="golden"){g.exposure=.95f;g.saturation=1.05f;g.vibrance=.12f;g.contrast=1.1f;g.vignette=.22f;g.shadowTint={.93f,.98f,1.09f};g.highlightTint={1.06f,1,.925f};}
    else if(theme=="day"){g.exposure=.9f;g.saturation=1.1f;g.vibrance=.12f;g.contrast=1.09f;g.vignette=.2f;g.shadowTint={.92f,.97f,1.1f};g.highlightTint={1.035f,1,.955f};}
    else throw std::invalid_argument("Unknown environment grade theme: "+theme);
    return g;
}
struct RenderPipeline::State {
    std::unique_ptr<CharacterShader> gtao,denoise,highpass,composite,grade,screen,output,fxaa,mul,sum;
    std::array<std::unique_ptr<CharacterShader>,5> blur;
    Target scene,resolved,ao,filtered,ping,pong,bright,bloom;
    std::array<Target,5> horizontal,vertical;
    GLuint vao=0,noise=0,pdNoise=0;
    int samples=4,width=0,height=0,allocatedSamples=-1;bool useAo=true,useBloom=true,useFxaa=false,active=false;
    glm::mat4 projection{1},world{1};float nearPlane=.1f,farPlane=1000;
    explicit State(const std::filesystem::path& path,const std::filesystem::path& shaderAssets){
        auto load=[&](const char* name){return std::make_unique<CharacterShader>(fullscreen,CharacterShader::readSource(path/name));};
        gtao=load("gtao.frag");denoise=load("denoise.frag");highpass=load("highpass.frag");composite=load("bloom-composite.frag");grade=load("grade.frag");screen=load("screen.frag");output=load("output.frag");fxaa=std::make_unique<CharacterShader>(fullscreen,CharacterShader::readSource(shaderAssets/"post/fxaa.frag"));
        mul=std::make_unique<CharacterShader>(fullscreen,multiply);sum=std::make_unique<CharacterShader>(fullscreen,add);
        for(int i=0;i<5;i++){
            blur[i]=load(("blur-"+std::to_string(i)+".frag").c_str());
            const int radius=6+4*i;const double sigma=radius/3.;
            auto coefficient=[&](int k){return .39894*std::exp(-.5*k*k/(sigma*sigma))/sigma;};
            std::vector<double> offsets,weights;
            for(int k=1;k<radius;k+=2){const double a=coefficient(k),b=k+1<radius?coefficient(k+1):0,w=a+b;offsets.push_back((k*a+(k+1)*b)/w);weights.push_back(w);}
            blur[i]->use();blur[i]->uniform("centerWeight",coefficient(0));blur[i]->uniform("gaussianOffsets",offsets);blur[i]->uniform("gaussianWeights",weights);
        }
        try{noise=noiseTexture(path/"gtao-noise.bin",5);pdNoise=noiseTexture(path/"denoise-noise.bin",64);glGenVertexArrays(1,&vao);}
        catch(...){if(noise)glDeleteTextures(1,&noise);if(pdNoise)glDeleteTextures(1,&pdNoise);throw;}
    }
    ~State(){if(vao)glDeleteVertexArrays(1,&vao);if(noise)glDeleteTextures(1,&noise);if(pdNoise)glDeleteTextures(1,&pdNoise);}
    void resize(int w,int h){
        if(width==w&&height==h&&allocatedSamples==samples)return;
        width=w;height=h;allocatedSamples=samples;
        scene.resize(w,h,samples,true);resolved.resize(w,h,0,true);
        for(auto* t:{&ao,&filtered,&ping,&pong})t->resize(w,h);
        int bw=std::max(1,(w+1)/2),bh=std::max(1,(h+1)/2);
        bright.resize(bw,bh);bloom.resize(bw,bh);
        for(int i=0;i<5;i++){horizontal[i].resize(bw,bh);vertical[i].resize(bw,bh);bw=std::max(1,(bw+1)/2);bh=std::max(1,(bh+1)/2);}
    }
    void target(const Target& t){glBindFramebuffer(GL_FRAMEBUFFER,t.fbo);glViewport(0,0,t.width,t.height);}
    void draw(){glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);}
};
RenderPipeline::RenderPipeline(std::filesystem::path assets,std::filesystem::path shaderAssets):state_(std::make_unique<State>(assets,shaderAssets)){}
RenderPipeline::~RenderPipeline()=default;
void RenderPipeline::Configure(int samples,bool ao,bool bloom,bool antialiasing){
    GLint maximum=0;glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES,&maximum);
    if(samples<0||(antialiasing&&samples>maximum))throw std::invalid_argument("Unsupported HDR sample count");
    if(state_->active)throw std::logic_error("Cannot reconfigure an active frame");
    state_->samples=antialiasing?samples:0;state_->useAo=ao;state_->useBloom=bloom;state_->useFxaa=antialiasing&&samples==0;
}
void RenderPipeline::Begin(int w,int h,const glm::mat4& p,const glm::mat4& world,float nearPlane,float farPlane){
    auto& s=*state_;GLint maxSize=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxSize);
    if(s.active)throw std::logic_error("HDR frame already active");
    if(w<=0||h<=0||w>maxSize||h>maxSize||!std::isfinite(nearPlane)||!std::isfinite(farPlane)||nearPlane<=0||farPlane<=nearPlane)throw std::invalid_argument("Invalid HDR viewport or projection range");
    s.resize(w,h);s.projection=p;s.world=world;s.nearPlane=nearPlane;s.farPlane=farPlane;s.active=true;
    s.target(s.scene);glDisable(GL_FRAMEBUFFER_SRGB);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
}
int RenderPipeline::End(const GradeSettings& g,const ScreenEffects& f,int outputWidth,int outputHeight){
    auto& s=*state_;if(!s.active)throw std::logic_error("No HDR frame to resolve");
    glBindFramebuffer(GL_READ_FRAMEBUFFER,s.scene.fbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,s.resolved.fbo);
    glBlitFramebuffer(0,0,s.width,s.height,0,0,s.width,s.height,GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT,GL_NEAREST);
    glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    GLuint current=s.resolved.color;int draws=0;
    if(s.useAo){
        s.target(s.ao);glClearColor(1,1,1,1);glClear(GL_COLOR_BUFFER_BIT);auto& a=*s.gtao;a.use();
        bindTexture(a,"tDepth",s.resolved.depth,0);bindTexture(a,"tNoise",s.noise,1);
        a.uniform("resolution",{double(s.width),double(s.height)});a.uniform("cameraNear",s.nearPlane);a.uniform("cameraFar",s.farPlane);
        a.uniform("cameraProjectionMatrix",matrix(s.projection));a.uniform("cameraProjectionMatrixInverse",matrix(glm::inverse(s.projection)));a.uniform("cameraWorldMatrix",matrix(s.world));
        a.uniform("radius",1.1);a.uniform("distanceExponent",1.6);a.uniform("thickness",1.);a.uniform("scale",1.5);a.uniform("distanceFallOff",1.);s.draw();draws++;
        s.target(s.filtered);glClear(GL_COLOR_BUFFER_BIT);auto& d=*s.denoise;d.use();bindTexture(d,"tDiffuse",s.ao.color,0);bindTexture(d,"tDepth",s.resolved.depth,1);bindTexture(d,"tNoise",s.pdNoise,2);
        d.uniform("resolution",{double(s.width),double(s.height)});d.uniform("cameraProjectionMatrixInverse",matrix(glm::inverse(s.projection)));d.uniform("lumaPhi",10.);d.uniform("depthPhi",2.);d.uniform("normalPhi",3.);d.uniform("radius",6.);d.uniform("index",0.);s.draw();draws++;
        s.target(s.ping);s.mul->use();bindTexture(*s.mul,"scene",current,0);bindTexture(*s.mul,"ao",s.filtered.color,1);s.draw();draws++;current=s.ping.color;
    }
    if(s.useBloom){
        s.target(s.bright);auto& h=*s.highpass;h.use();bindTexture(h,"tDiffuse",current,0);h.uniform("luminosityThreshold",g.bloomThreshold);h.uniform("smoothWidth",.01);h.uniform("defaultColor",{0.,0.,0.});h.uniform("defaultOpacity",0.);s.draw();draws++;
        GLuint input=s.bright.color;
        for(int i=0;i<5;i++){
            auto& b=*s.blur[i];b.use();b.uniform("invSize",{1./s.horizontal[i].width,1./s.horizontal[i].height});
            s.target(s.horizontal[i]);bindTexture(b,"colorTexture",input,0);b.uniform("direction",{1.,0.});s.draw();
            s.target(s.vertical[i]);bindTexture(b,"colorTexture",s.horizontal[i].color,0);b.uniform("direction",{0.,1.});s.draw();draws+=2;input=s.vertical[i].color;
        }
        s.target(s.bloom);auto& c=*s.composite;c.use();for(int i=0;i<5;i++)bindTexture(c,("blurTexture"+std::to_string(i+1)).c_str(),s.vertical[i].color,i);
        c.uniform("bloomStrength",g.bloomStrength);c.uniform("bloomRadius",g.bloomRadius);c.uniform("bloomFactors",{1.,.8,.6,.4,.2});c.uniform("bloomTintColors",std::vector<double>(15,1.));s.draw();draws++;
        auto& next=current==s.ping.color?s.pong:s.ping;s.target(next);s.sum->use();bindTexture(*s.sum,"scene",current,0);bindTexture(*s.sum,"bloom",s.bloom.color,1);s.draw();draws++;current=next.color;
    }
    auto& graded=current==s.ping.color?s.pong:s.ping;s.target(graded);auto& grade=*s.grade;grade.use();bindTexture(grade,"tDiffuse",current,0);
    grade.uniform("uSat",g.saturation);grade.uniform("uVib",g.vibrance);grade.uniform("uContrast",g.contrast);grade.uniform("uLift",g.lift);grade.uniform("uExposure",g.exposure);grade.uniform("uVignette",g.vignette);grade.uniform("uHurt",g.hurt);grade.uniform("uFlash",g.flash);grade.uniform("uAspect",double(s.width)/s.height);
    grade.uniform("uShadowTint",{g.shadowTint.x,g.shadowTint.y,g.shadowTint.z});grade.uniform("uHighTint",{g.highlightTint.x,g.highlightTint.y,g.highlightTint.z});s.draw();draws++;current=graded.color;
    if(f.enabled){
    auto& effected=current==s.ping.color?s.pong:s.ping;s.target(effected);auto& e=*s.screen;e.use();bindTexture(e,"tDiffuse",current,0);bindTexture(e,"tLens",f.lensTexture,1);
    auto v2=[&](const char* n,glm::vec2 v){e.uniform(n,{v.x,v.y});};auto v3=[&](const char* n,glm::vec3 v){e.uniform(n,{v.x,v.y,v.z});};auto v4=[&](const char* n,glm::vec4 v){e.uniform(n,{v.x,v.y,v.z,v.w});};
    v2("uRes",{s.width,s.height});e.uniform("uAspect",double(s.width)/s.height);e.uniform("uTime",f.time);e.uniform("uSpeed",f.speed);v3("uSpeedTint",f.speedTint);e.uniform("uStretch",f.stretch);e.uniform("uPunch",f.punch);v2("uPunchPos",f.punchPos);e.uniform("uBlast",f.blast);v2("uBlastPos",f.blastPos);e.uniform("uBlastRing",f.blastRing);v3("uBlastColor",f.blastColor);e.uniform("uChroma",f.chroma);e.uniform("uLensOn",f.lensTexture?1.:0.);v2("uLensTexel",f.lensTexel);v3("uLensColA",f.lensColorA);v3("uLensColB",f.lensColorB);
    v4("uEdgeInk",f.edgeInk);v4("uAura",f.aura);v4("uHeart",f.heart);e.uniform("uHurt",f.hurt);v4("uUrgency",f.urgency);v4("uSwim",f.swim);e.uniform("uFocus",f.focus);v4("uShimmer",f.shimmer);v4("uFlood",f.flood);e.uniform("uFloodDrip",f.floodDrip);e.uniform("uFloodClear",f.floodClear);e.uniform("uHole",f.hole);v3("uHoleRim",f.holeRim);v4("uFlash",f.flash);e.uniform("uDesat",f.desaturation);e.uniform("uSat",f.saturation);v4("uKill",f.kill);v4("uCharge",f.charge);s.draw();draws++;
    current=effected.color;
    }
    if(s.useFxaa){
        auto& antialiased=current==s.ping.color?s.pong:s.ping;s.target(antialiased);s.fxaa->use();bindTexture(*s.fxaa,"tDiffuse",current,0);s.fxaa->uniform("resolution",{double(s.width),double(s.height)});s.draw();draws++;current=antialiased.color;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,outputWidth>0?outputWidth:s.width,outputHeight>0?outputHeight:s.height);s.output->use();bindTexture(*s.output,"tDiffuse",current,0);s.output->uniform("toneMappingExposure",1.);s.draw();draws++;
    glActiveTexture(GL_TEXTURE0);s.active=false;return draws;
}
}
