#define GLM_ENABLE_EXPERIMENTAL
#include "DeviceRenderer.hpp"
#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"
#include "ArsenalData.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <fstream>
#include <map>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

namespace restoration {
namespace {
glm::dmat4 rotate(glm::dmat4 m,double x,double y,double z){m=glm::rotate(m,x,glm::dvec3(1,0,0));m=glm::rotate(m,y,glm::dvec3(0,1,0));return glm::rotate(m,z,glm::dvec3(0,0,1));}
glm::dmat4 along(glm::dvec3 direction){return glm::toMat4(glm::rotation(glm::dvec3(0,0,1),glm::length(direction)>1e-8?glm::normalize(direction):glm::dvec3(0,0,1)));}
}
struct DeviceRenderer::Impl {
    struct Node {int parent=-1;std::string tag,file,material;bool visible=true,casts=false,transparent=false,depthWrite=true;int side=0,blend=1;double phase=0;glm::dmat4 matrix{1};};
    struct Mesh {GLuint vao=0,index=0;std::vector<GLuint> buffers;GLsizei count=0;~Mesh(){if(vao)glDeleteVertexArrays(1,&vao);if(index)glDeleteBuffers(1,&index);if(!buffers.empty())glDeleteBuffers(static_cast<GLsizei>(buffers.size()),buffers.data());}};
    struct Program {std::unique_ptr<CharacterShader> shader;std::map<std::string,std::vector<double>> defaults;};
    struct ActorMotion {double leg=0,gun=0,roll=0,speed=0;};
    struct DeviceMotion {glm::dvec3 previous{0};double roll=0,prop=0,fin=0,open=0,glow=1.2,walk=0,moveSpeed=0,spin=0,spinSpeed=30,blink=0,tiltX=0,tiltZ=0;};
    struct TracerTail {int team=0;glm::dvec3 direction{0,0,1};std::vector<std::pair<glm::dvec3,double>> points;bool active=false;double lastSeen=0;};
    struct RibbonGpu {
        GLuint vao=0,position=0,info=0,index=0;
        ~RibbonGpu(){if(vao)glDeleteVertexArrays(1,&vao);const GLuint buffers[]={position,info,index};glDeleteBuffers(3,buffers);}
    };
    std::filesystem::path assets;
    std::map<std::string,std::vector<Node>> models;
    std::map<std::string,Program> programs;
    std::map<std::pair<std::string,std::string>,std::unique_ptr<Mesh>> meshes;
    std::vector<CombatDevice> devices;
    std::unordered_map<const PlayerSimulation*,ActorMotion> motion;
    std::unordered_map<std::uint64_t,DeviceMotion> deviceMotion;
    std::unordered_map<std::uint64_t,TracerTail> tracerTails;
    std::unique_ptr<CharacterShader> ribbonShader;
    std::unique_ptr<RibbonGpu> ribbonGpu;
    GLuint dfg=0;double dt=0,clock=0;bool clockInitialized=false;
    explicit Impl(std::filesystem::path path):assets(std::move(path)){
        std::ifstream input(assets/"models.txt");if(!input)throw std::runtime_error("Missing source device model catalog");std::string kind;int team=0;std::size_t count=0;
        while(input>>kind>>team>>count){if(team<0||team>1||!count||count>4096)throw std::runtime_error("Invalid source device model header");auto& nodes=models[kind+std::to_string(team)];
            for(std::size_t i=0;i<count;i++){Node n;int visible=0,casts=0,transparent=0,depth=0;if(!(input>>n.parent>>n.tag>>n.file>>n.material>>visible>>casts>>transparent>>depth>>n.side>>n.blend>>n.phase)||n.parent>=int(i)||n.parent< -1||!std::isfinite(n.phase))throw std::runtime_error("Invalid source device node");
                n.visible=visible!=0;n.casts=casts!=0;n.transparent=transparent!=0;n.depthWrite=depth!=0;
                for(int c=0;c<4;c++)for(int r=0;r<4;r++)if(!(input>>n.matrix[c][r])||!std::isfinite(n.matrix[c][r]))throw std::runtime_error("Invalid source device matrix");
                if(n.file!="-"&&(n.file.size()!=69||n.file.substr(64)!=".mesh"||n.file.substr(0,64).find_first_not_of("0123456789abcdef")!=std::string::npos))throw std::runtime_error("Invalid source device mesh path");
                nodes.push_back(n);
            }
        }
        glGenTextures(1,&dfg);glBindTexture(GL_TEXTURE_2D,dfg);glTexImage2D(GL_TEXTURE_2D,0,GL_RG16F,16,16,0,GL_RG,GL_HALF_FLOAT,CharacterDfg.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    ~Impl(){if(dfg)glDeleteTextures(1,&dfg);}
    void initRibbon(){
        if(ribbonShader)return;
        ribbonShader=std::make_unique<CharacterShader>(CharacterShader::readSource(assets/"tracer-ribbon.vert"),CharacterShader::readSource(assets/"tracer-ribbon.frag"),true,true);
        GLint oldVao=0,oldArray=0;glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldArray);
        auto gpu=std::make_unique<RibbonGpu>();glGenVertexArrays(1,&gpu->vao);glBindVertexArray(gpu->vao);
        glGenBuffers(1,&gpu->position);glBindBuffer(GL_ARRAY_BUFFER,gpu->position);const auto positionAt=ribbonShader->attribute("position");
        if(positionAt<0)throw std::runtime_error("Source tracer ribbon shader has no position attribute");glEnableVertexAttribArray(static_cast<GLuint>(positionAt));glVertexAttribPointer(static_cast<GLuint>(positionAt),3,GL_FLOAT,GL_FALSE,0,nullptr);
        glGenBuffers(1,&gpu->info);glBindBuffer(GL_ARRAY_BUFFER,gpu->info);const auto infoAt=ribbonShader->attribute("aInfo");
        if(infoAt<0)throw std::runtime_error("Source tracer ribbon shader has no aInfo attribute");glEnableVertexAttribArray(static_cast<GLuint>(infoAt));glVertexAttribPointer(static_cast<GLuint>(infoAt),2,GL_FLOAT,GL_FALSE,0,nullptr);
        glGenBuffers(1,&gpu->index);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,gpu->index);glBindVertexArray(static_cast<GLuint>(oldVao));glBindBuffer(GL_ARRAY_BUFFER,static_cast<GLuint>(oldArray));ribbonGpu=std::move(gpu);
    }
    int drawRibbon(const TracerTail& tail,double now,double life,const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye){
        const std::size_t count=tail.points.size();if(count<2)return 0;initRibbon();
        std::vector<glm::vec3> positions(count*2);std::vector<glm::vec2> info(count*2);std::vector<std::uint32_t> indices((count-1)*6);
        const glm::dvec3 camera=eye;
        for(std::size_t i=0;i<count;i++){
            const auto& p=tail.points[i].first;auto tangent=tail.points[std::min(count-1,i+1)].first-tail.points[i?i-1:i].first;
            if(glm::dot(tangent,tangent)<1e-8)tangent=tail.direction;
            auto side=glm::cross(tangent,camera-p);if(glm::dot(side,side)<1e-8)side={0,1,0};
            const double age=std::clamp((now-tail.points[i].second)/life,0.0,1.0),width=.085*(1+.6*(1-age));side=glm::normalize(side)*width;
            positions[i*2]=glm::vec3(p+side);positions[i*2+1]=glm::vec3(p-side);
            info[i*2]={-1,static_cast<float>(tail.points[i].second)};info[i*2+1]={1,static_cast<float>(tail.points[i].second)};
        }
        for(std::size_t i=0;i+1<count;i++){const auto a=static_cast<std::uint32_t>(i*2);const auto at=i*6;indices[at]=a;indices[at+1]=a+1;indices[at+2]=a+2;indices[at+3]=a+1;indices[at+4]=a+3;indices[at+5]=a+2;}
        GLint oldProgram=0,oldVao=0,oldArray=0,oldElement=0,oldSrc=0,oldDst=0;GLboolean oldDepth=GL_TRUE;
        const GLboolean blended=glIsEnabled(GL_BLEND),culled=glIsEnabled(GL_CULL_FACE);
        glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldArray);glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&oldElement);glGetIntegerv(GL_BLEND_SRC_RGB,&oldSrc);glGetIntegerv(GL_BLEND_DST_RGB,&oldDst);glGetBooleanv(GL_DEPTH_WRITEMASK,&oldDepth);
        glEnable(GL_DEPTH_TEST);glEnable(GL_BLEND);glDisable(GL_CULL_FACE);glDepthMask(GL_FALSE);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        auto& shader=*ribbonShader;shader.use();shader.matrices(glm::dmat4(1),view,projection,eye);shader.uniform("uTime",now);shader.uniform("uLife",life);
        const glm::dvec3 color=tail.team==1?glm::dvec3(.028426039499072558,.10461648408208657,1):glm::dvec3(1,.2541520943200296,.0069954101845983935);
        shader.uniform("uColor",{color.x,color.y,color.z});glBindVertexArray(ribbonGpu->vao);
        glBindBuffer(GL_ARRAY_BUFFER,ribbonGpu->position);glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(positions.size()*sizeof(glm::vec3)),positions.data(),GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER,ribbonGpu->info);glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(info.size()*sizeof(glm::vec2)),info.data(),GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ribbonGpu->index);glBufferData(GL_ELEMENT_ARRAY_BUFFER,static_cast<GLsizeiptr>(indices.size()*sizeof(std::uint32_t)),indices.data(),GL_STREAM_DRAW);
        glDrawElements(GL_TRIANGLES,static_cast<GLsizei>(indices.size()),GL_UNSIGNED_INT,nullptr);
        glBindVertexArray(static_cast<GLuint>(oldVao));glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,static_cast<GLuint>(oldElement));glBindBuffer(GL_ARRAY_BUFFER,static_cast<GLuint>(oldArray));glUseProgram(static_cast<GLuint>(oldProgram));glDepthMask(oldDepth);glBlendFunc(static_cast<GLenum>(oldSrc),static_cast<GLenum>(oldDst));if(culled)glEnable(GL_CULL_FACE);if(!blended)glDisable(GL_BLEND);
        return 1;
    }
    Program& program(const std::string& key){
        const auto found=programs.find(key);if(found!=programs.end())return found->second;
        if(key.size()!=20||key.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid source device material key");
        Program p;p.shader=std::make_unique<CharacterShader>(CharacterShader::readSource(assets/(key+".vert")),CharacterShader::readSource(assets/(key+".frag")),true,true);
        std::ifstream file(assets/(key+".uniforms"));if(!file)throw std::runtime_error("Missing source device material uniforms");std::string name;std::size_t count;
        while(file>>name>>count){if(!count||count>4096)throw std::runtime_error("Invalid device uniform shape");std::vector<double> values(count);for(auto& v:values)if(!(file>>v)||!std::isfinite(v))throw std::runtime_error("Invalid device uniform value");p.defaults.emplace(name,std::move(values));}
        return programs.emplace(key,std::move(p)).first->second;
    }
    Mesh& mesh(const Node& node,CharacterShader& shader){
        const auto key=std::make_pair(node.file,node.material);const auto found=meshes.find(key);if(found!=meshes.end())return *found->second;
        const auto source=CharacterData::LoadMesh(assets/node.file);auto m=std::make_unique<Mesh>();glGenVertexArrays(1,&m->vao);glBindVertexArray(m->vao);
        for(const auto& a:source.attributes){const auto at=shader.attribute(a.name);if(at<0)continue;GLuint b=0;glGenBuffers(1,&b);m->buffers.push_back(b);glBindBuffer(GL_ARRAY_BUFFER,b);glBufferData(GL_ARRAY_BUFFER,a.values.size()*4,a.values.data(),GL_STATIC_DRAW);glEnableVertexAttribArray(at);glVertexAttribPointer(at,a.components,GL_FLOAT,GL_FALSE,0,nullptr);}
        glGenBuffers(1,&m->index);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,m->index);glBufferData(GL_ELEMENT_ARRAY_BUFFER,source.indices.size()*4,source.indices.data(),GL_STATIC_DRAW);m->count=static_cast<GLsizei>(source.indices.size());return *meshes.emplace(key,std::move(m)).first->second;
    }
    int draw(const std::string& kind,int team,const glm::dmat4& root,double age,const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& lights,bool shadow,const CombatDevice* device=nullptr,const SpecialPose* special=nullptr,const ActorMotion* actorMotion=nullptr,double opacity=1,double phase=0){
        const auto found=models.find(kind+std::to_string(team));if(found==models.end())return 0;const auto& nodes=found->second;
        const DeviceMotion* motionState=nullptr;if(device){const auto state=deviceMotion.find(device->id);if(state!=deviceMotion.end())motionState=&state->second;}
        std::vector<glm::dmat4> world;std::vector<bool> visible;std::vector<std::size_t> render;world.reserve(nodes.size());visible.reserve(nodes.size());
        const double speed=device?std::hypot(device->vel.x,device->vel.z):actorMotion?actorMotion->speed:0;
        for(std::size_t i=0;i<nodes.size();i++){
            const auto& n=nodes[i];auto local=n.matrix;bool show=n.visible;
            if(kind=="mistCloud"&&n.parent==0)local=glm::scale(rotate(local,0,age*.3,0),glm::dvec3(1+.12*std::sin(age*1.7+n.phase)));
            if(kind=="curtainSheet"&&n.tag=="sheet"){const double k=std::clamp(age/.25,0.,1.);local[3].y=(local[3].y-.05)*k+.05;local=glm::scale(local,glm::dvec3(1,k,1));}
            if(kind=="speaker"&&n.tag=="cone"){const double pulse=phase<.5?1+std::sin(age*40)*.05*std::clamp(age/1.3,0.,1.):phase<1.5?1+std::sin(age*60)*.12:1;local=glm::scale(local,glm::dvec3(1,1,pulse));}
            if(kind=="crab"&&special){if(n.tag=="hullG")show=!special->roll;else if(n.tag=="ball"){show=special->roll;local=rotate(local,actorMotion?actorMotion->roll:0,device?device->heading:0,0);}else if(n.tag=="gunSpin")local=rotate(local,0,0,actorMotion?actorMotion->gun:age*2);else if(n.tag.rfind("legs",0)==0){const int index=n.tag.back()-'0';local=rotate(local,std::sin((actorMotion?actorMotion->leg:age)*5+index*1.7)*.28*std::min(1.,speed/1.5),0,0);}}
            if(device){
                if(kind=="brolly"&&n.tag=="inner"){local[3].y=.76+(device->h0-.76)*std::max(0.,1-age/.22)+.012*std::sin(age*11);local=rotate(local,0,0,.035*std::sin(age*7.3));}
                if(n.tag=="spin")local=rotate(local,0,kind=="boomerang"&&motionState?motionState->spin:device->spin,0);
                if(kind=="boomerang"&&n.tag=="tilt"&&motionState)local=rotate(local,motionState->tiltX,device->heading,motionState->tiltZ);
                if(kind=="waddle"){
                    const double k=std::min(1.2,(motionState?motionState->moveSpeed:speed)/std::max(.01,device->speed)),sw=std::sin((motionState?motionState->walk:0)*6.283185307179586);
                    if(n.tag=="rock")local=rotate(local,-.1*k,0,.17*sw*k);
                    if(n.tag=="legs0"||n.tag=="legs1"){const double side=n.tag=="legs0"?1:-1;local[3].y+=std::max(0.,-side*sw)*.009*k;local=rotate(local,side*.6*sw*k,0,0);}
                    if(n.tag=="model")local[3].y+=std::abs(sw)*.012*k;
                }
                if(kind=="torpedo"){
                    const double fin=motionState?motionState->fin:0;
                    if(n.tag.rfind("fins",0)==0)local=rotate(local,0,0,-1.8*fin);
                    if(n.tag=="prop"){const double pk=motionState?motionState->open:0;local=glm::scale(rotate(local,0,0,motionState?motionState->prop:0),glm::dvec3(.38+.62*pk,.38+.62*pk,1));}
                }
                if(kind=="boomerang"&&n.tag=="blur")show=true;
            }
            world.push_back((n.parent<0?root:world[n.parent])*local);visible.push_back(show&&(n.parent<0||visible[n.parent]));
            if(visible.back()&&n.file!="-"&&(!shadow||n.casts))render.push_back(i);
        }
        std::stable_sort(render.begin(),render.end(),[&](auto a,auto b){return nodes[a].transparent<nodes[b].transparent;});int draws=0;
        glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,dfg);
        for(const auto i:render){const auto& n=nodes[i];auto& p=program(n.material);auto& shader=*p.shader;shader.use();for(const auto& [k,v]:p.defaults)shader.uniform(k,v);
            shader.uniform("uTime",age);shader.uniform("uT",age);shader.uniform("uAng",-age*(device?device->spin:1)*.12);shader.uniform("uAlpha",kind=="boomerang"?.72:1.0);shader.uniform("dfgLUT",6);shader.matrices(world[i],view,projection,eye);shader.lights(lights,view);
            shader.uniform("uLife",phase);
            const auto alpha=p.defaults.find("opacity");if(alpha!=p.defaults.end()&&alpha->second.size()==1)shader.uniform("opacity",alpha->second[0]*opacity);
            if(kind=="waddleRing"){shader.uniform("uGrow",std::clamp(age/.28,0.,1.));shader.uniform("uLock",phase);shader.uniform("uSweep",1-phase);shader.uniform("uAlpha",opacity);}
            if(kind=="torpedoRing")shader.uniform("uAlpha",opacity);
            if(motionState&&kind=="torpedo"){const auto emission=p.defaults.find("emissive");if(emission!=p.defaults.end()){auto v=emission->second;for(auto& x:v)x*=motionState->glow/1.2;shader.uniform("emissive",v);}}
            if(n.transparent&&!shadow){glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,n.blend==2?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);}else glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(shadow||n.depthWrite?GL_TRUE:GL_FALSE);if(n.side==2)glDisable(GL_CULL_FACE);else{glEnable(GL_CULL_FACE);glCullFace(n.side==1?GL_FRONT:GL_BACK);}
            const auto& gpu=mesh(n,shader);glBindVertexArray(gpu.vao);glDrawElements(GL_TRIANGLES,gpu.count,GL_UNSIGNED_INT,nullptr);++draws;
        }
        glBindVertexArray(0);glDepthMask(GL_TRUE);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);return draws;
    }
};
DeviceRenderer::DeviceRenderer(std::filesystem::path assets):impl_(std::make_unique<Impl>(std::move(assets))){}
DeviceRenderer::~DeviceRenderer()=default;
void DeviceRenderer::Update(double dt,const CombatWorld& world,glm::dvec3 eye){
    impl_->dt=std::clamp(dt,0.,.1);const double now=world.time();
    if(impl_->clockInitialized&&now+1e-9<impl_->clock){impl_->tracerTails.clear();impl_->deviceMotion.clear();impl_->motion.clear();}
    impl_->clock=now;impl_->clockInitialized=true;
    impl_->devices.clear();for(const auto& d:world.devices())if(d.live&&glm::distance(d.pos,eye)<65)impl_->devices.push_back(d);
    std::unordered_set<std::uint64_t> deviceIds;
    std::unordered_set<std::uint64_t> liveTracerIds;
    for(const auto& d:world.devices()){
        if(!d.live)continue;deviceIds.insert(d.id);auto [it,inserted]=impl_->deviceMotion.try_emplace(d.id);auto& m=it->second;if(inserted)m.previous=d.pos;
        if(d.kind=="tracer"){
            liveTracerIds.insert(d.id);auto& tail=impl_->tracerTails[d.id];tail.team=d.team;tail.active=true;tail.lastSeen=now;
            if(glm::dot(d.vel,d.vel)>1e-8)tail.direction=glm::normalize(d.vel);
            tail.points=d.trailPoints;
            constexpr std::size_t maxRibbonPoints=160;
            if(tail.points.size()>maxRibbonPoints)tail.points.erase(tail.points.begin(),tail.points.end()-maxRibbonPoints);
            if(tail.points.empty())tail.points.emplace_back(d.pos,now);
        }
        const auto backOut=[](double k,double s){k-=1;return 1+(s+1)*k*k*k+s*k*k;};
        if(d.kind=="torpedo"){
            if(d.state=="fly"){m.roll+=dt*7;m.fin=0;m.glow=.45+.25*std::sin(d.age*20);}
            else if(d.state=="unfold"){const double k=std::clamp(d.age/std::max(.001,d.timer),0.,1.);m.fin=backOut(std::clamp(k/.65,0.,1.),2.4);m.open=backOut(std::clamp((k-.45)/.45,0.,1.),2);m.roll+=dt*2*(1-k);m.prop+=dt*30*k;m.glow=1.2+2.6*k+.8*std::sin(d.age*40)*k;}
            else {m.fin=m.open=1;m.prop+=dt*(30+d.speed*6);m.roll+=dt*1.2;m.glow=3.2+.8*std::sin(d.age*30);}
        }
        if(d.kind=="waddle"){
            const double moved=glm::distance(glm::dvec2(d.pos.x,d.pos.z),glm::dvec2(m.previous.x,m.previous.z));
            m.moveSpeed=dt>1e-6?moved/dt:0;
            const double k=std::clamp(moved/(std::max(.01,d.speed)*std::max(0.,dt)+1e-6),0.,1.2);m.walk+=dt*5.5*k;
        }
        if(d.kind=="boomerang"){
            if(d.state=="fly")m.spinSpeed=30-6*std::clamp(d.age/GetEquipment(EquipmentCategory::Sub,"boomerang").number("outTime"),0.,1.);
            else {const double target=d.state=="hover"?34:d.state=="return"?26:d.state=="orbit"?30:44;m.spinSpeed+=(target-m.spinSpeed)*(1-std::exp(-(d.state=="hover"?8:4)*dt));}
            m.spin=std::fmod(m.spin+dt*m.spinSpeed,6.283185307179586);const bool moving=d.state=="fly"||d.state=="return";const double bx=moving?-.08:.07*std::sin(d.age*7.3),bz=moving?(d.state=="return"?-.35:.28):d.state=="orbit"?.3:.07*std::cos(d.age*5.9);m.tiltX+=(bx-m.tiltX)*(1-std::exp(-10*dt));m.tiltZ+=(bz-m.tiltZ)*(1-std::exp(-10*dt));
        }
        m.previous=d.pos;
    }
    for(auto& [id,tail]:impl_->tracerTails)if(!liveTracerIds.count(id))tail.active=false;
    const double trailLife=GetEquipment(EquipmentCategory::Sub,"tracer").number("trailLife",1.0);
    for(auto it=impl_->tracerTails.begin();it!=impl_->tracerTails.end();){
        auto& points=it->second.points;
        while(points.size()>1&&now-points[1].second>trailLife)points.erase(points.begin());
        if(points.empty()||(!it->second.active&&now-points.back().second>trailLife))it=impl_->tracerTails.erase(it);else ++it;
    }
    constexpr std::size_t maxTracerTails=128;
    while(impl_->tracerTails.size()>maxTracerTails){
        auto oldest=std::min_element(impl_->tracerTails.begin(),impl_->tracerTails.end(),[](const auto& a,const auto& b){return a.second.lastSeen<b.second.lastSeen;});
        if(oldest==impl_->tracerTails.end())break;impl_->tracerTails.erase(oldest);
    }
    for(auto it=impl_->deviceMotion.begin();it!=impl_->deviceMotion.end();)if(!deviceIds.count(it->first))it=impl_->deviceMotion.erase(it);else ++it;
    std::unordered_set<const PlayerSimulation*> alive;
    for(const auto* a:world.actors()){const auto s=a->specialPose();if(!a->state.alive||!s.active||s.kind!="crab")continue;alive.insert(a);auto& m=impl_->motion[a];m.speed=std::hypot(a->state.vel.x,a->state.vel.z);if(s.roll)m.roll+=m.speed/.95*dt;else{m.leg+=dt*(1+m.speed*2.2);m.gun+=dt*(s.firing>0?40:2);}}
    for(auto it=impl_->motion.begin();it!=impl_->motion.end();)if(!alive.count(it->first))it=impl_->motion.erase(it);else ++it;
}
std::size_t DeviceRenderer::Count()const{return impl_->devices.size();}
std::size_t DeviceRenderer::TracerTailCount()const{return impl_->tracerTails.size();}
std::size_t DeviceRenderer::TracerRibbonSegmentCount()const{std::size_t count=0;for(const auto& [id,tail]:impl_->tracerTails){(void)id;if(tail.points.size()>1)count+=tail.points.size()-1;}return count;}
int DeviceRenderer::DrawModel(const std::string& kind,int team,const glm::dmat4& root,double age,const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& lights,bool depthOnly,double opacity,double phase){return impl_->draw(kind,team,root,age,view,projection,eye,lights,depthOnly,nullptr,nullptr,nullptr,opacity,phase);}
int DeviceRenderer::Draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& lights,const std::vector<DeviceActorPose>& actors,const CharacterShadowState* shadow){
    const bool depthOnly=shadow&&shadow->depthOnly;int draws=0;
    for(const auto& d:impl_->devices){if(d.kind=="sonar")continue;
        if(d.kind=="scan"&&d.state=="cloud"){const auto& s=GetEquipment(EquipmentCategory::Sub,"scan");const double grow=std::clamp(d.age/.25,0.,1.),life=s.number("cloudTime"),fade=std::clamp((d.age-life*.5)/(life*.5),0.,1.);const double scale=.2+(s.number("radius")-.2)*(1-std::pow(1-grow,3));draws+=DrawModel("scanCloud",d.team,glm::translate(glm::dmat4(1),d.pos)*glm::scale(glm::dmat4(1),glm::dvec3(scale/.2)),d.age,view,projection,eye,lights,depthOnly,1-fade);continue;}
        if(d.kind=="mist"&&d.state=="mist"){const auto& s=GetEquipment(EquipmentCategory::Sub,"mist");const double grow=std::clamp(d.age/.4,0.,1.),fade=std::clamp((d.age-(s.number("mistTime")-.8))/.8,0.,1.);const double scale=.3+(s.number("radius")*.72-.3)*(1-std::pow(1-grow,3));draws+=DrawModel("mistCloud",d.team,glm::translate(glm::dmat4(1),d.pos)*glm::scale(glm::dmat4(1),glm::dvec3(scale/.3)),d.age,view,projection,eye,lights,depthOnly,1-fade);continue;}
        auto root=glm::translate(glm::dmat4(1),d.pos);
        if(d.kind=="brolly")root=rotate(root,0,std::atan2(d.N.x,d.N.z),0);
        else if(d.kind=="tracer"||d.kind=="torpedo"){root*=along(d.kind=="torpedo"&&d.state=="unfold"?d.N:d.vel);const auto m=impl_->deviceMotion.find(d.id);root=rotate(root,0,0,d.kind=="tracer"?d.age*26:m!=impl_->deviceMotion.end()?m->second.roll:0);}
        else if(d.state=="fly")root=rotate(root,d.age*8,0,d.age*5);
        else if(d.kind=="sprinkler"||d.kind=="sticky")root*=glm::toMat4(glm::rotation(glm::dvec3(0,1,0),glm::normalize(d.normal)));
        else root=rotate(root,0,d.heading,0);
        if(d.state=="stuck"&&d.fuse>0){const double k=std::clamp(d.timer/d.fuse,0.,1.);root=glm::scale(root,glm::dvec3(1+k*.3+std::sin(d.timer*40)*.025*k));}
        if(d.kind=="mine"&&d.fuse>=0)root=glm::scale(root,glm::dvec3(1+std::sin(d.age*50)*.08));
        draws+=impl_->draw(d.kind,d.team,root,d.age,view,projection,eye,lights,depthOnly,&d);
        if(!depthOnly&&d.kind=="waddle"&&(d.state=="sense"||d.state=="wake"||d.state=="walk")){const double radius=d.senseRadius>0?d.senseRadius:GetEquipment(EquipmentCategory::Sub,"waddle").number("senseRadius");draws+=DrawModel("waddleRing",d.team,glm::translate(glm::dmat4(1),d.pos+glm::dvec3(0,.035,0))*glm::scale(glm::dmat4(1),glm::dvec3(radius)),d.age,view,projection,eye,lights,false,.75,d.locked?1:0);}
        if(!depthOnly&&d.kind=="torpedo"&&d.target&&d.target->state.alive&&d.state!="fly"){const double k=std::clamp(d.age/.22,0.,1.),scale=(1.9-.9*(1-std::pow(1-k,3)))*(1+.04*std::sin(d.age*9));draws+=DrawModel("torpedoRing",d.team,glm::translate(glm::dmat4(1),d.target->visualPos()+glm::dvec3(0,.06,0))*glm::scale(glm::dmat4(1),glm::dvec3(scale)),d.age,view,projection,eye,lights,false,1);}
        if(d.kind=="curtain"&&d.state=="curtain"){
            const double maxHp=d.hpMax>0?d.hpMax:GetEquipment(EquipmentCategory::Sub,"curtain").number("hp",1.0);
            draws+=DrawModel("curtainSheet",d.team,root,d.age,view,projection,eye,lights,depthOnly,1,std::clamp(d.hp/std::max(1.,maxHp),0.,1.));
        }
    }
    if(!depthOnly){const double life=GetEquipment(EquipmentCategory::Sub,"tracer").number("trailLife",1.0);for(const auto& [id,tail]:impl_->tracerTails){(void)id;if(tail.points.size()>1&&glm::distance(tail.points.back().first,eye)<65)draws+=impl_->drawRibbon(tail,impl_->clock,life,view,projection,eye);}}
    for(const auto& item:actors){const auto& a=*item.actor;const auto s=a.specialPose();if(!s.active||!a.state.alive)continue;const auto& f=*item.frame;
        auto root=glm::translate(glm::dmat4(1),a.visualPos());
        if(s.kind=="kraken"){
            const double speed=std::hypot(a.state.vel.x,a.state.vel.z),sq=a.state.grounded?1+std::sin(s.elapsed*14)*.04*std::min(1.,speed/4):std::clamp(1+a.state.vel.y*.025,.8,1.25);
            root=glm::scale(rotate(root,0,a.state.yaw,0),glm::dvec3(1/std::sqrt(sq),sq,1/std::sqrt(sq)));draws+=impl_->draw("kraken",a.team(),root,s.elapsed,view,projection,eye,lights,depthOnly);
        }else if(s.kind=="crab"){
            root=rotate(root,0,s.faceYaw.value_or(a.state.yaw),0);const auto found=impl_->motion.find(&a);draws+=impl_->draw("crab",a.team(),root,s.elapsed,view,projection,eye,lights,depthOnly,nullptr,&s,found==impl_->motion.end()?nullptr:&found->second);
        }else if(s.kind=="jetpack"){
            root=item.tank;
            draws+=impl_->draw("jetpack",a.team(),root,s.elapsed,view,projection,eye,lights,depthOnly);
        }
    }
    return draws;
}
int DeviceRenderer::DrawShadow(const glm::dmat4& lightViewProjection,const std::vector<DeviceActorPose>& actors){CharacterShadowState shadow;shadow.depthOnly=true;return Draw(glm::dmat4(1),lightViewProjection,glm::dvec3(0),CharacterLightState{},actors,&shadow);}
}
