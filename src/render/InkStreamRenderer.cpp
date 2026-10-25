#include "InkStreamRenderer.hpp"
#include "CharacterShader.hpp"
#include "TeamPalette.hpp"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace restoration {
namespace {
constexpr int TubeSides=8, SdfNodeLimit=48;
constexpr float Pi=3.14159265358979323846f;
struct Point { glm::vec3 p; float r; };
struct Vertex { float p[3],n[3],c[3]; };
struct SphereVertex { float p[3],n[3]; };
struct Instance { float centerRadius[4],color[3]; };
struct Range { GLint first=0,count=0; };
struct GpuPiece {
    int team=0;
    glm::vec3 boundsMin{0},boundsMax{0};
    std::vector<Point> points;
    std::vector<Vertex> tube;
    std::vector<float> sdf;
    std::vector<float> drops;
};

glm::vec3 unit(glm::vec3 v,glm::vec3 fallback={0,1,0}) {
    const float d=glm::dot(v,v);return d>1e-12f&&std::isfinite(d)?v/std::sqrt(d):fallback;
}
bool finite(glm::vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
glm::vec3 teamColor(int team) { return glm::vec3(InkColors[team==1?1:0]); }
std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot read ink stream shader: "+path.string());
    return {std::istreambuf_iterator<char>(file),{}};
}
GLuint compile(GLenum type,const std::string& source) {
    const GLuint shader=glCreateShader(type);const char* text=source.c_str();
    glShaderSource(shader,1,&text,nullptr);glCompileShader(shader);GLint okay=GL_FALSE;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&okay);
    if(!okay){GLint length=0;glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&length);std::string log(std::max(length,1),0);glGetShaderInfoLog(shader,length,nullptr,log.data());glDeleteShader(shader);throw std::runtime_error("Ink stream shader compile: "+log);}
    return shader;
}
GLuint program(const std::filesystem::path& assets,const char* vertex,const char* fragment) {
    const GLuint vs=compile(GL_VERTEX_SHADER,readText(assets/vertex));GLuint fs=0;
    try{fs=compile(GL_FRAGMENT_SHADER,readText(assets/fragment));}catch(...){glDeleteShader(vs);throw;}
    const GLuint id=glCreateProgram();glAttachShader(id,vs);glAttachShader(id,fs);glLinkProgram(id);glDeleteShader(vs);glDeleteShader(fs);
    GLint okay=GL_FALSE;glGetProgramiv(id,GL_LINK_STATUS,&okay);
    if(!okay){GLint length=0;glGetProgramiv(id,GL_INFO_LOG_LENGTH,&length);std::string log(std::max(length,1),0);glGetProgramInfoLog(id,length,nullptr,log.data());glDeleteProgram(id);throw std::runtime_error("Ink stream program link: "+log);}
    return id;
}
GLint location(GLuint p,const char* name) { return glGetUniformLocation(p,name); }
void mat4(GLuint p,const char* name,const glm::mat4& m) { const GLint at=location(p,name);if(at>=0)glUniformMatrix4fv(at,1,GL_FALSE,glm::value_ptr(m)); }
void mat3(GLuint p,const char* name,const glm::mat3& m) { const GLint at=location(p,name);if(at>=0)glUniformMatrix3fv(at,1,GL_FALSE,glm::value_ptr(m)); }
void vec2(GLuint p,const char* name,float x,float y) { const GLint at=location(p,name);if(at>=0)glUniform2f(at,x,y); }
void vec3(GLuint p,const char* name,glm::vec3 v) { const GLint at=location(p,name);if(at>=0)glUniform3fv(at,1,glm::value_ptr(v)); }
void vec3d(GLuint p,const char* name,glm::dvec3 v) { vec3(p,name,glm::vec3(v)); }
void scalar(GLuint p,const char* name,float v) { const GLint at=location(p,name);if(at>=0)glUniform1f(at,v); }
void integer(GLuint p,const char* name,int v) { const GLint at=location(p,name);if(at>=0)glUniform1i(at,v); }

class State {
public:
    GLint program=0,vao=0,array=0,activeTexture=GL_TEXTURE0,texture0=0,texture1=0,drawFbo=0,readFbo=0,viewport[4]{},depthFunc=GL_LESS;
    GLint blendSrcRgb=GL_ONE,blendDstRgb=GL_ZERO,blendSrcAlpha=GL_ONE,blendDstAlpha=GL_ZERO,blendEqRgb=GL_FUNC_ADD,blendEqAlpha=GL_FUNC_ADD;
    GLint scissorBox[4]{};GLdouble depthRange[2]{0,1};GLboolean depthMask=GL_TRUE,colorMask[4]{GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE};
    GLboolean depth=GL_FALSE,blend=GL_FALSE,cull=GL_FALSE,scissor=GL_FALSE;
    State() {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);
        glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);glGetDoublev(GL_DEPTH_RANGE,depthRange);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);glGetBooleanv(GL_COLOR_WRITEMASK,colorMask);
        glGetIntegerv(GL_BLEND_SRC_RGB,&blendSrcRgb);glGetIntegerv(GL_BLEND_DST_RGB,&blendDstRgb);glGetIntegerv(GL_BLEND_SRC_ALPHA,&blendSrcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&blendDstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB,&blendEqRgb);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&blendEqAlpha);glGetIntegerv(GL_SCISSOR_BOX,scissorBox);
        depth=glIsEnabled(GL_DEPTH_TEST);blend=glIsEnabled(GL_BLEND);cull=glIsEnabled(GL_CULL_FACE);scissor=glIsEnabled(GL_SCISSOR_TEST);
        glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture0);glActiveTexture(GL_TEXTURE1);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture1);glActiveTexture(activeTexture);
    }
    ~State() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(drawFbo));glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(readFbo));
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glDepthRange(depthRange[0],depthRange[1]);glDepthFunc(static_cast<GLenum>(depthFunc));glDepthMask(depthMask);glColorMask(colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
        glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb),static_cast<GLenum>(blendDstRgb),static_cast<GLenum>(blendSrcAlpha),static_cast<GLenum>(blendDstAlpha));glBlendEquationSeparate(static_cast<GLenum>(blendEqRgb),static_cast<GLenum>(blendEqAlpha));
        restore(GL_DEPTH_TEST,depth);restore(GL_BLEND,blend);restore(GL_CULL_FACE,cull);restore(GL_SCISSOR_TEST,scissor);glScissor(scissorBox[0],scissorBox[1],scissorBox[2],scissorBox[3]);
        glUseProgram(static_cast<GLuint>(program));glBindVertexArray(static_cast<GLuint>(vao));glBindBuffer(GL_ARRAY_BUFFER,static_cast<GLuint>(array));
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(texture0));glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(texture1));glActiveTexture(static_cast<GLenum>(activeTexture));
    }
private:
    static void restore(GLenum cap,GLboolean on){if(on)glEnable(cap);else glDisable(cap);}
};

void push(Vertex& v,glm::vec3 p,glm::vec3 n,glm::vec3 c) {
    for(int i=0;i<3;++i){v.p[i]=p[i];v.n[i]=n[i];v.c[i]=c[i];}
}
std::vector<Vertex> buildTube(const std::vector<Point>& points,glm::vec3 color) {
    std::vector<Vertex> out;if(points.size()<2)return out;
    const std::size_t n=points.size();std::vector<float> radius(n);std::vector<glm::vec3> tangent(n),normal(n),binormal(n);
    for(std::size_t i=0;i<n;++i){const float a=points[i?i-1:i].r,b=points[i].r,c=points[std::min(i+1,n-1)].r;
        radius[i]=b<0.025f?b:0.25f*a+0.5f*b+0.25f*c;}
    for(std::size_t i=0;i<n;++i){const glm::vec3 delta=points[std::min(i+1,n-1)].p-points[i?i-1:i].p;tangent[i]=unit(delta,i?tangent[i-1]:glm::vec3(0,1,0));}
    glm::vec3 axis=std::abs(tangent[0].x)<std::abs(tangent[0].y)?glm::vec3(1,0,0):glm::vec3(0,1,0);
    if(std::abs(glm::dot(axis,tangent[0]))>0.9f)axis={0,0,1};
    normal[0]=unit(glm::cross(tangent[0],axis),{1,0,0});binormal[0]=unit(glm::cross(tangent[0],normal[0]),{0,0,1});
    for(std::size_t i=1;i<n;++i){
        const glm::vec3 axisCross=glm::cross(tangent[i-1],tangent[i]);const float s=glm::length(axisCross),c=std::clamp(glm::dot(tangent[i-1],tangent[i]),-1.0f,1.0f);
        glm::vec3 next=normal[i-1];
        if(s>1e-6f){const glm::vec3 a=axisCross/s;next=next*c+glm::cross(a,next)*s+a*glm::dot(a,next)*(1-c);}
        next-=tangent[i]*glm::dot(next,tangent[i]);normal[i]=unit(next,normal[i-1]);binormal[i]=unit(glm::cross(tangent[i],normal[i]),binormal[i-1]);
    }
    using Ring=std::array<Vertex,TubeSides>;std::vector<Ring> rings;rings.reserve(n+8);
    const auto ring=[&](glm::vec3 center,glm::vec3 t,glm::vec3 u,glm::vec3 v,float r,float phi,float capSign){
        Ring q;for(int j=0;j<TubeSides;++j){const float angle=2*Pi*j/TubeSides;const glm::vec3 radial=std::cos(angle)*u+std::sin(angle)*v;
            const glm::vec3 position=center+t*(capSign*r*std::cos(phi))+radial*(r*std::sin(phi));
            const glm::vec3 norm=unit(radial*std::sin(phi)+t*(capSign*std::cos(phi)),radial);push(q[j],position,norm,color);}rings.push_back(q);
    };
    for(int i=0;i<4;++i)ring(points.front().p,tangent.front(),normal.front(),binormal.front(),radius.front(),Pi*0.5f*i/4,-1);
    for(std::size_t i=0;i<n;++i)ring(points[i].p,tangent[i],normal[i],binormal[i],radius[i],Pi*0.5f,0);
    for(int i=3;i>=0;--i)ring(points.back().p,tangent.back(),normal.back(),binormal.back(),radius.back(),Pi*0.5f*i/4,1);
    out.reserve((rings.size()-1)*TubeSides*6);
    for(std::size_t i=0;i+1<rings.size();++i)for(int j=0;j<TubeSides;++j){const int k=(j+1)%TubeSides;const Vertex a=rings[i][j],b=rings[i][k],c=rings[i+1][j],d=rings[i+1][k];out.insert(out.end(),{a,c,b,b,c,d});}
    return out;
}
std::vector<Point> resample(const std::vector<Point>& points,int limit) {
    if(points.size()<=static_cast<std::size_t>(limit))return points;
    std::vector<float> distance(points.size(),0);for(std::size_t i=1;i<points.size();++i)distance[i]=distance[i-1]+glm::distance(points[i-1].p,points[i].p);
    if(distance.back()<1e-6f)return {points.front(),points.back()};
    std::vector<Point> out;out.reserve(limit);std::size_t segment=1;
    for(int i=0;i<limit;++i){const float target=distance.back()*i/(limit-1);while(segment+1<distance.size()&&distance[segment]<target)++segment;
        const float span=distance[segment]-distance[segment-1],t=span>1e-8f?(target-distance[segment-1])/span:0;
        out.push_back({glm::mix(points[segment-1].p,points[segment].p,t),glm::mix(points[segment-1].r,points[segment].r,t)});}
    return out;
}
std::vector<SphereVertex> makeSphere() {
    constexpr int slices=12,stacks=8;std::vector<SphereVertex> out;out.reserve(slices*stacks*6);
    const auto vertex=[](float u,float v){const float phi=Pi*v,theta=2*Pi*u;const glm::vec3 n{std::sin(phi)*std::cos(theta),std::cos(phi),std::sin(phi)*std::sin(theta)};return SphereVertex{{n.x,n.y,n.z},{n.x,n.y,n.z}};};
    for(int y=0;y<stacks;++y)for(int x=0;x<slices;++x){const float u=float(x)/slices,v=float(y)/stacks,du=1.0f/slices,dv=1.0f/stacks;const auto a=vertex(u,v),b=vertex(u+du,v),c=vertex(u,v+dv),d=vertex(u+du,v+dv);out.insert(out.end(),{a,c,b,b,c,d});}
    return out;
}
Instance instance(Point p,glm::vec3 color) { return {{p.p.x,p.p.y,p.p.z,p.r},{color.r,color.g,color.b}}; }
void uniformMatrices(GLuint p,const glm::mat4& view,const glm::mat4& projection) {
    mat4(p,"uView",view);mat4(p,"uProjection",projection);mat3(p,"uNormalMatrix",glm::transpose(glm::inverse(glm::mat3(view))));
}
void uniformLights(GLuint p,const CharacterLightState& light,const glm::mat4& view) {
    const glm::dmat3 rotation(view);vec3d(p,"ambientLightColor",light.ambient);
    auto direction=[&](glm::dvec3 v){v=rotation*v;return glm::length(v)>1e-10?glm::normalize(v):glm::dvec3(0,1,0);};
    vec3d(p,"directionalLights[0].direction",direction(glm::dvec3(light.sunDirection)));vec3d(p,"directionalLights[0].color",light.sunColor);
    vec3d(p,"hemisphereLights[0].direction",direction(glm::dvec3(light.skyDirection)));vec3d(p,"hemisphereLights[0].skyColor",light.skyColor);vec3d(p,"hemisphereLights[0].groundColor",light.groundColor);
    vec3d(p,"fogColor",light.fogColor);scalar(p,"fogNear",float(light.fogNear));scalar(p,"fogFar",float(light.fogFar));scalar(p,"toneMappingExposure",float(light.exposure));
}
}

struct InkStreamRenderer::Impl {
    explicit Impl(std::filesystem::path path):assets(std::move(path)){}
    ~Impl(){
        for(GLuint p:{tubeProgram,sphereProgram,sampleProgram,sdfProgram,compositeProgram})if(p)glDeleteProgram(p);
        for(GLuint o:{tubeVao,sphereVao,fullscreenVao})if(o)glDeleteVertexArrays(1,&o);
        for(GLuint b:{tubeBuffer,sphereMeshBuffer,tubeInstanceBuffer,screenInstanceBuffer})if(b)glDeleteBuffers(1,&b);
        if(screenFbo)glDeleteFramebuffers(1,&screenFbo);if(screenColor)glDeleteTextures(1,&screenColor);if(screenDepth)glDeleteTextures(1,&screenDepth);
    }
    std::filesystem::path assets;
    GLuint tubeProgram=0,sphereProgram=0,sampleProgram=0,sdfProgram=0,compositeProgram=0;
    GLuint tubeVao=0,sphereVao=0,fullscreenVao=0,tubeBuffer=0,sphereMeshBuffer=0,tubeInstanceBuffer=0,screenInstanceBuffer=0;
    GLuint screenFbo=0,screenColor=0,screenDepth=0;int screenWidth=0,screenHeight=0,sphereVertexCount=0;
    bool gpuReady=false,tubeDirty=true,tubeInstancesDirty=true,screenInstancesDirty=true;
    std::vector<GpuPiece> pieces;std::vector<Range> tubeRanges;std::vector<Instance> tubeInstances,screenInstances;

    void update(const std::vector<InkStreamPiece>& source,const std::vector<InkStreamDrop>& drops) {
        pieces.clear();tubeInstances.clear();screenInstances.clear();pieces.reserve(source.size());
        for(const auto& piece:source){GpuPiece p;p.team=piece.team==1?1:0;
            for(const auto& point:piece.points)if(finite(point.position)&&std::isfinite(point.radius)&&point.radius>=0){p.points.push_back({point.position,point.radius});screenInstances.push_back(instance(p.points.back(),teamColor(p.team)));}
            if(p.points.empty())continue;
            glm::vec3 lo=p.points.front().p,hi=lo;for(const auto& point:p.points){lo=glm::min(lo,point.p-glm::vec3(point.r));hi=glm::max(hi,point.p+glm::vec3(point.r));}
            p.boundsMin=lo-glm::vec3(0.06f);p.boundsMax=hi+glm::vec3(0.06f);
            if(p.points.size()==1)tubeInstances.push_back(instance(p.points.front(),teamColor(p.team)));
            else {p.tube=buildTube(p.points,teamColor(p.team));const auto nodes=resample(p.points,SdfNodeLimit);p.sdf.reserve(nodes.size()*4);for(const auto& node:nodes){p.sdf.insert(p.sdf.end(),{node.p.x,node.p.y,node.p.z,node.r});}}
            pieces.push_back(std::move(p));
        }
        for(const auto& drop:drops)if(finite(drop.position)&&std::isfinite(drop.radius)&&drop.radius>0){const float fade=std::clamp((drop.life-drop.age)/.08f,0.f,1.f);if(fade<=0)continue;const Point p{drop.position,drop.radius*fade};const glm::vec3 color=teamColor(drop.team);const auto item=instance(p,color);tubeInstances.push_back(item);screenInstances.push_back(item);}
        for(auto& piece:pieces)if(!piece.sdf.empty()){
            const auto lo=piece.boundsMin,hi=piece.boundsMax;
            for(const auto& drop:drops){
                if(piece.drops.size()>=16*4)break;
                if((drop.team==1?1:0)!=piece.team||!finite(drop.position)||!std::isfinite(drop.radius)||drop.radius<=0)continue;
                const float radius=drop.radius*std::clamp((drop.life-drop.age)/.08f,0.f,1.f);
                const auto delta=drop.position-glm::clamp(drop.position,lo,hi);
                if(radius<=0||glm::length(delta)>radius+.04f)continue;
                piece.drops.insert(piece.drops.end(),{drop.position.x,drop.position.y,drop.position.z,radius});
                piece.boundsMin=glm::min(piece.boundsMin,drop.position-glm::vec3(radius+.04f));piece.boundsMax=glm::max(piece.boundsMax,drop.position+glm::vec3(radius+.04f));
            }
        }
        tubeDirty=tubeInstancesDirty=screenInstancesDirty=true;
    }
    void ensurePrograms(InkStreamMode mode){
        try{
            if(!tubeProgram)tubeProgram=program(assets,"tube.vert","tube.frag");
            if(!sphereProgram)sphereProgram=program(assets,"sphere.vert","tube.frag");
            if(mode==InkStreamMode::Sdf&&!sdfProgram)sdfProgram=program(assets,"fullscreen.vert","sdf.frag");
            if(mode==InkStreamMode::ScreenSpace&&!sampleProgram)sampleProgram=program(assets,"sphere.vert","sample.frag");
            if(mode==InkStreamMode::ScreenSpace&&!compositeProgram)compositeProgram=program(assets,"fullscreen.vert","composite.frag");
        }
        catch(...){for(GLuint* p:{&tubeProgram,&sphereProgram,&sampleProgram,&sdfProgram,&compositeProgram})if(*p){glDeleteProgram(*p);*p=0;}throw;}
    }
    void ensureGeometry(){
        if(gpuReady)return;
        glGenVertexArrays(1,&tubeVao);glGenBuffers(1,&tubeBuffer);glBindVertexArray(tubeVao);glBindBuffer(GL_ARRAY_BUFFER,tubeBuffer);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,p)));
        glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,n)));
        glEnableVertexAttribArray(2);glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,c)));
        const auto sphere=makeSphere();sphereVertexCount=static_cast<int>(sphere.size());glGenBuffers(1,&sphereMeshBuffer);glBindBuffer(GL_ARRAY_BUFFER,sphereMeshBuffer);glBufferData(GL_ARRAY_BUFFER,sphere.size()*sizeof(SphereVertex),sphere.data(),GL_STATIC_DRAW);
        glGenVertexArrays(1,&sphereVao);glBindVertexArray(sphereVao);glBindBuffer(GL_ARRAY_BUFFER,sphereMeshBuffer);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(SphereVertex),reinterpret_cast<void*>(offsetof(SphereVertex,p)));
        glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(SphereVertex),reinterpret_cast<void*>(offsetof(SphereVertex,n)));
        glGenBuffers(1,&tubeInstanceBuffer);glGenBuffers(1,&screenInstanceBuffer);bindInstances(tubeInstanceBuffer);
        glGenVertexArrays(1,&fullscreenVao);gpuReady=true;
    }
    void bindInstances(GLuint buffer){
        glBindVertexArray(sphereVao);glBindBuffer(GL_ARRAY_BUFFER,buffer);
        glEnableVertexAttribArray(3);glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,sizeof(Instance),reinterpret_cast<void*>(offsetof(Instance,centerRadius)));glVertexAttribDivisor(3,1);
        glEnableVertexAttribArray(4);glVertexAttribPointer(4,3,GL_FLOAT,GL_FALSE,sizeof(Instance),reinterpret_cast<void*>(offsetof(Instance,color)));glVertexAttribDivisor(4,1);
    }
    void upload(InkStreamMode mode,bool shadow){
        if(tubeDirty&&(shadow||mode==InkStreamMode::Tube)){std::vector<Vertex> data;tubeRanges.clear();tubeRanges.reserve(pieces.size());
            for(const auto& p:pieces){const GLint first=static_cast<GLint>(data.size());data.insert(data.end(),p.tube.begin(),p.tube.end());tubeRanges.push_back({first,static_cast<GLint>(p.tube.size())});}
            glBindBuffer(GL_ARRAY_BUFFER,tubeBuffer);glBufferData(GL_ARRAY_BUFFER,data.size()*sizeof(Vertex),data.empty()?nullptr:data.data(),GL_DYNAMIC_DRAW);tubeDirty=false;}
        if(tubeInstancesDirty&&(shadow||mode!=InkStreamMode::ScreenSpace)){glBindBuffer(GL_ARRAY_BUFFER,tubeInstanceBuffer);glBufferData(GL_ARRAY_BUFFER,tubeInstances.size()*sizeof(Instance),tubeInstances.empty()?nullptr:tubeInstances.data(),GL_DYNAMIC_DRAW);tubeInstancesDirty=false;}
        if(screenInstancesDirty&&!shadow&&mode==InkStreamMode::ScreenSpace){glBindBuffer(GL_ARRAY_BUFFER,screenInstanceBuffer);glBufferData(GL_ARRAY_BUFFER,screenInstances.size()*sizeof(Instance),screenInstances.empty()?nullptr:screenInstances.data(),GL_DYNAMIC_DRAW);screenInstancesDirty=false;}
    }
    void ensureScreen(int width,int height){
        if(screenFbo&&screenWidth==width&&screenHeight==height)return;
        glActiveTexture(GL_TEXTURE0);
        if(screenFbo)glDeleteFramebuffers(1,&screenFbo);if(screenColor)glDeleteTextures(1,&screenColor);if(screenDepth)glDeleteTextures(1,&screenDepth);
        screenWidth=width;screenHeight=height;glGenFramebuffers(1,&screenFbo);glBindFramebuffer(GL_FRAMEBUFFER,screenFbo);
        glGenTextures(1,&screenColor);glBindTexture(GL_TEXTURE_2D,screenColor);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,width,height,0,GL_RGBA,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,screenColor,0);
        glGenTextures(1,&screenDepth);glBindTexture(GL_TEXTURE_2D,screenDepth);glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT32F,width,height,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,screenDepth,0);glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Ink stream screen-space framebuffer is incomplete");
    }
    int drawTube(const glm::mat4& view,const glm::mat4& projection,const CharacterLightState& light,bool shadow){
        int calls=0;glUseProgram(tubeProgram);uniformMatrices(tubeProgram,view,projection);uniformLights(tubeProgram,light,view);integer(tubeProgram,"uShadow",shadow?1:0);glBindVertexArray(tubeVao);
        if(!tubeRanges.empty()){const auto end=tubeRanges.back().first+tubeRanges.back().count;if(end>0){glDrawArrays(GL_TRIANGLES,0,end);++calls;}}
        if(!tubeInstances.empty()){drawSpheres(tubeInstances,tubeInstanceBuffer,sphereProgram,view,projection,light,shadow);++calls;}
        return calls;
    }
    void drawSpheres(const std::vector<Instance>& items,GLuint buffer,GLuint p,const glm::mat4& view,const glm::mat4& projection,const CharacterLightState& light,bool shadow){
        if(items.empty())return;glUseProgram(p);uniformMatrices(p,view,projection);uniformLights(p,light,view);integer(p,"uShadow",shadow?1:0);bindInstances(buffer);glDrawArraysInstanced(GL_TRIANGLES,0,sphereVertexCount,static_cast<GLsizei>(items.size()));
    }
    bool scissorPiece(const GpuPiece& piece,const glm::mat4& viewProjection,const GLint* viewport){
        float minX=1,minY=1,maxX=-1,maxY=-1;bool uncertain=false,allFar=true;
        for(int x=0;x<2;++x)for(int y=0;y<2;++y)for(int z=0;z<2;++z){const glm::vec3 corner{x?piece.boundsMax.x:piece.boundsMin.x,y?piece.boundsMax.y:piece.boundsMin.y,z?piece.boundsMax.z:piece.boundsMin.z};const glm::vec4 c=viewProjection*glm::vec4(corner,1);
            if(c.w<=1e-5f||c.z<=-c.w)uncertain=true;if(c.w<=0||c.z<=c.w)allFar=false;if(!uncertain){const float nx=c.x/c.w,ny=c.y/c.w;minX=std::min(minX,nx);maxX=std::max(maxX,nx);minY=std::min(minY,ny);maxY=std::max(maxY,ny);}}
        if(allFar)return false;
        int x=viewport[0],y=viewport[1],w=viewport[2],h=viewport[3];
        if(!uncertain){x+=static_cast<int>(std::floor((std::clamp(minX,-1.0f,1.0f)*0.5f+0.5f)*w));y+=static_cast<int>(std::floor((std::clamp(minY,-1.0f,1.0f)*0.5f+0.5f)*h));
            const int x2=viewport[0]+static_cast<int>(std::ceil((std::clamp(maxX,-1.0f,1.0f)*0.5f+0.5f)*w)),y2=viewport[1]+static_cast<int>(std::ceil((std::clamp(maxY,-1.0f,1.0f)*0.5f+0.5f)*h));w=x2-x;h=y2-y;if(w<=0||h<=0)return false;}
        glEnable(GL_SCISSOR_TEST);glScissor(x,y,w,h);return true;
    }
    int drawSdf(const glm::mat4& view,const glm::mat4& projection,glm::vec3 eye,const CharacterLightState& light,const State& state){
        const glm::mat4 vp=projection*view;int calls=0;glUseProgram(sdfProgram);uniformLights(sdfProgram,light,view);mat4(sdfProgram,"uView",view);mat4(sdfProgram,"uProjection",projection);
        mat4(sdfProgram,"uInverseView",glm::inverse(view));mat4(sdfProgram,"uInverseProjection",glm::inverse(projection));vec3(sdfProgram,"uEye",eye);
        integer(sdfProgram,"uOrthographic",std::abs(projection[3][3])>0.5f);vec2(sdfProgram,"uDepthRange",float(state.depthRange[0]),float(state.depthRange[1]));glBindVertexArray(fullscreenVao);
        // shortcut: SDF samples at most 48 arc-resampled nodes per piece; use a GPU buffer for longer hero streams.
        for(const auto& p:pieces)if(p.sdf.size()>=8&&scissorPiece(p,vp,state.viewport)){
            integer(sdfProgram,"uCount",static_cast<int>(p.sdf.size()/4));const GLint nodes=location(sdfProgram,"uNodes[0]");if(nodes>=0)glUniform4fv(nodes,static_cast<GLsizei>(p.sdf.size()/4),p.sdf.data());
            integer(sdfProgram,"uDropCount",static_cast<int>(p.drops.size()/4));const GLint drops=location(sdfProgram,"uDrops[0]");if(drops>=0&&!p.drops.empty())glUniform4fv(drops,static_cast<GLsizei>(p.drops.size()/4),p.drops.data());
            vec3(sdfProgram,"uBoundsMin",p.boundsMin);vec3(sdfProgram,"uBoundsMax",p.boundsMax);vec3(sdfProgram,"uInkColor",teamColor(p.team));glDrawArrays(GL_TRIANGLES,0,3);++calls;}
        return calls;
    }
    int drawScreen(const glm::mat4& view,const glm::mat4& projection,const CharacterLightState& light,const State& state){
        if(screenInstances.empty())return 0;
        ensureScreen(state.viewport[2],state.viewport[3]);glDisable(GL_SCISSOR_TEST);glDepthRange(0,1);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glEnable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
        glBindFramebuffer(GL_FRAMEBUFFER,screenFbo);glViewport(0,0,screenWidth,screenHeight);glDrawBuffer(GL_COLOR_ATTACHMENT0);const GLfloat clearColor[4]={0,0,0,0};const GLfloat clearDepth=1.0f;
        glClearBufferfv(GL_COLOR,0,clearColor);glClearBufferfv(GL_DEPTH,0,&clearDepth);drawSpheres(screenInstances,screenInstanceBuffer,sampleProgram,view,projection,light,false);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(state.drawFbo));glViewport(state.viewport[0],state.viewport[1],state.viewport[2],state.viewport[3]);glDepthRange(state.depthRange[0],state.depthRange[1]);glDepthFunc(static_cast<GLenum>(state.depthFunc));
        glDepthMask(GL_TRUE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glEnable(GL_DEPTH_TEST);
        glUseProgram(compositeProgram);uniformLights(compositeProgram,light,view);mat4(compositeProgram,"uProjection",projection);mat4(compositeProgram,"uInverseProjection",glm::inverse(projection));
        integer(compositeProgram,"uOrthographic",std::abs(projection[3][3])>0.5f);vec2(compositeProgram,"uSize",float(screenWidth),float(screenHeight));vec2(compositeProgram,"uDepthRange",float(state.depthRange[0]),float(state.depthRange[1]));
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,screenColor);integer(compositeProgram,"uColorMask",0);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,screenDepth);integer(compositeProgram,"uDepthTexture",1);
        glBindVertexArray(fullscreenVao);glDrawArrays(GL_TRIANGLES,0,3);return 2;
    }
};

InkStreamRenderer::InkStreamRenderer(const std::filesystem::path& assets):impl_(std::make_unique<Impl>(assets)){}
InkStreamRenderer::~InkStreamRenderer()=default;
void InkStreamRenderer::Update(const std::vector<InkStreamPiece>& pieces,const std::vector<InkStreamDrop>& drops){impl_->update(pieces,drops);}
int InkStreamRenderer::Draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& light,InkStreamMode mode,bool shadow){
    if(impl_->pieces.empty()&&impl_->tubeInstances.empty()&&impl_->screenInstances.empty())return 0;
    State state;impl_->ensurePrograms(shadow?InkStreamMode::Tube:mode);impl_->ensureGeometry();impl_->upload(mode,shadow);const glm::mat4 v(view),p(projection);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    if(shadow)glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    if(shadow||mode==InkStreamMode::Tube)return impl_->drawTube(v,p,light,shadow);
    if(mode==InkStreamMode::Sdf){const int sdf=impl_->drawSdf(v,p,glm::vec3(eye),light,state);if(state.scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);glScissor(state.scissorBox[0],state.scissorBox[1],state.scissorBox[2],state.scissorBox[3]);if(impl_->tubeInstances.empty())return sdf;impl_->drawSpheres(impl_->tubeInstances,impl_->tubeInstanceBuffer,impl_->sphereProgram,v,p,light,false);return sdf+1;}
    if(mode==InkStreamMode::ScreenSpace)return impl_->drawScreen(v,p,light,state);
    return impl_->drawTube(v,p,light,false);
}
} // namespace restoration
