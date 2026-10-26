#include "LobbyEffects.hpp"

#include "CharacterShader.hpp"
#include "TeamPalette.hpp"
#include "WorldFxParticleRenderer.hpp"
#include "../game/LobbyPresentation.hpp"

#include <glad/glad.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace restoration {
namespace {
constexpr double kPi=3.14159265358979323846,kTau=2.0*kPi,kTrailGap=0.07;
constexpr std::size_t kTrailCapacity=520,kSplatCapacity=56,kSparkCapacity=36,kParticleCapacity=2048;

double clamp01(double x){return std::clamp(x,0.0,1.0);}
double easeOut3(double x){x=clamp01(x);return 1.0-(1.0-x)*(1.0-x)*(1.0-x);}
double smoothstep01(double x){x=clamp01(x);return x*x*(3.0-2.0*x);}
double backOut(double x,double overshoot){x=clamp01(x)-1.0;return 1.0+x*x*((overshoot+1.0)*x+overshoot);}
double random01(std::uint32_t& state){state^=state<<13;state^=state>>17;state^=state<<5;return double(state)/4294967296.0;}
glm::vec3 store(glm::dvec3 x){return {float(x.x),float(x.y),float(x.z)};}
glm::dvec3 teamColor(int team){return InkColors[team==1?1:0];}

WorldFxParticle makeDrop(glm::dvec3 position,glm::dvec3 velocity,glm::dvec3 color,double size,
                         double life,double gravity,double stretch,std::uint32_t& random){
    WorldFxParticle item;item.shape=WorldFxShape::Drop;item.pos=store(position);item.vel=store(velocity);
    item.color=store(color);item.radius=float(size);item.life=float(life);item.gravity=float(gravity);
    item.stretch=float(stretch);item.seed=float(random01(random));item.renderRadius=item.radius;
    item.renderLength=1.0f;return item;
}

void sourceRipple(std::vector<WorldFxParticle>& out,glm::dvec3 position,glm::dvec3 color,
                  double startRadius,double endRadius,double life){
    WorldFxParticle item;item.shape=WorldFxShape::Ring;item.pos=store(position+glm::dvec3(0.0,0.002,0.0));
    item.normal={0,1,0};item.color=store(color);item.radius=float(endRadius);item.life=float(life);
    item.style=1.0f;item.alpha=1.0f;item.length=0.068f;
    item.expand=float(endRadius>1e-6?startRadius/endRadius:0.0);item.renderRadius=item.radius;
    item.renderAlpha=item.alpha;out.push_back(item);
}

void sourceCrown(std::vector<WorldFxParticle>& out,glm::dvec3 position,glm::dvec3 color,
                 double power,int count,double ringRadius,std::uint32_t& random){
    for(int i=0;i<count;++i){
        const double angle=(double(i)/count)*kTau+random01(random)*0.45;
        const double radius=ringRadius*(0.8+random01(random)*0.6);
        const double outward=(0.5+random01(random)*0.9)*power,up=(1.7+random01(random)*1.7)*power;
        const double r0=random01(random),r1=random01(random),size=0.011+r0*r1*0.026;
        const glm::dvec3 velocity(std::sin(angle)*outward,up,std::cos(angle)*outward);
        if(out.size()<kParticleCapacity)out.push_back(makeDrop(position+glm::dvec3(std::sin(angle)*radius,0.01,std::cos(angle)*radius),
            velocity,color,size,4.0,10.5/17.0,0.9,random));
    }
    sourceRipple(out,position,color,ringRadius*0.8,ringRadius+0.55*power,0.6);
}

const char* domeVertex=R"GLSL(#version 330 core
in vec3 position;
in vec3 normal;
in vec4 iPositionWidth;
in vec4 iShape;
in vec3 iColor;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
out vec3 vNormal;
out vec3 vViewPosition;
out vec3 vColor;
void main(){
    float c=cos(iShape.z),s=sin(iShape.z);
    vec3 local=position*vec3(iPositionWidth.w,iShape.y,iShape.x);
    vec3 world=vec3(c*local.x+s*local.z,local.y,-s*local.x+c*local.z)+iPositionWidth.xyz;
    vec3 n=vec3(c*normal.x+s*normal.z,normal.y,-s*normal.x+c*normal.z);
    n=normalize(vec3(n.x/max(iPositionWidth.w,0.0004),n.y/max(iShape.y,0.0004),n.z/max(iShape.x,0.0004)));
    vec4 viewPosition=viewMatrix*vec4(world,1.0);
    vNormal=normalize(mat3(viewMatrix)*n);
    vViewPosition=viewPosition.xyz;
    vColor=iColor;
    gl_Position=projectionMatrix*viewPosition;
})GLSL";

const char* domeFragment=R"GLSL(#version 330 core
in vec3 vNormal;
in vec3 vViewPosition;
in vec3 vColor;
struct DirectionalLight { vec3 direction; vec3 color; };
struct HemisphereLight { vec3 direction; vec3 skyColor; vec3 groundColor; };
uniform DirectionalLight directionalLights[1];
uniform HemisphereLight hemisphereLights[1];
uniform vec3 ambientLightColor;
uniform sampler2D envMap;
uniform float envMapIntensity;
uniform float uClearcoat;
out vec4 fragmentColor;
void main(){
    vec3 n=normalize(vNormal),v=normalize(-vViewPosition),l=normalize(directionalLights[0].direction);
    float ndl=max(dot(n,l),0.0);
    float hemi=0.5+0.5*dot(n,normalize(hemisphereLights[0].direction));
    vec3 hemiLight=mix(hemisphereLights[0].groundColor,hemisphereLights[0].skyColor,clamp(hemi,0.0,1.0));
    vec3 h=normalize(l+v);
    float spec=pow(max(dot(n,h),0.0),96.0);
    float coat=pow(max(dot(n,h),0.0),320.0);
    float fresnel=0.04+0.96*pow(1.0-max(dot(n,v),0.0),5.0);
    vec3 reflected=reflect(-v,n);
    vec2 uv=vec2(atan(reflected.z,reflected.x)*0.1591549431+0.5,
                 asin(clamp(reflected.y,-1.0,1.0))*0.3183098862+0.5);
    vec3 environment=texture(envMap,uv).rgb*envMapIntensity*0.3;
    vec3 diffuse=vColor*(ambientLightColor+hemiLight*0.48+directionalLights[0].color*ndl*0.58);
    vec3 highlight=directionalLights[0].color*(spec*0.08+coat*fresnel*uClearcoat);
    fragmentColor=vec4(diffuse+highlight+environment*fresnel,1.0);
})GLSL";

const char* sparkleVertex=R"GLSL(#version 330 core
in vec3 position;
in vec4 iPositionSize;
in vec4 iColor;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
out vec2 vUv;
out vec3 vColor;
void main(){
    vUv=position.xy+vec2(0.5);
    vColor=iColor.rgb;
    vec4 mv=viewMatrix*vec4(iPositionSize.xyz,1.0);
    mv.xy+=position.xy*iPositionSize.w;
    gl_Position=projectionMatrix*mv;
})GLSL";

const char* sparkleFragment=R"GLSL(#version 330 core
in vec2 vUv;
in vec3 vColor;
out vec4 fragmentColor;
void main(){
    vec2 p=vUv*2.0-1.0;
    float r=length(p);
    float core=exp(-r*r*26.0);
    float rays=exp(-abs(p.y)*30.0)*pow(max(0.0,1.0-abs(p.x)),2.0)
              +exp(-abs(p.x)*30.0)*pow(max(0.0,1.0-abs(p.y)),2.0);
    float halo=exp(-r*r*5.0)*0.18;
    float alpha=clamp(core*1.3+rays*0.9+halo,0.0,1.0);
    if(alpha<0.004)discard;
    fragmentColor=vec4(vColor*(1.2+3.0*core),alpha);
})GLSL";

struct GlState {
    GLint program=0,vao=0,array=0,activeTexture=GL_TEXTURE0,envTexture=0;
    GLint srcRgb=0,dstRgb=0,srcAlpha=0,dstAlpha=0,eqRgb=0,eqAlpha=0,depthFunc=0;
    GLboolean depthMask=GL_TRUE,blend=GL_FALSE,cull=GL_FALSE,depth=GL_FALSE;
    GlState(){
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array);glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);
        glActiveTexture(GL_TEXTURE9);glGetIntegerv(GL_TEXTURE_BINDING_2D,&envTexture);glActiveTexture(static_cast<GLenum>(activeTexture));
        glGetIntegerv(GL_BLEND_SRC_RGB,&srcRgb);glGetIntegerv(GL_BLEND_DST_RGB,&dstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB,&eqRgb);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&eqAlpha);
        glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
        blend=glIsEnabled(GL_BLEND);cull=glIsEnabled(GL_CULL_FACE);depth=glIsEnabled(GL_DEPTH_TEST);
    }
    ~GlState(){
        glBindVertexArray(static_cast<GLuint>(vao));glBindBuffer(GL_ARRAY_BUFFER,static_cast<GLuint>(array));
        glUseProgram(static_cast<GLuint>(program));glActiveTexture(GL_TEXTURE9);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(envTexture));
        glActiveTexture(static_cast<GLenum>(activeTexture));
        glBlendFuncSeparate(static_cast<GLenum>(srcRgb),static_cast<GLenum>(dstRgb),static_cast<GLenum>(srcAlpha),static_cast<GLenum>(dstAlpha));
        glBlendEquationSeparate(static_cast<GLenum>(eqRgb),static_cast<GLenum>(eqAlpha));glDepthFunc(static_cast<GLenum>(depthFunc));glDepthMask(depthMask);
        auto set=[](GLenum cap,GLboolean enabled){if(enabled)glEnable(cap);else glDisable(cap);};
        set(GL_BLEND,blend);set(GL_CULL_FACE,cull);set(GL_DEPTH_TEST,depth);
    }
};

struct TrailDome {
    glm::dvec3 position{0.0},color{1.0};double yaw=0,width=0.08,length=0.12,age=0,life=1.8;bool active=false;
};
struct SourceSplat {
    glm::dvec3 position{0.0},color{1.0};double yaw=0,size=0.34,age=0,life=2.8;bool active=false;
};
struct Sparkle {
    glm::dvec3 position{0.0},color{1.0};double size=0.1,age=0,life=0.5;bool active=false;
};
struct TrailGpu {std::array<float,4> positionWidth{},shape{};std::array<float,3> color{};float pad=0;};
struct SparkleGpu {std::array<float,4> positionSize{},color{};};

std::vector<float> domeMesh(){
    std::vector<float> vertices;vertices.reserve(16*5*6*6);
    constexpr int columns=16,rows=5;
    const auto point=[=](int x,int y){
        const double phi=kTau*double(x)/columns,theta=(kPi*0.5)*double(y)/rows;
        const double radius=std::sin(theta);return glm::vec3(float(std::cos(phi)*radius),float(std::cos(theta)),float(std::sin(phi)*radius));
    };
    for(int y=0;y<rows;++y)for(int x=0;x<columns;++x){
        const auto a=point(x,y),b=point(x+1,y),c=point(x,y+1),d=point(x+1,y+1);
        for(const auto& v:std::array<glm::vec3,6>{a,c,b,b,c,d})
            vertices.insert(vertices.end(),{v.x,v.y,v.z,v.x,v.y,v.z});
    }
    return vertices;
}

std::vector<float> splatMesh(){
    struct Vertex { glm::vec3 position,normal{0.0f}; };
    std::vector<float> data;
    std::uint32_t random=77;
    const auto sourceRandom=[&](){
        random+=0x6d2b79f5u;
        std::uint32_t value=(random^(random>>15))*(1u|random);
        value^=value+((value^(value>>7))*(61u|value));
        return double(value^(value>>14))/4294967296.0;
    };
    const auto addDome=[&](double cx,double cz,double radius,double amplitude,int columns,int rows,double height){
        const std::array<double,3> phases={sourceRandom()*kTau,sourceRandom()*kTau,sourceRandom()*kTau};
        std::vector<Vertex> vertices;
        vertices.push_back(Vertex{glm::vec3(float(cx),float(height),float(cz)),glm::vec3(0.0f)});
        for(int row=1;row<=rows;++row){
            const double f=double(row)/rows;
            for(int column=0;column<columns;++column){
                const double angle=kTau*double(column)/columns;
                const double wave=1.0+amplitude*(0.55*std::sin(5.0*angle+phases[0])+0.3*std::sin(9.0*angle+phases[1])+0.15*std::sin(14.0*angle+phases[2]));
                const double r=radius*wave*f;
                vertices.push_back(Vertex{glm::vec3(float(cx+std::sin(angle)*r),float(height*std::pow(std::max(0.0,1.0-f*f),0.7)+0.004),float(cz+std::cos(angle)*r)),glm::vec3(0.0f)});
            }
        }
        std::vector<std::array<std::size_t,3>> triangles;
        triangles.reserve(std::size_t(columns)*(1+2*std::max(0,rows-1)));
        for(int column=0;column<columns;++column)
            triangles.push_back({0,1+std::size_t(column),1+std::size_t((column+1)%columns)});
        for(int row=1;row<rows;++row){
            const std::size_t inner=1+std::size_t(row-1)*columns,outer=1+std::size_t(row)*columns;
            for(int column=0;column<columns;++column){
                const auto next=std::size_t((column+1)%columns);
                triangles.push_back({inner+std::size_t(column),outer+std::size_t(column),inner+next});
                triangles.push_back({inner+next,outer+std::size_t(column),outer+next});
            }
        }
        for(const auto& t:triangles){
            const glm::vec3 face=glm::cross(vertices[t[1]].position-vertices[t[0]].position,
                                             vertices[t[2]].position-vertices[t[0]].position);
            for(const auto index:t)vertices[index].normal+=face;
        }
        for(std::size_t i=0;i<vertices.size();++i){
            const float length=glm::length(vertices[i].normal);
            vertices[i].normal=length>1e-8f?vertices[i].normal/length:glm::vec3(0,1,0);
        }
        for(const auto& triangle:triangles)for(const auto index:triangle){
            const auto& v=vertices[index];
            data.insert(data.end(),{v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z});
        }
    };
    addDome(0.0,0.0,1.0,0.2,44,4,0.16);
    for(int i=0;i<5;++i){
        const double angle=sourceRandom()*kTau,distance=1.25+sourceRandom()*0.45;
        addDome(std::sin(angle)*distance,std::cos(angle)*distance,0.1+sourceRandom()*0.12,0.08,12,2,0.06);
    }
    return data;
}

void attribute(const CharacterShader& shader,const char* name,GLint components,GLsizei stride,
               std::size_t offset,bool instance){
    const int location=shader.attribute(name);if(location<0)throw std::runtime_error(std::string("Lobby FX shader is missing attribute: ")+name);
    glEnableVertexAttribArray(static_cast<GLuint>(location));
    glVertexAttribPointer(static_cast<GLuint>(location),components,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<const void*>(offset));
    if(instance)glVertexAttribDivisor(static_cast<GLuint>(location),1);
}
}

struct LobbyEffects::Impl {
    explicit Impl(std::filesystem::path particleAssets):particles(std::move(particleAssets)){}
    ~Impl(){
        if(sparkInstance)glDeleteBuffers(1,&sparkInstance);
        if(sparkVertex)glDeleteBuffers(1,&sparkVertex);
        if(sparkVao)glDeleteVertexArrays(1,&sparkVao);
        if(trailInstance)glDeleteBuffers(1,&trailInstance);
        if(trailVertex)glDeleteBuffers(1,&trailVertex);
        if(trailVao)glDeleteVertexArrays(1,&trailVao);
    }

    enum class BurstTarget { None, Squid, Kid };
    struct ActorFx {
        glm::dvec3 previous{0.0};double trailRemainder=0;std::string form;
        int inkBurst=0;BurstTarget burstTarget=BurstTarget::None;bool initialized=false;
    };
    struct PendingReady {network::MemberId id=0;double remaining=0;};
    struct PendingBurst {network::MemberId id=0;glm::dvec3 position{0.0};double remaining=0;int count=0;};

    WorldFxParticleRenderer particles;
    std::vector<WorldFxParticle> live;
    std::array<TrailDome,kTrailCapacity> trail{};
    std::array<SourceSplat,kSplatCapacity> splats{};
    std::array<Sparkle,kSparkCapacity> sparkles{};
    std::size_t nextTrail=0,nextSplat=0,nextSparkle=0;
    std::unordered_map<network::MemberId,ActorFx> actors;
    std::vector<PendingReady> ready;
    std::vector<PendingBurst> bursts;
    std::uint32_t random=0x5ca1abu;
    glm::dvec3 camera{0.0,10.0,0.0};
    double lastTime=0,lastDt=0;
    std::unique_ptr<CharacterShader> domeShader,sparkShader;
    GLuint trailVao=0,trailVertex=0,trailInstance=0,sparkVao=0,sparkVertex=0,sparkInstance=0;
    GLsizei trailVertexCount=0,splatVertexFirst=0,splatVertexCount=0;
    bool gpuReady=false;

    void initGpu(){
        if(gpuReady)return;
        domeShader=std::make_unique<CharacterShader>(domeVertex,domeFragment,true,false,false);
        sparkShader=std::make_unique<CharacterShader>(sparkleVertex,sparkleFragment,true,false,false);
        auto dome=domeMesh();trailVertexCount=static_cast<GLsizei>(dome.size()/6);
        const auto splat=splatMesh();splatVertexFirst=trailVertexCount;splatVertexCount=static_cast<GLsizei>(splat.size()/6);
        dome.insert(dome.end(),splat.begin(),splat.end());
        glGenVertexArrays(1,&trailVao);glBindVertexArray(trailVao);
        glGenBuffers(1,&trailVertex);glBindBuffer(GL_ARRAY_BUFFER,trailVertex);glBufferData(GL_ARRAY_BUFFER,dome.size()*sizeof(float),dome.data(),GL_STATIC_DRAW);
        attribute(*domeShader,"position",3,6*sizeof(float),0,false);attribute(*domeShader,"normal",3,6*sizeof(float),3*sizeof(float),false);
        glGenBuffers(1,&trailInstance);glBindBuffer(GL_ARRAY_BUFFER,trailInstance);
        glBufferData(GL_ARRAY_BUFFER,kTrailCapacity*sizeof(TrailGpu),nullptr,GL_STREAM_DRAW);
        attribute(*domeShader,"iPositionWidth",4,sizeof(TrailGpu),offsetof(TrailGpu,positionWidth),true);
        attribute(*domeShader,"iShape",4,sizeof(TrailGpu),offsetof(TrailGpu,shape),true);
        attribute(*domeShader,"iColor",3,sizeof(TrailGpu),offsetof(TrailGpu,color),true);

        constexpr std::array<float,18> quad={-0.5f,-0.5f,0,0.5f,-0.5f,0,0.5f,0.5f,0,-0.5f,-0.5f,0,0.5f,0.5f,0,-0.5f,0.5f,0};
        glGenVertexArrays(1,&sparkVao);glBindVertexArray(sparkVao);
        glGenBuffers(1,&sparkVertex);glBindBuffer(GL_ARRAY_BUFFER,sparkVertex);glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad.data(),GL_STATIC_DRAW);
        attribute(*sparkShader,"position",3,3*sizeof(float),0,false);
        glGenBuffers(1,&sparkInstance);glBindBuffer(GL_ARRAY_BUFFER,sparkInstance);
        glBufferData(GL_ARRAY_BUFFER,kSparkCapacity*sizeof(SparkleGpu),nullptr,GL_STREAM_DRAW);
        attribute(*sparkShader,"iPositionSize",4,sizeof(SparkleGpu),offsetof(SparkleGpu,positionSize),true);
        attribute(*sparkShader,"iColor",4,sizeof(SparkleGpu),offsetof(SparkleGpu,color),true);
        glBindVertexArray(0);gpuReady=true;
    }

    void append(std::vector<WorldFxParticle> items){
        for(auto& item:items){if(live.size()>=kParticleCapacity)break;live.push_back(std::move(item));}
    }
    void recipe(const char* name,glm::dvec3 position,glm::dvec3 normal,glm::dvec3 color,
                double radius,const WorldFxRecipeContext& context={}){
        append(BuildWorldFxRecipe(name,position,normal,color,radius,random,-1.6,camera,{},context,
                                  glm::dvec3(0,0,1),lastTime,lastDt));
    }
    void addTrail(glm::dvec3 position,double yaw,double speed,int team){
        auto& item=trail[nextTrail];const auto index=nextTrail;nextTrail=(nextTrail+1)%trail.size();
        item.position=position;item.position.y+=0.0015+double(index%5)*0.0003;
        const double sp=clamp01(speed/10.0);
        item.width=(0.068+0.024*sp)*(0.85+0.3*random01(random));item.length=0.12;item.yaw=yaw;
        item.color=teamColor(team);item.age=0;item.life=1.6+random01(random)*0.5;item.active=true;
    }
    void sourceSplat(glm::dvec3 position,glm::dvec3 color,double size,double life){
        const auto index=nextSplat;nextSplat=(nextSplat+1)%splats.size();
        auto& item=splats[index];item.position=position;item.position.y+=0.0008+double(index%7)*0.00035;
        item.color=color;item.yaw=random01(random)*kTau;item.size=size;item.age=0;item.life=life;item.active=true;
    }
    void sourceInkBurst(glm::dvec3 position,glm::dvec3 color,int count){
        for(int i=0;i<count&&live.size()<kParticleCapacity;++i){
            const double angle=random01(random)*kTau,speed=0.6+random01(random)*1.1;
            const double vy=0.4+random01(random)*1.4,size=0.012+random01(random)*0.014;
            live.push_back(makeDrop(position+glm::dvec3(0.0,0.2,0.0),
                {std::sin(angle)*speed,vy,std::cos(angle)*speed},color,size,2.0,10.5/17.0,0.9,random));
        }
    }
    void sourceSparkles(glm::dvec3 position,glm::dvec3 color,int count){
        for(int i=0;i<count;++i){
            auto& item=sparkles[nextSparkle];nextSparkle=(nextSparkle+1)%sparkles.size();
            const double angle=(double(i)/count)*kTau+random01(random)*0.5,radius=0.3+random01(random)*0.3;
            item.position={position.x+std::sin(angle)*radius,position.y+0.35+random01(random)*1.35,position.z+std::cos(angle)*radius};
            item.color=random01(random)<0.45?glm::mix(color,glm::dvec3(1.0),0.35):glm::dvec3(1.0,0.96,0.84);
            item.size=0.07+random01(random)*0.07;item.life=0.45+random01(random)*0.4;item.age=0;item.active=true;
        }
    }
    void sourceLanding(glm::dvec3 position,glm::dvec3 color,double strength){
        const bool heavy=strength>0.9;
        sourceCrown(live,position,color,heavy?1.2:0.75,heavy?24:14,0.22,random);
        sourceSplat(position,color,0.34,2.8);
        if(heavy){
            sourceRipple(live,position,color,0.15,0.95,0.65);
            sourceSparkles(position,color,10);
        }
    }
    void readyFx(glm::dvec3 position,glm::dvec3 color){
        sourceCrown(live,position,color,0.8,16,0.28,random);
        sourceRipple(live,position,color,0.1,1.1,0.8);
        sourceSparkles(position,color,14);
    }
    void afterDive(glm::dvec3 position,glm::dvec3 color){
        sourceCrown(live,position,color,1.0,18,0.18,random);
        sourceSplat(position,color,0.3,1.8);
        sourceRipple(live,position,color,0.08,0.6,0.55);
    }
    void updateActor(const LobbyActorState& actor,double dt){
        auto& fx=actors[actor.id];
        if(!fx.initialized){fx.initialized=true;fx.previous=actor.position;fx.form=actor.form;return;}
        if(fx.inkBurst&&fx.form!=actor.form&&
           ((fx.burstTarget==BurstTarget::Squid&&actor.form=="squid")||(fx.burstTarget==BurstTarget::Kid&&actor.form=="kid"))){
            sourceInkBurst(actor.position-glm::dvec3(0.0,0.1,0.0),teamColor(actor.team),fx.inkBurst);fx.inkBurst=0;fx.burstTarget=BurstTarget::None;
        }
        if(actor.visible&&actor.form=="swim"){
            const glm::dvec3 delta(actor.position.x-fx.previous.x,0.0,actor.position.z-fx.previous.z);
            const double distance=glm::length(delta);
            if(distance>1e-8){
                const glm::dvec3 direction=delta/distance;double traversed=0.0,credit=fx.trailRemainder;
                while(traversed+std::max(0.0,kTrailGap-credit)<=distance+1e-9){
                    const double step=std::max(0.0,kTrailGap-credit);traversed+=step;
                    const double t=clamp01(traversed/distance);auto at=glm::mix(fx.previous,actor.position,t);
                    at.y-=0.1;at-=direction*0.16;
                    addTrail(at,actor.yaw,actor.speed,actor.team);credit=0.0;
                    if(traversed>=distance)break;
                }
                fx.trailRemainder=credit+(distance-traversed);
                fx.previous=actor.position;
                const double sp=clamp01(actor.speed/10.0);
                if(random01(random)<dt*26.0*sp){
                    const double side=random01(random)<0.5?-1.0:1.0;
                    const double r0=random01(random),r1=random01(random),r2=random01(random);
                    const glm::dvec3 lateral(direction.z*side,0.0,-direction.x*side);
                    const glm::dvec3 position=actor.position-glm::dvec3(0.0,0.1,0.0)-direction*0.28+lateral*0.07;
                    const glm::dvec3 velocity=-direction*(0.6+r0)+lateral*(0.4+r1*0.5);
                    live.push_back(makeDrop(position+glm::dvec3(0.0,0.05,0.0),
                        {velocity.x,1.1+r2*1.3,velocity.z},teamColor(actor.team),0.01+random01(random)*0.012,
                        2.0,10.5/17.0,0.9,random));
                }
            }
        }else{
            fx.previous=actor.position;fx.trailRemainder=0;
        }
        fx.form=actor.form;
    }
    void update(const std::vector<LobbyActorState>& roster,const std::vector<LobbyPresentationEvent>& events,double dt,double time){
        dt=std::clamp(std::isfinite(dt)?dt:0.0,0.0,0.1);lastTime=std::isfinite(time)?time:0.0;lastDt=dt;
        for(auto& item:trail)if(item.active){item.age+=dt;if(item.age>=item.life)item.active=false;}
        for(auto& item:splats)if(item.active){item.age+=dt;if(item.age>=item.life)item.active=false;}
        for(auto& item:sparkles)if(item.active){item.age+=dt;if(item.age>=item.life)item.active=false;}
        UpdateWorldFxRecipe(live,dt,random,{},-1.6f,false,false);

        std::unordered_map<network::MemberId,const LobbyActorState*> byId;
        byId.reserve(roster.size());for(const auto& actor:roster)byId.emplace(actor.id,&actor);
        for(std::size_t i=0;i<ready.size();){
            ready[i].remaining-=dt;
            if(ready[i].remaining<=0){
                if(const auto found=byId.find(ready[i].id);found!=byId.end())readyFx(found->second->position,teamColor(found->second->team));
                ready[i]=ready.back();ready.pop_back();
            }else ++i;
        }
        for(std::size_t i=0;i<bursts.size();){
            bursts[i].remaining-=dt;
            if(bursts[i].remaining<=0){
                if(const auto found=byId.find(bursts[i].id);found!=byId.end())
                    sourceInkBurst(bursts[i].position,teamColor(found->second->team),bursts[i].count);
                bursts[i]=bursts.back();bursts.pop_back();
            }else ++i;
        }
        for(const auto& event:events){
            auto state=actors.find(event.id);
            const auto actor=byId.find(event.id);
            const glm::dvec3 color=actor==byId.end()?InkColors[0]:teamColor(actor->second->team);
            if(event.kind=="ready")ready.push_back({event.id,0.36});
            else if(event.kind=="unready")ready.erase(std::remove_if(ready.begin(),ready.end(),[&](const auto& pending){return pending.id==event.id;}),ready.end());
            else if(event.kind=="dive"||event.kind=="switch"||event.kind=="leave"){
                bursts.push_back({event.id,event.position,0.283,9});
            }else if(event.kind=="leap"){
                sourceCrown(live,event.position,color,0.9,16,0.14,random);
                sourceRipple(live,event.position,color,0.06,0.5,0.5);
                auto& fx=actors[event.id];fx.inkBurst=7;fx.burstTarget=BurstTarget::Kid;
            }else if(event.kind=="pop"){
                sourceCrown(live,event.position,color,1.15,22,0.2,random);
                sourceSparkles(event.position,color,8);
            }else if(event.kind=="land"||event.kind=="switch_land"||event.kind=="hub_arrival"){
                sourceLanding(event.position,color,event.strength);
                if(state!=actors.end()){state->second.inkBurst=0;state->second.burstTarget=BurstTarget::None;}
            }else if(event.kind=="switch_swim"||event.kind=="leave_swim"){
                afterDive(event.position,color);
            }else if(event.kind=="superjump"){
                sourceCrown(live,event.position,color,1.2,22,0.24,random);
                sourceRipple(live,event.position,color,0.15,1.1,0.6);
            }
        }
        for(const auto& actor:roster)updateActor(actor,dt);
        for(auto it=actors.begin();it!=actors.end();){
            if(!byId.count(it->first))it=actors.erase(it);else ++it;
        }
    }

    std::vector<TrailGpu> trailInstances()const{
        std::vector<TrailGpu> out;out.reserve(trail.size());
        for(const auto& item:trail){
            if(!item.active)continue;
            const double grow=easeOut3(item.age/0.14);
            const double soak=1.0-smoothstep01((item.age-0.3*item.life)/(0.7*item.life));
            const double width=item.width*grow*(0.3+0.7*soak),height=std::max(0.0004,0.0065*grow*soak);
            const double length=item.length*grow*(0.55+0.45*soak);
            TrailGpu gpu;gpu.positionWidth={float(item.position.x),float(item.position.y),float(item.position.z),float(width)};
            gpu.shape={float(length),float(height),float(item.yaw),0.0f};
            gpu.color={float(item.color.r),float(item.color.g),float(item.color.b)};out.push_back(gpu);
        }
        return out;
    }
    std::vector<TrailGpu> splatInstances()const{
        std::vector<TrailGpu> out;out.reserve(splats.size());
        for(const auto& item:splats){
            if(!item.active)continue;
            const double grow=backOut(item.age/0.16,2.2);
            const double shrink=1.0-smoothstep01((item.age-item.life*0.5)/(item.life*0.5));
            const double scale=item.size*grow*std::max(0.001,shrink);
            TrailGpu gpu;gpu.positionWidth={float(item.position.x),float(item.position.y),float(item.position.z),float(scale)};
            gpu.shape={float(scale),float(scale*(0.35+0.65*shrink)),float(item.yaw),0.0f};
            gpu.color={float(item.color.r),float(item.color.g),float(item.color.b)};out.push_back(gpu);
        }
        return out;
    }
    std::vector<SparkleGpu> sparkleInstances()const{
        std::vector<SparkleGpu> out;out.reserve(sparkles.size());
        for(std::size_t i=0;i<sparkles.size();++i){
            const auto& item=sparkles[i];if(!item.active)continue;
            const double f=item.age/item.life,env=std::pow(std::sin(kPi*f),1.6)*(0.82+0.18*std::sin(item.age*31.0+double(i)));
            SparkleGpu gpu;gpu.positionSize={float(item.position.x),float(item.position.y+item.age*0.05),float(item.position.z),float(item.size*env)};
            gpu.color={float(item.color.r),float(item.color.g),float(item.color.b),1.0f};out.push_back(gpu);
        }
        return out;
    }
    void prepareDome(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& lights){
        domeShader->use();domeShader->matrices(glm::dmat4(1.0),view,projection,eye);domeShader->lights(lights,view);
        const glm::dvec3 sunDir=glm::normalize(glm::dmat3(view)*glm::dvec3(lights.sunDirection));
        domeShader->uniform("directionalLights[0].direction",std::vector<double>{sunDir.x,sunDir.y,sunDir.z});
        domeShader->uniform("directionalLights[0].color",std::vector<double>{lights.sunColor.r,lights.sunColor.g,lights.sunColor.b});
        domeShader->uniform("ambientLightColor",std::vector<double>{lights.ambient.r,lights.ambient.g,lights.ambient.b});
        const glm::dvec3 skyDir=glm::normalize(glm::dmat3(view)*glm::dvec3(lights.skyDirection));
        domeShader->uniform("hemisphereLights[0].direction",std::vector<double>{skyDir.x,skyDir.y,skyDir.z});
        domeShader->uniform("hemisphereLights[0].skyColor",std::vector<double>{lights.skyColor.r,lights.skyColor.g,lights.skyColor.b});
        domeShader->uniform("hemisphereLights[0].groundColor",std::vector<double>{lights.groundColor.r,lights.groundColor.g,lights.groundColor.b});
        domeShader->uniform("uClearcoat",0.55);domeShader->uniform("envMap",9.0);
    }
    int draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& lights,double time){
        initGpu();GlState state;int draws=0;
        const auto trailGpu=trailInstances();
        const auto splatGpu=splatInstances();
        if(!trailGpu.empty()||!splatGpu.empty()){
            glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
            prepareDome(view,projection,eye,lights);
            glBindVertexArray(trailVao);glBindBuffer(GL_ARRAY_BUFFER,trailInstance);
            if(!trailGpu.empty()){
                glBufferData(GL_ARRAY_BUFFER,trailGpu.size()*sizeof(TrailGpu),trailGpu.data(),GL_STREAM_DRAW);
                glDrawArraysInstanced(GL_TRIANGLES,0,trailVertexCount,static_cast<GLsizei>(trailGpu.size()));++draws;
            }
            if(!splatGpu.empty()){
                glBufferData(GL_ARRAY_BUFFER,splatGpu.size()*sizeof(TrailGpu),splatGpu.data(),GL_STREAM_DRAW);
                glDrawArraysInstanced(GL_TRIANGLES,splatVertexFirst,splatVertexCount,static_cast<GLsizei>(splatGpu.size()));++draws;
            }
        }
        const auto sparkGpu=sparkleInstances();
        if(!sparkGpu.empty()){
            glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);glEnable(GL_BLEND);
            glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
            sparkShader->use();sparkShader->matrices(glm::dmat4(1.0),view,projection,eye);
            glBindVertexArray(sparkVao);glBindBuffer(GL_ARRAY_BUFFER,sparkInstance);
            glBufferData(GL_ARRAY_BUFFER,sparkGpu.size()*sizeof(SparkleGpu),sparkGpu.data(),GL_STREAM_DRAW);
            glDrawArraysInstanced(GL_TRIANGLES,0,6,static_cast<GLsizei>(sparkGpu.size()));++draws;
        }
        draws+=particles.Draw(live,{},glm::mat4(view),glm::mat4(projection),glm::vec3(eye),&lights,time);
        return draws;
    }
    Counts counts()const{
        Counts result;result.particles=live.size();
        for(const auto& item:trail)if(item.active)++result.trailDomes;
        for(const auto& item:splats)if(item.active)++result.splats;
        for(const auto& item:sparkles)if(item.active)++result.sparkles;
        return result;
    }
};

LobbyEffects::LobbyEffects(std::filesystem::path particleAssets):impl_(std::make_unique<Impl>(std::move(particleAssets))){}
LobbyEffects::~LobbyEffects()=default;
void LobbyEffects::Update(const std::vector<LobbyActorState>& actors,const std::vector<LobbyPresentationEvent>& events,double dt,double time){impl_->update(actors,events,dt,time);}
int LobbyEffects::Draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& lights,double time){return impl_->draw(view,projection,eye,lights,time);}
LobbyEffects::Counts LobbyEffects::counts()const{return impl_->counts();}
}

