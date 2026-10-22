#define GLM_ENABLE_EXPERIMENTAL
#include "WorldEffects.hpp"
#include "WorldFxTransitions.hpp"
#include "TeamPalette.hpp"
#include "DeviceRenderer.hpp"
#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"
#include "ArsenalData.hpp"
#include "CharacterData.hpp"
#include "FacePaint.hpp"
#include "../game/WorldFxAssetData.hpp"
#include "../world/StageGeometry.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace restoration {
namespace {
constexpr float pi=3.14159265358979323846f;
constexpr glm::vec3 orange{1.0f,0.5412f,0.0784f},blue{0.1843f,0.3569f,1.0f};
glm::vec3 ink(int team) { return SrgbInkColors[team==1?1:0]; }
glm::vec3 unit(glm::vec3 v) { return glm::length(v)>1e-5f?glm::normalize(v):glm::vec3(0,1,0); }
float clamp01(float v) { return std::clamp(v,0.0f,1.0f); }
struct Vertex { glm::vec4 posRadius, normalLength, colorAlpha, meta, velocity; };
GLuint shader(GLenum type,const char* source) {
    const GLuint id=glCreateShader(type);
    glShaderSource(id,1,&source,nullptr);glCompileShader(id);
    GLint ok=0;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
    if(!ok) { char log[4096]{};glGetShaderInfoLog(id,sizeof(log),nullptr,log);glDeleteShader(id);throw std::runtime_error(std::string("WorldEffects shader: ")+log); }
    return id;
}
constexpr const char* vertexSource=R"GLSL(#version 450 core
layout(location=0) in vec4 posRadius;
layout(location=1) in vec4 normalLength;
layout(location=2) in vec4 colorAlpha;
layout(location=3) in vec4 meta;
layout(location=4) in vec4 velocity;
out V {vec3 pos;vec3 normal;vec3 color;vec3 vel;float radius;float length;float alpha;float shape;float age;float style;float seed;} v;
void main(){v.pos=posRadius.xyz;v.radius=posRadius.w;v.normal=normalLength.xyz;v.length=normalLength.w;
v.color=colorAlpha.rgb;v.alpha=colorAlpha.a;v.shape=meta.x;v.age=meta.y;v.style=meta.z;v.seed=meta.w;
v.vel=velocity.xyz;gl_Position=vec4(posRadius.xyz,1.0);}
)GLSL";
constexpr const char* geometrySource=R"GLSL(#version 450 core
layout(points) in;
layout(triangle_strip,max_vertices=16) out;
in V {vec3 pos;vec3 normal;vec3 color;vec3 vel;float radius;float length;float alpha;float shape;float age;float style;float seed;} v[];
out vec2 uv;out vec3 col;out vec3 world;out vec3 nrm;out float alpha;out float shape;out float age;out float style;out float seed;
uniform mat4 view;uniform mat4 projection;
void quad(vec3 center,vec3 right,vec3 up){
  vec2 q[4]=vec2[4](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1));
  for(int i=0;i<4;i++){uv=q[i];world=center+right*q[i].x+up*q[i].y;
    gl_Position=projection*view*vec4(world,1);EmitVertex();}
  EndPrimitive();
}
void main(){
  col=v[0].color;alpha=v[0].alpha;shape=v[0].shape;age=v[0].age;style=v[0].style;seed=v[0].seed;
  vec3 camR=vec3(view[0][0],view[1][0],view[2][0]);
  vec3 camU=vec3(view[0][1],view[1][1],view[2][1]);
  vec3 axis=normalize(v[0].normal);nrm=axis;
  float r=v[0].radius;
  if(shape==3.0){vec3 ref=abs(axis.y)<0.95?vec3(0,1,0):vec3(1,0,0);
    vec3 right=normalize(cross(ref,axis));quad(v[0].pos+axis*0.025,right*r,cross(axis,right)*r);return;}
  if(shape==5.0||shape==6.0){vec3 right=normalize(cross(axis,camU));if(length(right)<0.001)right=camR;
    vec3 up=axis*max(v[0].length,0.01)*0.5;
    quad(v[0].pos+up,right*r,up);quad(v[0].pos+up,normalize(cross(axis,right))*r,up);return;}
  if(shape==9.0){quad(v[0].pos,camR*r,camU*r*1.8);return;}
  if(shape==10.0){quad(v[0].pos,camR*r,camU*r*1.25);return;}
  if(shape==0.0){vec2 projected=(view*vec4(v[0].vel,0)).xy;float stretch=1.0+min(length(projected)*0.09,3.0);
    vec3 up=length(projected)>0.01?normalize(camR*projected.x+camU*projected.y):camU;
    quad(v[0].pos,normalize(cross(up,normalize(vec3(view[0][2],view[1][2],view[2][2]))))*r/sqrt(stretch),up*r*stretch);return;}
  quad(v[0].pos,camR*r,camU*r);
}
)GLSL";
constexpr const char* fragmentSource=R"GLSL(#version 450 core
in vec2 uv;in vec3 col;in vec3 world;in vec3 nrm;in float alpha;in float shape;in float age;in float style;in float seed;
out vec4 outColor;uniform float time;
float hash(vec2 p){vec3 p3=fract(vec3(p.xyx)*0.1031);p3+=dot(p3,p3.yzx+33.33);return fract((p3.x+p3.y)*p3.z);}
float noise(vec2 p){vec2 i=floor(p),f=fract(p),u=f*f*(3.0-2.0*f);
return mix(mix(hash(i),hash(i+vec2(1,0)),u.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),u.x),u.y);}
void main(){float r=length(uv),a=0;vec3 c=col;float t=clamp(age,0,1);
 if(shape==0.0){
   float tail=clamp(length(uv)*0.2,0,1);float y=uv.y+0.22*tail;
   a=1.0-smoothstep(0.86,1.0,length(vec2(uv.x,y)));
   float z=sqrt(max(0.0,1.0-dot(uv,uv)));float rim=pow(1.0-z,2.2);
   c=col*(0.55+0.65*max(z*0.8+uv.y*0.2,0.0));c+=col*(col+0.25)*pow(max(-uv.x,0),1.5)*0.25;
   c=mix(c,vec3(0.48,0.66,0.95),rim*0.26);c+=vec3(1.0,0.95,0.82)*pow(max(1.0-length(uv-vec2(-0.28,0.33))*2.6,0),18.0)*1.7;
 }else if(shape==1.0){float n=noise(uv*3.1+seed*19.0+t*1.5);float edge=0.58+(n-0.5)*0.35;
   a=smoothstep(1.0,edge*0.7,r+(n-0.5)*0.2)*(1.0-t*t);c*=0.76+0.28*uv.y+0.15*n;
 }else if(shape==2.0){
   float kind=floor(style/10.0+0.001),core=style-kind*10.0;
   if(kind<0.5){float g=pow(max(1.0-r,0.0),2.2),hot=pow(max(1.0-r*1.8,0.0),3.0);c=col*g+vec3(1.0,0.97,0.92)*hot*core;}
   else if(kind<1.5){float cr=max(exp(-abs(uv.x)*22.0)*exp(-abs(uv.y)*2.6),exp(-abs(uv.y)*22.0)*exp(-abs(uv.x)*2.6));vec2 d=vec2(uv.x+uv.y,uv.x-uv.y)*0.7071;float dg=max(exp(-abs(d.x)*30.0)*exp(-abs(d.y)*6.0),exp(-abs(d.y)*30.0)*exp(-abs(d.x)*6.0))*0.4;float halo=pow(max(1.0-r,0.0),3.0)*0.45,hot=pow(max(1.0-r*3.2,0.0),2.0);c=col*(cr+dg+halo)+vec3(1.0,0.98,0.95)*hot*core;}
   else if(kind<2.5){float rim=smoothstep(0.16,0.0,abs(r-0.8))*0.8+smoothstep(0.5,0.95,r)*step(r,0.9)*0.25;float hl=smoothstep(0.24,0.0,length(uv-vec2(-0.3,0.34)));c=col*rim+vec3(1.0)*hl*(0.6+core*0.2);}
   else{float rim=exp(-pow((r-0.78)*7.0,2.0));c=col*rim+vec3(1.0)*rim*rim*core*0.3;}
   outColor=vec4(c*alpha,1.0);return;
 }else if(shape==3.0){float ang=atan(uv.y,uv.x),wob=(noise(vec2(cos(ang),sin(ang))*3.0+seed*53.0)-0.5)*0.1;
   if(style<0.5){float outer=0.93+wob,w=mix(0.34,0.07,t);a=smoothstep(outer+0.02,outer-0.02,r)*smoothstep(outer-w-0.03,outer-w+0.02,r)*pow(1.0-t,1.3);}
   else if(style<1.5){a=smoothstep(0.98,0.86,r)*smoothstep(0.74,0.84,r)*pow(1.0-t,2.0);c=mix(c,vec3(1),0.35);}
   else if(style<2.5){a=smoothstep(0.95+wob,0.65+wob,r)*pow(1.0-t,2.0);}
   else if(style<3.5){float rim=smoothstep(0.05,0.012,abs(r-0.95));float dash=step(0.45,fract(ang/6.28318*16.0-time*(0.5+t*1.8)))*smoothstep(0.04,0.012,abs(r-0.87));a=max(max(rim,dash),0.12+0.2*t);a*=step(r,0.99);}
   else if(style<4.5){float outer=smoothstep(0.045,0.012,abs(r-0.94)),inner=smoothstep(0.03,0.008,abs(r-0.56));float core=smoothstep(0.2,0.14,r)*(0.7+0.3*sin(time*9.0));a=max(max(outer,inner),core);}
   else if(style<5.5){a=smoothstep(0.9+wob,0.6+wob,r)*smoothstep(0.15,0.4,r)*pow(1.0-t,1.6)*0.5;}
   else {a=exp(-pow((r-0.93)/mix(0.14,0.035,t),2.0))*pow(1.0-t,1.1);c=mix(c,vec3(1),0.4);}
 }else if(shape==4.0){float n=noise(uv*4.0+seed*17.0+time*0.3);float holes=smoothstep(0.35+0.5*t,0.52+0.5*t,n);
   a=(1.0-smoothstep(0.85,1.0,r))*holes;c*=0.65+0.35*(1.0-r*r);c+=col*pow(max(uv.y,0),3.0)*0.3;
 }else if(shape==5.0){float band=sin(uv.y*34.0-time*30.0+uv.x*11.0);float edge=1.0-smoothstep(0.7,1.0,abs(uv.x));
   a=edge*(0.35+0.5*max(band,0.0))*(1.0-t*0.25);c=mix(c,vec3(1),max(band,0.0)*0.32);
 }else if(shape==6.0){float ang=atan(uv.y,uv.x);float spiral=sin(uv.y*24.0+uv.x*12.0-time*19.0+seed*6.0);
   float wide=mix(0.28,1.0,(uv.y+1.0)*0.5);a=(1.0-smoothstep(wide*0.7,wide,abs(uv.x)))*(0.5+0.5*spiral)*(1.0-t*0.65);
   c=mix(c,vec3(1),0.22+0.28*max(spiral,0.0));
 }else if(shape==7.0){float rim=exp(-pow((r-0.86)*8.0,2.0));float glare=pow(max(1.0-length(uv-vec2(-0.32,0.36))*2.3,0.0),8.0);
   a=(0.18+rim*0.62+glare*0.55)*(1.0-smoothstep(0.97,1.0,r));c=mix(col,vec3(1),glare*0.8+rim*0.23);
 }else if(shape==8.0){a=(1.0-smoothstep(0.82,1.0,r))*(1.0-t*0.6);c=col*(1.2+0.8*(1.0-r));
 }else if(shape==9.0){float head=step(-0.4,uv.y)*step(0.12,uv.y)*step(abs(uv.x),0.7);
   float handle=step(uv.y,-0.25)*step(-0.9,uv.y)*step(abs(uv.x),0.11);
   a=max(head,handle)*(1.0-t*0.3);c=col*(0.65+0.35*uv.y);
 }else {float body=step(abs(uv.x),0.8)*step(abs(uv.y),0.8);float cone=smoothstep(0.65,0.55,length(uv-vec2(0,0.15)));
   a=body*(0.65+0.35*cone);c=mix(vec3(0.12,0.16,0.22),col,cone*0.85);}
 a*=alpha;if(a<0.004)discard;outColor=vec4(c,a);
}
)GLSL";

// specials.js SWIRL_VS/FS, BEAM_FS and BUBBLE_VS/FS. Geometry and UVs are
// exported directly from the same THREE.js constructors used by the source.
constexpr const char* sourceVertexSource=R"GLSL(#version 450 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec3 color;
layout(location=4) in float aMat;
uniform mat4 uModel; uniform mat4 uView; uniform mat4 uProjection;
uniform mat3 uNormalMatrix; uniform int uMode; uniform float uTime;
uniform float uBottom; uniform float uTop; uniform float uWobble;
out vec2 vUv; out float vRim; out vec3 vN; out vec3 vV; out vec3 vP;
out vec3 vColor; out float vMat;
void main(){
  vUv=uv; vP=position; vColor=color; vMat=aMat;
  vec3 p=position; vec4 mv;
  if(uMode==1 || uMode==2){
    float k=mix(uBottom,uTop,pow(uv.y,0.8));
    p.xz*=k;
    p.x+=sin(uTime*3.1+uv.y*5.0)*uWobble*uv.y;
    p.z+=cos(uTime*2.7+uv.y*4.2)*uWobble*uv.y;
    mv=uView*uModel*vec4(p,1.0);
    vec3 n=normalize(uNormalMatrix*normal);
    vRim=1.0-abs(dot(n,normalize(-mv.xyz)));
    vN=n;vV=normalize(-mv.xyz);
  } else {
    mv=uView*uModel*vec4(p,1.0);
    vN=normalize(uNormalMatrix*normal);vV=normalize(-mv.xyz);
    vRim=1.0-abs(dot(vN,vV));
  }
  gl_Position=uProjection*mv;
}
)GLSL";
constexpr const char* sourceFragmentSource=R"GLSL(#version 450 core
in vec2 vUv; in float vRim; in vec3 vN; in vec3 vV; in vec3 vP;
in vec3 vColor; in float vMat;
out vec4 outColor;
uniform int uMode; uniform float uTime; uniform float uAlpha;
uniform float uSpeed; uniform float uCharge; uniform float uHit;
uniform vec3 uColor; uniform vec3 uTint; uniform vec3 uEmissive;
void main(){
  if(uMode==1){
    float s=fract(vUv.x*4.0+vUv.y*2.5-uTime*uSpeed);
    float band=smoothstep(0.0,0.18,s)*smoothstep(0.62,0.3,s);
    float s2=fract(vUv.x*7.0-vUv.y*1.5-uTime*uSpeed*1.6);
    float band2=smoothstep(0.0,0.1,s2)*smoothstep(0.4,0.2,s2);
    float fade=smoothstep(0.0,0.12,vUv.y)*smoothstep(1.0,0.8,vUv.y);
    float a=(0.28+0.5*band+0.25*band2)*fade*(0.55+0.45*vRim)*uAlpha;
    vec3 c=mix(uColor*0.7,uColor*1.5+0.12,band*0.8+band2*0.4);
    outColor=vec4(c,a);return;
  }
  if(uMode==2){
    float r=fract(vUv.y*26.0-uTime*5.0);
    float ring=smoothstep(0.0,0.08,r)*smoothstep(0.35,0.12,r);
    float edge=smoothstep(0.0,0.03,vUv.y)*smoothstep(1.0,0.85,vUv.y);
    float a=mix(0.12+0.5*vRim,(0.25+0.6*ring)*(0.45+0.55*vRim),uCharge)*edge*uAlpha;
    vec3 c=mix(uColor,uColor*1.6+0.25,ring*uCharge);
    outColor=vec4(c,a);return;
  }
  if(uMode==3){
    float f=1.0-abs(dot(normalize(vN),normalize(vV)));
    float rim=pow(f,2.2);
    vec3 irid=0.5+0.5*cos(6.2831*(f*1.3+uTime*0.15+vec3(0.0,0.33,0.67)));
    float swirl=0.5+0.5*sin(vP.y*5.0+vP.x*3.0+uTime*2.0);
    vec3 c=mix(uColor,irid,0.35*rim)+uColor*swirl*0.15+vec3(uHit);
    float hl=pow(max(0.0,dot(normalize(vN),normalize(vec3(-0.4,0.7,0.6)))),40.0);
    float a=(0.1+0.75*rim+0.2*swirl*rim+uHit*0.5)*uAlpha+hl*0.8;
    outColor=vec4(c+hl,clamp(a,0.0,1.0));return;
  }
  vec3 base=max(vec3(0.0),vColor*uTint);
  vec3 n=normalize(vN),v=normalize(vV),l=normalize(vec3(-0.4,0.8,0.5));
  float ndl=max(dot(n,l),0.0),rough=mix(0.32,0.78,clamp(vMat/4.0,0.0,1.0));
  vec3 h=normalize(l+v);float spec=pow(max(dot(n,h),0.0),mix(64.0,10.0,rough))*(1.0-rough*0.45);
  vec3 c=base*(0.38+0.68*ndl)+vec3(spec*0.48)+uEmissive;
  outColor=vec4(c,uAlpha);
}
)GLSL";
}

struct WorldEffects::GpuMesh {
    GLuint vao=0,index=0;
    std::vector<GLuint> buffers;
    GLsizei count=0;
    ~GpuMesh(){
        if(index)glDeleteBuffers(1,&index);
        if(!buffers.empty())glDeleteBuffers(static_cast<GLsizei>(buffers.size()),buffers.data());
        if(vao)glDeleteVertexArrays(1,&vao);
    }
};

WorldEffects::WorldEffects(const CharacterData& data):data_(&data) {}

WorldEffects::~WorldEffects() {
    sourceMeshes_.clear();
    if(sourceProgram_)glDeleteProgram(sourceProgram_);
    if(vbo_)glDeleteBuffers(1,&vbo_);
    if(vao_)glDeleteVertexArrays(1,&vao_);
    if(program_)glDeleteProgram(program_);
}
float WorldEffects::random() { random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;return float(double(random_)/4294967296.0); }
glm::vec3 WorldEffects::cone(glm::vec3 normal,float spread) {
    normal=unit(normal);glm::vec3 side=unit(glm::cross(normal,std::abs(normal.y)<0.9f?glm::vec3(0,1,0):glm::vec3(1,0,0)));
    glm::vec3 other=glm::cross(normal,side);float z=1-random()*(1-std::cos(spread));
    float r=std::sqrt(std::max(0.0f,1-z*z)),a=random()*2*pi;
    return normal*z+(side*std::cos(a)+other*std::sin(a))*r;
}
void WorldEffects::spawn(Item item) {
    const int type=static_cast<int>(item.shape);
    const std::size_t caps[]={2600,520,300,300,32,10};
    if(type<0||type>=6){live_.push_back(item);return;}
    std::size_t count=0;
    for(const auto& p:live_)if(p.shape==item.shape)++count;
    if(count>=caps[type]) {
        auto it=std::find_if(live_.begin(),live_.end(),[&](const Item& p){return p.shape==item.shape;});
        if(it!=live_.end())*it=item;
    } else live_.push_back(item);
}
void WorldEffects::burst(glm::vec3 pos,glm::vec3 normal,glm::vec3 color,float size,int count) {
    normal=unit(normal);
    for(int i=0;i<count;i++) {
        const glm::vec3 dir=cone(normal,0.9f*pi*0.5f),at=pos+normal*0.04f;
        const float speed=4.0f*(0.5f+random()*0.8f),sz=size*(0.35f+random()*0.6f);
        spawn({Shape::Drop,at,dir*speed,normal,color,sz,0,0,1.6f+random()*0.6f,1,0,random()});
        if(random()<0.45f) {
            const float sat=speed*(0.7f+random()*0.5f);
            spawn({Shape::Drop,at,dir*sat+glm::vec3((random()-0.5f)*0.8f,(random()-0.5f)*0.8f,(random()-0.5f)*0.8f),normal,color,sz*0.4f,0,0,1.2f,1,0,random()});
        }
    }
    const float splash=size*(1.6f+0.5f*std::sqrt(float(count)));
    if(count>=4)spawn({Shape::Shell,pos+normal*splash*0.12f,{},normal,color,splash*1.1f,0,0,0.17f+splash*0.12f,0.95f,0,random()});
}
void WorldEffects::explosion(glm::vec3 pos,glm::vec3 color,float radius) {
    const float rv=std::min(radius,2.4f),speed=std::sqrt(std::max(radius,0.1f)/3.0f);
    spawn({Shape::Glow,pos,{},{0,1,0},color*4.5f,rv*0.85f,0,0,0.12f,1,0,random()});
    spawn({Shape::Glow,pos,{},{0,1,0},color*1.15f,rv*1.6f,0,0,0.17f,0.5f,0,random()});
    spawn({Shape::Shell,pos,{},{0,1,0},color,rv*0.78f,0,0,0.34f,1,0,random()});
    auto drops=[&](int n,float size,float life,float scale) {
        for(int i=0;i<n;i++) { const float a=random()*2*pi,r=random();
            const glm::vec3 d=unit(glm::vec3(std::cos(a)*std::sqrt(1-r*r),r*0.85f+0.12f,std::sin(a)*std::sqrt(1-r*r)));
            spawn({Shape::Drop,pos+d*(rv*0.3f),d*(scale*(0.7f+random()*0.6f)*speed),d,color,size*(0.7f+random()*0.6f),0,0,life,1,0,random()});
        }
    };
    drops(26,0.045f,1.6f,9.5f);drops(6,0.085f,2.2f,4.0f);drops(18,0.02f,1.0f,7.0f);
    spawn({Shape::Ring,pos,{},{0,1,0},color,radius,0,0,0.55f,0.9f,0,random()});
    for(int i=0;i<4;i++)spawn({Shape::Puff,pos+glm::vec3((random()-0.5f)*radius,random()*radius*0.4f,(random()-0.5f)*radius),
        glm::vec3((random()-0.5f)*2,0.5f+random(),(random()-0.5f)*2),{0,1,0},glm::mix(color,glm::vec3(1),0.42f),rv*0.45f,0,0,0.55f+random()*0.3f,0.15f,0,random()});
}
void WorldEffects::emitSpecial(const char* kind,glm::vec3 pos,glm::vec3 normal,glm::vec3 color,float radius,glm::vec3 camera) {
    const std::string_view recipe(kind);
    if(recipe=="burst"||recipe=="explosion"||recipe=="slamWave"||recipe=="stormStart"||
       recipe=="formPop"||recipe=="dive"||recipe=="emerge"||recipe=="wake") {
        WorldFxRippleSink ripple;
        if(paint_)ripple=[this](glm::dvec3 p,double a,double w,double s,double l){paint_->requestVisualRipple({p,a,w,s,l});};
        auto particles=BuildWorldFxRecipe(recipe,pos,normal,color,radius,random_,-1.6f,camera,ripple);
        for(auto& particle:particles)spawn(std::move(particle));
    } else if(std::strcmp(kind,"beam")==0)
        spawn({Shape::Beam,pos,{},normal,color,0.16f,radius,0,0.12f,0.9f,0,random()});
}

void WorldEffects::Footstep(const PlayerSimulation& actor,glm::dvec3 position,double speed){
    auto& previous=actors_[&actor];previous.footsteps=true;
    const auto& state=actor.state;
    if(!state.alive||glm::distance(position,camera_)>(state.groundTeam?28.0:18.0))return;
    glm::dvec3 heading(state.vel.x,0,state.vel.z);
    if(glm::length2(heading)>1e-12)heading=glm::normalize(heading);else heading={std::sin(state.yaw),0,std::cos(state.yaw)};
    const char* recipe=state.groundTeam==0?"footstepDry":state.groundTeam==2?"footstepEnemy":"footstepOwn";
    const auto color=ink(state.groundTeam==2?1-actor.team():actor.team());
    WorldFxRippleSink ripple;
    if(paint_)ripple=[this](glm::dvec3 p,double a,double w,double s,double l){paint_->requestVisualRipple({p,a,w,s,l});};
    auto particles=BuildWorldFxRecipe(recipe,position,heading,color,speed,random_,-1.6,camera_,ripple);
    for(auto& particle:particles)spawn(std::move(particle));
}

void WorldEffects::Update(double seconds,CombatWorld& combat,glm::dvec3 camera) {
    const float dt=std::clamp(float(seconds),0.0f,0.05f);
    camera_=camera;
    time_+=dt;
    world_.clear();
    const auto near=[&](glm::dvec3 at,double range){return glm::length2(at-camera)<range*range;};
    const auto add=[&](Item item){if(world_.size()<2048)world_.push_back(item);};

    lastEvents_=combat.takeVisualEvents();
    for(const auto& event:lastEvents_) {
        if(event.kind=="device_arm"||event.kind=="device_warning"||event.kind=="device_hit"||event.kind=="device_destroy"
           ||event.kind=="special_warning"||event.kind=="special_audio"||event.kind=="bubble_pop")continue;
        if(event.kind=="beam")continue; // Source charger ribbons are owned by ProjectileRenderer.
        if(!near(event.pos,65))continue;
        emitSpecial(event.kind.c_str(),glm::vec3(event.pos),glm::vec3(event.normal),ink(event.team),float(event.radius),glm::vec3(camera));
    }

    for(const auto* actor:combat.actors()) {
        if(!actor)continue;
        auto& old=actors_[actor];const auto& s=actor->state;
        const auto pose=actor->specialPose();
        const bool squidForm=s.squid&&!pose.body&&!pose.noSquid;
        const WorldFxActorForm previous{old.squid,old.swimming,old.climbing};
        const WorldFxActorForm current{squidForm,squidForm&&s.submerged,squidForm&&s.climbing};
        if(old.initialized&&!old.alive&&s.alive)old.born=time_;
        const bool fresh=!old.initialized||time_-old.born<0.35;
        const bool superJump=actor->superJumpState().has_value();
        const WorldFxTransitionConditions transitionConditions{
            old.initialized,s.alive,fresh,superJump,pose.active,s.groundTeam==1,time_-old.climbEnd<0.25,near(s.pos,26),near(s.pos,30)};
        const auto transitions=ResolveWorldFxTransitions(previous,current,transitionConditions);
        const glm::vec3 pos(actor->visualPos()),color=ink(actor->team());
        if(old.initialized) {
            const double distance=glm::length(glm::dvec2(s.pos.x-old.pos.x,s.pos.z-old.pos.z));
            if(!old.footsteps&&s.alive&&s.grounded&&near(s.pos,24)&&!s.squid&&!pose.active&&!superJump) {
                old.step+=distance;
                if(old.step>0.65) {
                    old.step=0;
                    Footstep(*actor,actor->visualPos(),std::hypot(s.vel.x,s.vel.z));
                    old.footsteps=false; // polling remains available until an actual foot-plant event arrives.
                }
            }
            if(transitions.formPop)
                emitSpecial("formPop",pos,{0,1,0},color,current.squid?1.0f:0.0f,glm::vec3(camera));
            if(transitions.dive)
                emitSpecial("dive",glm::vec3(s.pos),{0,1,0},color,float(std::hypot(s.vel.x,s.vel.z)),glm::vec3(camera));
            if(transitions.emerge)
                emitSpecial("emerge",glm::vec3(s.pos),{0,1,0},color,float(std::hypot(s.vel.x,s.vel.z)),glm::vec3(camera));
            if(s.alive&&!old.grounded&&s.grounded&&s.landSpeed>3.5&&near(s.pos,38))burst(pos,{0,1,0},color,0.11f,14);
            if(old.alive&&!s.alive&&near(s.pos,60)) {
                explosion(pos+glm::vec3(0,0.8f,0),color,1.2f);
                spawn({Shape::Puff,pos+glm::vec3(0,1,0),glm::vec3(0,1.3f,0),{0,1,0},glm::mix(color,glm::vec3(1),0.3f),0.8f,0,0,1.1f,0.7f,3,random()});
            }
            if(s.alive&&s.hp<old.hp-0.05&&near(s.pos,40))burst(pos+glm::vec3(0,s.squid?0.3f:0.95f,0),unit(glm::vec3(s.vel.x,0,s.vel.z)),ink(1-actor->team()),0.07f,6);
            if(pose.active&&old.special!=pose.kind&&near(s.pos,45)) {
                if(pose.kind=="slam") {
                    spawn({Shape::Ring,pos,{},{0,1,0},color,1.5f,0,0,0.4f,0.9f,0,random()});
                    spawn({Shape::Ring,pos,{},{0,1,0},color,1.1f,0,0,0.9f,0.6f,1,random()});
                    for(int i=0;i<18;i++){const auto d=cone({0,1,0},0.45f);spawn({Shape::Drop,pos+glm::vec3(0,0.1f,0),d*(5+random()*5),d,color,0.035f+random()*0.045f,0,0,1.4f,1,0,random()});}
                    for(int i=0;i<3;i++)spawn({Shape::Puff,pos+glm::vec3(0,0.3f,0),glm::vec3((random()-0.5f)*2,1.5f+random(),(random()-0.5f)*2),{0,1,0},glm::mix(color,glm::vec3(1),0.35f),0.9f,0,0,0.5f,0.32f,0,random()});
                } else if(pose.kind=="sonar") {
                    for(int i=0;i<3;i++)spawn({Shape::Ring,pos,{},{0,1,0},color,6.0f+i*7.0f,0,0,0.8f+i*0.25f,0.8f,7,random()});
                } else if(pose.kind=="bubbler"||pose.kind=="kraken") {
                    explosion(pos+glm::vec3(0,0.8f,0),color,1.5f);
                } else {
                    spawn({Shape::Glow,pos+glm::vec3(0,1.1f,0),{},{0,1,0},color*2.0f,0.8f,0,0,0.25f,0.9f,1,random()});
                }
            }
            if(pose.kind=="slam"&&pose.phase=="fall"&&old.phase!="fall"&&near(s.pos,36)) {
                for(int i=0;i<3;i++)spawn({Shape::Drop,pos+glm::vec3((random()-0.5f)*0.8f,1,0),{0,-16,0},{0,1,0},color,0.025f,0,0,0.15f,1,0,random()});
            }
        }
        if(s.alive&&current.swimming&&dt>0&&std::hypot(s.vel.x,s.vel.z)>2.0) {
            old.wakeTimer+=dt;
            if(old.wakeTimer>0.05) {
                old.wakeTimer=0;
                const double horizontalSpeed=std::hypot(s.vel.x,s.vel.z);
                if(near(s.pos,45)) {
                    const glm::dvec3 direction(s.vel.x/horizontalSpeed,0,s.vel.z/horizontalSpeed);
                    emitSpecial("wake",s.pos,direction,color,float(horizontalSpeed),glm::vec3(camera));
                }
            }
        }
        if(s.alive&&near(s.pos,55)) {
            if(s.groundTeam==2&&s.grounded&&dt>0&&random()<dt*8)
                spawn({Shape::Puff,pos+glm::vec3((random()-0.5f)*0.5f,0.1f,(random()-0.5f)*0.5f),{0,0.5f,0},{0,1,0},ink(1-actor->team()),0.16f,0,0,0.3f,0.5f,0,random()});
            if(pose.active) {
                if(pose.kind=="bubbler"||(!models_&&pose.kind=="kraken"))add({Shape::Bubble,pos+glm::vec3(0,0.85f,0),{},{0,1,0},color,pose.kind=="kraken"?1.15f:1.8f,0,0,1,0.65f,0,0});
                else if(pose.kind=="booyah"&&!pose.thrown)add({Shape::Orb,pos+glm::vec3(0,2.0f,0),{},{0,1,0},color,0.5f+float(pose.charge)*0.65f,0,0,1,0.9f,0,0});
                else if(pose.kind=="zipcaster"&&pose.phase=="zip") {
                    glm::vec3 to(pose.target),from=pos+glm::vec3(0,0.8f,0),axis=to-from;
                    if(glm::length(axis)>0.1f)add({Shape::Beam,from,{},unit(axis),color,0.09f,glm::length(axis),0,1,0.7f,0,0});
                } else if(pose.kind=="slam"&&pose.phase=="hang")add({Shape::Glow,pos+glm::vec3(0,1,0),{},{0,1,0},color*2.0f,1.3f,0,0,1,0.5f,0,0});
            }
        }
        if(!old.initialized)old.born=time_;
        if(old.initialized&&old.climbing&&!current.climbing)old.climbEnd=time_;
        old.initialized=true;old.pos=s.pos;old.hp=s.hp;old.charge=s.specialCharge;old.alive=s.alive;
        old.squid=current.squid;old.swimming=current.swimming;old.climbing=current.climbing;old.grounded=s.grounded;old.special=pose.active?pose.kind:"";old.phase=pose.phase;
    }
    for(auto it=actors_.begin();it!=actors_.end();) {
        if(std::find(combat.actors().begin(),combat.actors().end(),it->first)==combat.actors().end())it=actors_.erase(it);
        else ++it;
    }

    for(const auto& object:combat.visualObjects()) {
        if(!near(object.pos,100))continue;
        const glm::vec3 pos(object.pos),col=ink(object.team),dir=unit(glm::vec3(object.direction));
        const float r=float(object.radius),age=float(object.age),timer=float(object.timer);
        if(object.kind=="stormBomb")add({Shape::Orb,pos,{},{0,1,0},col,0.32f,0,0,1,1,0,0});
        else if(object.kind=="stormCloud") {
            // The cloud geometry, 15-puff layout, color lerps and envelope are
            // ported from weapons.js::_spawnCloud / _updateClouds. The source
            // random stream is not present in WorldObjectView, so a stable seed
            // is derived from the immutable landing position without consuming
            // CombatWorld's gameplay RNG.
            const float radius=float(GetEquipment(EquipmentCategory::Special,"storm").number("radius"));
            const float grow=clamp01(age/0.5f),fade=clamp01((timer-age)/0.6f);
            const float scale=(0.3f+0.7f*(1.0f-std::pow(1.0f-grow,3.0f)))*(0.2f+0.8f*fade);
            std::uint32_t cloudRng=static_cast<std::uint32_t>(std::llround(std::abs(object.previous.x+271.0)*10007.0))
                ^(static_cast<std::uint32_t>(std::llround(std::abs(object.previous.z+619.0)*8191.0))<<1)
                ^(static_cast<std::uint32_t>(object.team)*0x9e3779b9u);
            if(!cloudRng)cloudRng=1;
            const auto cloudRandom=[&](){cloudRng^=cloudRng<<13;cloudRng^=cloudRng>>17;cloudRng^=cloudRng<<5;return float(double(cloudRng)/4294967296.0);};
            const auto cloudPuff=[&](float x,float y,float z,float radius,bool top){
                const float bob=cloudRandom()*2*pi;
                add({Shape::Cloud,pos+glm::vec3(x*scale,y*scale,z*scale),{},{0,1,0},col,radius*scale,0,time_,1,0.97f,top?1.0f:0.0f,bob});
            };
            cloudPuff(0,0,0,1.9f,false);
            for(int i=0;i<8;i++){
                const float a=float(i)/8.0f*2*pi+cloudRandom()*0.3f,r=1.7f+cloudRandom()*0.6f;
                const float y=-0.1f+cloudRandom()*0.2f,size=1.05f+cloudRandom()*0.45f;
                cloudPuff(std::cos(a)*r,y,std::sin(a)*r,size,false);
            }
            for(int i=0;i<5;i++){
                const float a=float(i)/5.0f*2*pi+0.4f,r=0.6f+cloudRandom()*0.8f;
                const float y=0.75f+cloudRandom()*0.3f,size=0.9f+cloudRandom()*0.4f;
                cloudPuff(std::cos(a)*r,y,std::sin(a)*r,size,true);
            }
            cloudPuff(0,1.15f,0,1.0f,true);
            if(dt>0)for(int i=0,n=int(radius*radius*3.2f*dt+random());i<n;i++){float a=random()*2*pi,rr=std::sqrt(random())*radius*0.95f;
                spawn({Shape::Drop,pos+glm::vec3(std::cos(a)*rr,-0.4f,std::sin(a)*rr),{0.35f,-19.0f-random()*4,0.12f},{0,1,0},glm::mix(col,glm::vec3(1),0.35f),0.022f+random()*0.012f,0,0,1,0.9f,0,random()});}
        } else if(object.kind=="strikeMissile") {
            const float flight=std::max(timer,0.01f),k=clamp01(age/flight);
            const glm::vec3 target(object.target),from=pos;
            const bool climbing=k<0.35f;
            const glm::vec3 missile=climbing?from+glm::vec3(0,30*std::pow(k/0.35f,2),0)
                :target+glm::vec3(0,34*std::pow(1-(k-0.35f)/0.65f,2)+0.3f,0);
            add({Shape::Missile,missile,{},climbing?glm::vec3(0,1,0):glm::vec3(0,-1,0),col,0.28f,0,0,1,1,0,0});
            add({Shape::WarningRing,target+glm::vec3(0,0.06f,0),{},{0,1,0},col,r,0,age,1,0.35f+0.35f*std::abs(std::sin(age*9)),0,0});
        } else if(object.kind=="tornado") {
            const float grow=clamp01(age/0.4f),opacity=grow*clamp01((timer-age)/0.6f),vertical=0.3f+0.7f*grow;
            Item inner{Shape::Swirl,pos,{},glm::vec3(0,1,0),col,r*0.8f,8,age,timer,opacity,0,0};
            inner.radius1=0.25f;inner.gravity=1.0f;inner.stretch=0.35f;inner.flags=1.9f;inner.expand=vertical;inner.rotationVelocity=4.0f;add(inner);
            Item outer{Shape::Swirl,pos,{},glm::vec3(0,1,0),glm::mix(col,glm::vec3(1),0.25f),r,9,age,timer,opacity,1,0};
            outer.radius1=0.4f;outer.gravity=1.15f;outer.stretch=0.5f;outer.flags=1.2f;outer.expand=vertical;outer.rotationVelocity=4.0f;add(outer);
        } else if(object.kind=="zooka") {
            const float height=float(GetEquipment(EquipmentCategory::Special,"zooka").number("height"));
            Item twister{Shape::Swirl,pos-glm::vec3(0,height*0.45f,0),{},glm::vec3(0,1,0),col,r*1.4f,height,age,timer,
                clamp01((timer-age)/0.25f),0,0};
            twister.radius1=0.3f;twister.gravity=1.0f;twister.stretch=0.12f;twister.flags=3.0f;twister.rotationVelocity=14.0f;add(twister);
        } else if(object.kind=="speaker") {
            Item speaker{Shape::Speaker,pos,{},dir,col,1.0f,0,age,1,1,object.phase=="charge"?0.0f:object.phase=="blast"?1.0f:2.0f,0};add(speaker);
            const auto& def=GetEquipment(EquipmentCategory::Special,"wail");
            const float charge=object.phase=="charge"?0.0f:1.0f;
            float width=0.18f,alpha=0.9f;
            if(object.phase=="charge"){
                const float k=age/float(def.number("charge",1.3));
                width=0.08f+0.1f*k;alpha=0.35f+0.35f*std::abs(std::sin(age*(8.0f+k*20.0f)));
            }else if(object.phase=="blast")width=r*std::min(1.0f,age/0.18f);
            else {const float k=age/0.6f;width=r*(1.0f-k);alpha=0.9f*(1.0f-k);}
            add({Shape::SourceBeam,glm::vec3(object.mouth),{},dir,col,width,float(object.range),age,1,alpha,charge,0});
        } else if(object.kind=="bubble"){
            Item bubble{Shape::Bubble,pos,{},glm::vec3(0,1,0),col,r,0,age,timer,1,object.held?0.8f:0.65f,0};
            bubble.flags=0;add(bubble);
        } else if(object.kind=="thrownStamp"){
            add({Shape::Stamp,pos,glm::vec3(object.vel),{0,1,0},col,1.15f,0,age,timer,1,0,0});
        }
        else if(object.kind=="crabShell")add({Shape::Orb,pos,{},{0,1,0},col,0.28f,0,age,1,1,0,0});
        else if(object.kind=="orb") {
            const float fuse=std::max(float(object.range),0.001f);
            const float k=object.phase=="fuse"?clamp01(age/fuse):0.0f;
            const float orbRadius=0.5f+std::sin(age*30.0f)*0.05f*k;
            const float haloRadius=0.8f+(r-0.8f)*k*k;
            const float haloAlpha=object.phase=="fuse"?0.35f+0.3f*std::abs(std::sin(age*(6.0f+k*20.0f))):0.6f;
            add({Shape::Orb,pos,{},{0,1,0},col,orbRadius,0,age,1,1,0,0});
            add({Shape::Bubble,pos,{},{0,1,0},col,haloRadius,0,age,1,haloAlpha,0,0});
        } else if(object.kind=="jetpackShot")add({Shape::Orb,pos,{},{0,1,0},col,0.28f,0,0,1,1,0,0});
    }
    for(const auto& device:combat.devices()) {
        if(!device.live||!near(device.pos,65))continue;
        const glm::vec3 pos(device.pos),col=ink(device.team);
        if(models_&&device.kind!="sonar")continue;
        if(device.kind=="sonar")add({Shape::Ring,pos,{},{0,1,0},col,float(device.radius),0,0,1,0.8f,7,0});
        else if(device.shield)add({Shape::Bubble,pos,{},{0,1,0},col,float(std::max(device.R,device.radius)),0,0,1,0.45f,0,0});
        else {
            add({Shape::Orb,pos,{},{0,1,0},col,float(std::clamp(device.radius*0.3,0.15,0.6)),0,0,1,0.9f,0,0});
            if(device.fuse>0)add({Shape::Ring,pos,{},{0,1,0},col,float(std::max(device.radius,0.6)),0,0,1,0.7f,3,0});
        }
    }

    const WorldFxCollider collider=[&](glm::vec3 from,glm::vec3 to,WorldFxHit& hit){
        const auto result=combat.geometry().segment(glm::dvec3(from),glm::dvec3(to),true);
        if(!result.hit)return false;
        hit.point=glm::vec3(result.point);hit.normal=glm::vec3(result.normal);return true;
    };
    WorldFxSpeckSink speck;WorldFxRippleSink ripple;
    if(paint_){
        speck=[&](const WorldFxHit& hit,glm::vec3 color,float size){
            if(size<0.014f||glm::distance(glm::dvec3(hit.point),camera)>26.0)return;
            const double d0=glm::length2(color-ink(0)),d1=glm::length2(color-ink(1));
            if(std::min(d0,d1)>0.01)return;
            SplatOptions options;options.kind=SplatKind::Speck;options.cosmetic=true;
            paint_->splat(glm::dvec3(hit.point)+glm::dvec3(hit.normal)*0.04,std::min(0.11,double(size)*1.7),d0<d1?0:1,random(),options);
        };
        ripple=[this](glm::dvec3 p,double a,double w,double s,double l){paint_->requestVisualRipple({p,a,w,s,l});};
    }
    UpdateWorldFxRecipe(live_,dt,random_,collider,-1.6f,paint_!=nullptr,true,speck,ripple);
}

void WorldEffects::Clear(){live_.clear();world_.clear();lastEvents_.clear();actors_.clear();time_=0;}
WorldEffects::Counts WorldEffects::counts() const {
    Counts c;c.world=world_.size();
    for(const auto& p:live_)switch(p.shape){case Shape::Drop:++c.drops;break;case Shape::Puff:++c.puffs;break;
        case Shape::Glow:++c.glows;break;case Shape::Ring:++c.rings;break;case Shape::Shell:++c.shells;break;case Shape::Beam:++c.beams;break;default:break;}
    return c;
}

void WorldEffects::initGpu() {
    if(program_)return;
    const GLuint vs=shader(GL_VERTEX_SHADER,vertexSource);
    GLuint gs=0,fs=0;
    try { gs=shader(GL_GEOMETRY_SHADER,geometrySource);fs=shader(GL_FRAGMENT_SHADER,fragmentSource); }
    catch(...) {glDeleteShader(vs);if(gs)glDeleteShader(gs);throw;}
    const GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,gs);glAttachShader(p,fs);glLinkProgram(p);
    glDeleteShader(vs);glDeleteShader(gs);glDeleteShader(fs);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[4096]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);glDeleteProgram(p);throw std::runtime_error(std::string("WorldEffects link: ")+log);}
    GLuint vao=0,vbo=0;glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);
    glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    for(GLuint i=0;i<5;++i){glEnableVertexAttribArray(i);glVertexAttribPointer(i,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(sizeof(glm::vec4)*i));}
    glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);
    program_=p;vao_=vao;vbo_=vbo;
}

void WorldEffects::initSourceGpu() {
    if(sourceProgram_)return;
    const GLuint vs=shader(GL_VERTEX_SHADER,sourceVertexSource);
    GLuint fs=0;
    try { fs=shader(GL_FRAGMENT_SHADER,sourceFragmentSource); }
    catch(...) { glDeleteShader(vs);throw; }
    const GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);
    glDeleteShader(vs);glDeleteShader(fs);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[4096]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);glDeleteProgram(p);throw std::runtime_error(std::string("WorldEffects source link: ")+log);}
    sourceProgram_=p;
}

const WorldEffects::GpuMesh& WorldEffects::sourceMesh(const char* kind,const char* role) {
    const std::string key=std::string(kind)+"/"+role;
    const auto found=sourceMeshes_.find(key);if(found!=sourceMeshes_.end())return *found->second;
    const auto asset=std::find_if(WorldFxMeshAssets.begin(),WorldFxMeshAssets.end(),[&](const auto& row){
        return key==std::string(row.kind)+"/"+row.role;
    });
    if(asset==WorldFxMeshAssets.end())throw std::runtime_error("Missing source World FX mesh: "+key);
    const auto& source=data_->mesh(asset->file);
    auto gpu=std::make_unique<GpuMesh>();glGenVertexArrays(1,&gpu->vao);glBindVertexArray(gpu->vao);
    const auto location=[](const std::string& name)->GLint {
        if(name=="position")return 0;if(name=="normal")return 1;if(name=="uv")return 2;
        if(name=="color")return 3;if(name=="aMat")return 4;return -1;
    };
    bool present[5]={false,false,false,false,false};
    for(const auto& attribute:source.attributes){
        const GLint at=location(attribute.name);if(at<0)continue;
        const GLuint loc=static_cast<GLuint>(at),components=std::min(4u,attribute.components);
        GLuint buffer=0;glGenBuffers(1,&buffer);gpu->buffers.push_back(buffer);
        glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,attribute.values.size()*sizeof(float),attribute.values.data(),GL_STATIC_DRAW);
        glEnableVertexAttribArray(loc);glVertexAttribPointer(loc,static_cast<GLint>(components),GL_FLOAT,GL_FALSE,0,nullptr);present[loc]=true;
    }
    glGenBuffers(1,&gpu->index);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,gpu->index);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,source.indices.size()*sizeof(std::uint32_t),source.indices.data(),GL_STATIC_DRAW);
    gpu->count=static_cast<GLsizei>(source.indices.size());glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);
    auto* raw=gpu.get();auto inserted=sourceMeshes_.emplace(key,std::move(gpu));
    // A disabled array takes its default from current generic vertex state. Set
    // it before every draw below; the array enable/format is VAO state.
    (void)present;(void)raw;
    return *inserted.first->second;
}

int WorldEffects::drawSourceItems(const glm::mat4& view,const glm::mat4& projection,const glm::vec3& camera,const CharacterLightState* lights) {
    if(world_.empty())return 0;
    const auto isSource=[](WorldFxShape shape){return shape==Shape::Swirl||shape==Shape::Bubble||shape==Shape::Orb
        ||shape==Shape::Stamp||shape==Shape::Speaker||shape==Shape::Missile||shape==Shape::Cloud
        ||shape==Shape::WarningRing||shape==Shape::SourceBeam;};
    if(std::none_of(world_.begin(),world_.end(),[&](const auto& item){return isSource(item.shape);}))return 0;
    initSourceGpu();
    GLint oldProgram=0,oldVao=0,oldBuffer=0,oldSrc=0,oldDst=0,oldSrcAlpha=0,oldDstAlpha=0,oldEquation=0,oldEquationAlpha=0;
    GLboolean oldDepth=GL_TRUE;const GLboolean blended=glIsEnabled(GL_BLEND),culled=glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldBuffer);glGetIntegerv(GL_BLEND_SRC_RGB,&oldSrc);glGetIntegerv(GL_BLEND_DST_RGB,&oldDst);glGetIntegerv(GL_BLEND_SRC_ALPHA,&oldSrcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&oldDstAlpha);glGetIntegerv(GL_BLEND_EQUATION_RGB,&oldEquation);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&oldEquationAlpha);
    glGetBooleanv(GL_DEPTH_WRITEMASK,&oldDepth);
    glEnable(GL_BLEND);glDisable(GL_CULL_FACE);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(sourceProgram_);
    const GLint modelLoc=glGetUniformLocation(sourceProgram_,"uModel"),viewLoc=glGetUniformLocation(sourceProgram_,"uView");
    const GLint projectionLoc=glGetUniformLocation(sourceProgram_,"uProjection"),normalLoc=glGetUniformLocation(sourceProgram_,"uNormalMatrix");
    const GLint modeLoc=glGetUniformLocation(sourceProgram_,"uMode"),timeLoc=glGetUniformLocation(sourceProgram_,"uTime");
    const GLint bottomLoc=glGetUniformLocation(sourceProgram_,"uBottom"),topLoc=glGetUniformLocation(sourceProgram_,"uTop");
    const GLint wobbleLoc=glGetUniformLocation(sourceProgram_,"uWobble"),speedLoc=glGetUniformLocation(sourceProgram_,"uSpeed");
    const GLint chargeLoc=glGetUniformLocation(sourceProgram_,"uCharge"),alphaLoc=glGetUniformLocation(sourceProgram_,"uAlpha");
    const GLint hitLoc=glGetUniformLocation(sourceProgram_,"uHit"),colorLoc=glGetUniformLocation(sourceProgram_,"uColor");
    const GLint tintLoc=glGetUniformLocation(sourceProgram_,"uTint"),emissiveLoc=glGetUniformLocation(sourceProgram_,"uEmissive");
    glUniformMatrix4fv(viewLoc,1,GL_FALSE,glm::value_ptr(view));glUniformMatrix4fv(projectionLoc,1,GL_FALSE,glm::value_ptr(projection));
    const glm::vec3 white(1.0f);
    int drawCalls=0;
    const auto part=[&](const char* kind,const char* role,const glm::mat4& model,int mode,glm::vec3 color,float alpha,
                        float time=0,float bottom=1,float top=1,float wobble=0,float speed=1,float charge=0,float hit=0,
                        glm::vec3 tint=glm::vec3(1),glm::vec3 emissive=glm::vec3(0)){
        const auto& gpu=sourceMesh(kind,role);
        const bool transparent=mode!=0||alpha<0.999f||std::string(kind)=="geometry"&&std::string(role)=="stormCloud";
        glDepthMask(transparent?GL_FALSE:GL_TRUE);
        glUniform1i(modeLoc,mode);glUniformMatrix4fv(modelLoc,1,GL_FALSE,glm::value_ptr(model));
        const glm::mat3 normal=glm::transpose(glm::inverse(glm::mat3(view*model)));
        glUniformMatrix3fv(normalLoc,1,GL_FALSE,glm::value_ptr(normal));glUniform1f(timeLoc,time);
        glUniform1f(bottomLoc,bottom);glUniform1f(topLoc,top);glUniform1f(wobbleLoc,wobble);
        glUniform1f(speedLoc,speed);glUniform1f(chargeLoc,charge);glUniform1f(alphaLoc,alpha);glUniform1f(hitLoc,hit);
        glUniform3fv(colorLoc,1,glm::value_ptr(color));glUniform3fv(tintLoc,1,glm::value_ptr(tint));glUniform3fv(emissiveLoc,1,glm::value_ptr(emissive));
        glBindVertexArray(gpu.vao);
        // Exported meshes without authored colors/material ids use the source
        // shader defaults: white vertex color, plastic surface class zero.
        glVertexAttrib3f(3,1,1,1);glVertexAttrib1f(4,0);
        glDrawElements(GL_TRIANGLES,gpu.count,GL_UNSIGNED_INT,nullptr);++drawCalls;
    };
    const auto orientZ=[](glm::vec3 direction){
        direction=unit(direction);return glm::toMat4(glm::rotation(glm::vec3(0,0,1),direction));
    };
    const auto meshModel=[&](glm::vec3 pos,glm::vec3 direction,glm::vec3 scale){
        return glm::translate(glm::mat4(1),pos)*orientZ(direction)*glm::scale(glm::mat4(1),scale);
    };
    for(const auto& item:world_){
        if(!isSource(item.shape)||item.alpha<=0)continue;
        const glm::vec3 pos=item.pos,color=item.color;
        if(models_&&lights&&(item.shape==Shape::Cloud||item.shape==Shape::Missile||item.shape==Shape::Stamp||item.shape==Shape::Speaker)){
            const int team=color.b>color.r?1:0;std::string kind;glm::mat4 model(1);
            if(item.shape==Shape::Cloud){kind=item.style<.5f?"stormBase":"stormTop";const float bob=std::sin(time_*1.6f+item.seed)*.0025f;model=glm::translate(glm::mat4(1),pos+glm::vec3(0,bob,0))*glm::scale(glm::mat4(1),glm::vec3(item.radius,item.radius*.68f,item.radius));}
            else if(item.shape==Shape::Missile){kind="missile";model=meshModel(pos,item.normal,{1,1,1});}
            else if(item.shape==Shape::Stamp){kind="stamp";model=glm::translate(glm::mat4(1),pos)*glm::rotate(glm::mat4(1),std::atan2(item.vel.x,item.vel.z),glm::vec3(0,1,0))*glm::rotate(glm::mat4(1),item.age*14,glm::vec3(1,0,0))*glm::scale(glm::mat4(1),glm::vec3(1.6f));}
            else {kind="speaker";const float k=clamp01(item.age/.25f),scale=item.style<1.5f?.2f+.8f*(1-std::pow(1-k,3.f)):item.style<2.5f?1.f:std::max(.01f,1-item.age/.6f);model=glm::translate(glm::mat4(1),pos)*glm::rotate(glm::mat4(1),std::atan2(item.normal.x,item.normal.z),glm::vec3(0,1,0))*glm::scale(glm::mat4(1),glm::vec3(scale));}
            drawCalls+=models_->DrawModel(kind,team,glm::dmat4(model),item.age,glm::dmat4(view),glm::dmat4(projection),glm::dvec3(camera),*lights,false,item.shape==Shape::Cloud?item.alpha/.97:1,item.style);
            glUseProgram(sourceProgram_);continue;
        }
        if(item.shape==Shape::Swirl){
            const float grow=item.expand>0?item.expand:1.0f;
            const glm::mat4 model=glm::translate(glm::mat4(1),pos)
                *glm::rotate(glm::mat4(1),item.age*item.rotationVelocity,glm::vec3(0,1,0))
                *glm::scale(glm::mat4(1),glm::vec3(item.radius,item.length*(0.3f+0.7f*grow),item.radius));
            part("geometry","funnel",model,1,color,item.alpha,item.age,item.radius1,item.gravity,item.stretch,item.flags);
        }else if(item.shape==Shape::SourceBeam){
            const glm::mat4 model=meshModel(pos,item.normal,{item.radius,item.radius,item.length});
            part("geometry","speakerBeam",model,2,color,item.alpha,item.age,1,1,0,1,item.style);
        }else if(item.shape==Shape::Bubble){
            const auto model=glm::translate(glm::mat4(1),pos)*glm::scale(glm::mat4(1),glm::vec3(item.radius));
            part("geometry","sphere4",model,3,color,item.alpha,item.age,1,1,0,1,0,item.flags);
        }else if(item.shape==Shape::Orb){
            const auto model=glm::translate(glm::mat4(1),pos)*glm::scale(glm::mat4(1),glm::vec3(item.radius));
            part("geometry","sphere4",model,0,color,item.alpha,item.age,1,1,0,1,0,0,color);
        }else if(item.shape==Shape::Cloud){
            const float bob=std::sin(time_*1.6f+item.seed)*0.0025f;
            const float tint=item.style<0.5f?0.12f:0.55f;
            const glm::vec3 cloudColor=glm::mix(color,white,tint);
            const auto model=glm::translate(glm::mat4(1),pos+glm::vec3(0,bob,0))*glm::scale(glm::mat4(1),glm::vec3(item.radius,item.radius*0.68f,item.radius));
            part("geometry","stormCloud",model,0,cloudColor,item.alpha,0,1,1,0,1,0,0,white,cloudColor*0.12f);
        }else if(item.shape==Shape::WarningRing){
            const auto model=glm::translate(glm::mat4(1),pos)*glm::rotate(glm::mat4(1),item.age*2.0f,glm::vec3(0,1,0))*glm::scale(glm::mat4(1),glm::vec3(item.radius));
            part("geometry","warningRing",model,0,color,item.alpha,item.age,1,1,0,1,0,0,color,color*0.1f);
        }else if(item.shape==Shape::Missile){
            const auto model=meshModel(pos,item.normal,{1,1,1});
            part("missile","body",model,0,color,1);
            part("missile","ink",model,0,color,1,0,1,1,0,1,0,0,color);
            part("missile","glow",model,0,color,1,0,1,1,0,1,0,0,color,color*0.8f);
        }else if(item.shape==Shape::Stamp){
            const float yaw=std::atan2(item.vel.x,item.vel.z);
            const auto model=glm::translate(glm::mat4(1),pos)*glm::rotate(glm::mat4(1),yaw,glm::vec3(0,1,0))
                *glm::rotate(glm::mat4(1),item.age*14.0f,glm::vec3(1,0,0))*glm::scale(glm::mat4(1),glm::vec3(1.6f));
            part("stamp","body",model,0,color,1);
            part("stamp","ink",model,0,color,1,0,1,1,0,1,0,0,color);
        }else if(item.shape==Shape::Speaker){
            const float yaw=std::atan2(item.normal.x,item.normal.z),k=clamp01(item.age/0.25f);
            const float groupScale=item.style<1.5f?0.2f+0.8f*(1.0f-std::pow(1.0f-k,3.0f))
                :item.style<2.5f?1.0f:std::max(0.01f,1.0f-item.age/0.6f);
            const auto model=glm::translate(glm::mat4(1),pos)*glm::rotate(glm::mat4(1),yaw,glm::vec3(0,1,0))*glm::scale(glm::mat4(1),glm::vec3(groupScale));
            part("speaker","body",model,0,color,1);
            part("speaker","ink",model,0,color,1,0,1,1,0,1,0,0,color);
            part("speaker","glow",model,0,color,1,0,1,1,0,1,0,0,color,color*0.8f);
            const auto& coneAsset=*std::find_if(WorldFxMeshAssets.begin(),WorldFxMeshAssets.end(),[](const auto& row){return std::string(row.kind)=="speaker"&&std::string(row.role)=="cone";});
            const glm::vec3 pivot(coneAsset.pivot);
            float pulse=1.0f;
            if(item.style<0.5f)pulse=1.0f+std::sin(item.age*40.0f)*0.05f*clamp01(item.age/1.3f);
            else if(item.style<1.5f)pulse=1.0f+std::sin(item.age*60.0f)*0.12f;
            const auto coneModel=model*glm::translate(glm::mat4(1),pivot)*glm::scale(glm::mat4(1),glm::vec3(1,1,pulse))*glm::translate(glm::mat4(1),-pivot);
            part("speaker","cone",coneModel,0,color,1);
            part("speaker","coneInk",coneModel,0,color,1,0,1,1,0,1,0,0,color);
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER,GLuint(oldBuffer));glBindVertexArray(GLuint(oldVao));glUseProgram(GLuint(oldProgram));
    glDepthMask(oldDepth);glBlendFuncSeparate(GLenum(oldSrc),GLenum(oldDst),GLenum(oldSrcAlpha),GLenum(oldDstAlpha));glBlendEquationSeparate(GLenum(oldEquation),GLenum(oldEquationAlpha));
    if(culled)glEnable(GL_CULL_FACE);if(!blended)glDisable(GL_BLEND);
    return drawCalls;
}

int WorldEffects::Draw(const glm::mat4& view,const glm::mat4& projection,const glm::vec3& camera,const CharacterLightState* lights) {
    if(live_.empty()&&world_.empty())return 0;
    std::vector<Vertex> vertices,glows;vertices.reserve(live_.size()+world_.size());
    const auto sourceShape=[](WorldFxShape shape){return shape==Shape::Swirl||shape==Shape::Bubble||shape==Shape::Orb
        ||shape==Shape::Stamp||shape==Shape::Speaker||shape==Shape::Missile||shape==Shape::Cloud
        ||shape==Shape::WarningRing||shape==Shape::SourceBeam;};
    const auto collect=[&](const std::vector<Item>& items) {
        for(const auto& p:items) {
            if(sourceShape(p.shape)||p.radius<=0||p.alpha<=0)continue;
            const bool glow=p.shape==Shape::Glow;
            auto& batch=glow?glows:vertices;
            batch.push_back({glm::vec4(p.pos,glow?p.renderRadius:p.radius),glm::vec4(unit(p.normal),p.length),
                glm::vec4(p.color,glow?p.renderAlpha:p.alpha),glm::vec4(float(static_cast<int>(p.shape)),
                    p.life>0?clamp01(p.age/p.life):0,glow?p.kind:p.style,p.seed),glm::vec4(p.vel,0)});
        }
    };
    collect(live_);collect(world_);
    int calls=0;
    if(!vertices.empty()||!glows.empty()){
        initGpu();
        GLint oldProgram=0,oldVao=0,oldBuffer=0,oldSrc=0,oldDst=0,oldSrcAlpha=0,oldDstAlpha=0,oldEquation=0,oldEquationAlpha=0;
        GLboolean oldDepth=GL_TRUE;
        const GLboolean blended=glIsEnabled(GL_BLEND),culled=glIsEnabled(GL_CULL_FACE);
        glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldBuffer);glGetIntegerv(GL_BLEND_SRC_RGB,&oldSrc);glGetIntegerv(GL_BLEND_DST_RGB,&oldDst);glGetIntegerv(GL_BLEND_SRC_ALPHA,&oldSrcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&oldDstAlpha);glGetIntegerv(GL_BLEND_EQUATION_RGB,&oldEquation);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&oldEquationAlpha);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&oldDepth);
        glEnable(GL_BLEND);glDisable(GL_CULL_FACE);glDepthMask(GL_FALSE);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(program_);glUniformMatrix4fv(glGetUniformLocation(program_,"view"),1,GL_FALSE,glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(program_,"projection"),1,GL_FALSE,glm::value_ptr(projection));
        glUniform1f(glGetUniformLocation(program_,"time"),time_);
        glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,vbo_);
        glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(vertices.size()*sizeof(Vertex)),vertices.data(),GL_STREAM_DRAW);
        if(!vertices.empty())glDrawArrays(GL_POINTS,0,GLsizei(vertices.size()));
        if(!glows.empty()){
            // fx.js GLOW_FRAG emits premultiplied light into an additive pass.
            glBlendFunc(GL_ONE,GL_ONE);
            glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(glows.size()*sizeof(Vertex)),glows.data(),GL_STREAM_DRAW);
            glDrawArrays(GL_POINTS,0,GLsizei(glows.size()));
        }
        glBindBuffer(GL_ARRAY_BUFFER,GLuint(oldBuffer));glBindVertexArray(GLuint(oldVao));glUseProgram(GLuint(oldProgram));
        glDepthMask(oldDepth);glBlendFuncSeparate(GLenum(oldSrc),GLenum(oldDst),GLenum(oldSrcAlpha),GLenum(oldDstAlpha));glBlendEquationSeparate(GLenum(oldEquation),GLenum(oldEquationAlpha));
        if(culled)glEnable(GL_CULL_FACE);if(!blended)glDisable(GL_BLEND);
        ++calls;
    }
    calls+=drawSourceItems(view,projection,camera,lights);
    return calls;
}
}
