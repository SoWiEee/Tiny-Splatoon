#define GLM_ENABLE_EXPERIMENTAL
#include "ProjectileRenderer.hpp"
#include "CharacterShader.hpp"
#include "CharacterRenderData.hpp"
#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"
#include "ArsenalData.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <cstddef>
#include <map>

namespace restoration {
namespace {
glm::dvec3 teamColor(int team) {
    return team==1?glm::dvec3(0.028426039499072558,0.10461648408208657,1):glm::dvec3(1,0.2541520943200296,0.0069954101845983935);
}
}
struct ProjectileRenderer::Impl {
    struct Part {
        std::unique_ptr<CharacterShader> shader;
        GLuint vao=0,vertices=0,indices=0;GLsizei count=0;
        ~Part(){if(vao)glDeleteVertexArrays(1,&vao);const GLuint buffers[]={vertices,indices};glDeleteBuffers(2,buffers);}
    };
    struct Ribbon {glm::dvec3 pos,dir,color;double age=0,life=.34,length=1,width=.08,charge=0,seed=0;bool sight=false;};
    CharacterShader blobShader,ribbonShader;
    GLuint vao=0,vertex=0,index=0,instance=0,ribbonVao=0,ribbonVertex=0,dfg=0;
    GLsizei indexCount=0;
    std::vector<ProjectileBlob> blobs;
    std::vector<Ribbon> beams,sights;
    std::vector<ShooterProjectile> custom;
    std::map<std::string,std::unique_ptr<Part>> parts;
    std::filesystem::path assets;
    explicit Impl(const std::filesystem::path& assets)
      :blobShader(CharacterShader::readSource(assets/"blob.vert"),CharacterShader::readSource(assets/"blob.frag"),true),
       ribbonShader(CharacterShader::readSource(assets/"ribbon.vert"),CharacterShader::readSource(assets/"ribbon.frag"),true),assets(assets) {
        std::ifstream file(assets/"blob.bin",std::ios::binary);
        char magic[8]{};std::uint32_t vertices=0,indices=0;
        file.read(magic,8);file.read(reinterpret_cast<char*>(&vertices),4);file.read(reinterpret_cast<char*>(&indices),4);
        if(!file||std::string(magic,8)!="TSBLOB01"||vertices>10000||indices>100000||!vertices||indices%3)
            throw std::runtime_error("Invalid source projectile geometry header");
        std::vector<float> data(vertices*6);std::vector<std::uint32_t> elements(indices);
        file.read(reinterpret_cast<char*>(data.data()),data.size()*sizeof(float));
        file.read(reinterpret_cast<char*>(elements.data()),elements.size()*sizeof(std::uint32_t));
        if(!file||std::any_of(elements.begin(),elements.end(),[&](auto i){return i>=vertices;}))
            throw std::runtime_error("Truncated or invalid source projectile geometry");
        indexCount=static_cast<GLsizei>(indices);
        glGenVertexArrays(1,&vao);glBindVertexArray(vao);
        glGenBuffers(1,&vertex);glBindBuffer(GL_ARRAY_BUFFER,vertex);glBufferData(GL_ARRAY_BUFFER,data.size()*sizeof(float),data.data(),GL_STATIC_DRAW);
        for(const auto& entry:std::array<std::pair<const char*,std::size_t>,2>{{{"position",0},{"normal",3*sizeof(float)}}}) {
            const auto loc=blobShader.attribute(entry.first);
            if(loc>=0){glEnableVertexAttribArray(loc);glVertexAttribPointer(loc,3,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<void*>(entry.second));}
        }
        glGenBuffers(1,&index);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,index);glBufferData(GL_ELEMENT_ARRAY_BUFFER,elements.size()*4,elements.data(),GL_STATIC_DRAW);
        glGenBuffers(1,&instance);glBindBuffer(GL_ARRAY_BUFFER,instance);
        const auto attribute=[&](const char* name,int count,std::size_t offset){
            const auto loc=blobShader.attribute(name);if(loc<0)return;
            glEnableVertexAttribArray(loc);glVertexAttribPointer(loc,count,GL_FLOAT,GL_FALSE,sizeof(ProjectileBlob),reinterpret_cast<void*>(offset));glVertexAttribDivisor(loc,1);
        };
        const int matrix=blobShader.attribute("instanceMatrix");
        if(matrix<0)throw std::runtime_error("Source projectile shader lost instancing");
        for(int i=0;i<4;i++){glEnableVertexAttribArray(matrix+i);glVertexAttribPointer(matrix+i,4,GL_FLOAT,GL_FALSE,sizeof(ProjectileBlob),reinterpret_cast<void*>(offsetof(ProjectileBlob,matrix)+i*sizeof(glm::vec4)));glVertexAttribDivisor(matrix+i,1);}
        attribute("aShape",4,offsetof(ProjectileBlob,shape));attribute("instanceColor",3,offsetof(ProjectileBlob,color));
        blobShader.use();
        std::ifstream uniforms(assets/"blob.uniforms");std::string name;std::size_t count;
        if(!uniforms)throw std::runtime_error("Missing source projectile material defaults");
        while(uniforms>>name>>count){
            if(count>64)throw std::runtime_error("Invalid source projectile uniform shape");
            std::vector<double> values(count);for(auto& value:values)if(!(uniforms>>value))throw std::runtime_error("Truncated source projectile uniform");
            blobShader.uniform(name,values);
        }
        std::vector<glm::vec3> strip;
        for(int i=0;i<16;i++) {
            const float a=i/16.f,b=(i+1)/16.f;
            for(const auto p:std::array<glm::vec3,6>{{{-1,0,a},{-1,0,b},{1,0,a},{1,0,a},{-1,0,b},{1,0,b}}})strip.push_back(p);
        }
        glGenVertexArrays(1,&ribbonVao);glBindVertexArray(ribbonVao);
        glGenBuffers(1,&ribbonVertex);glBindBuffer(GL_ARRAY_BUFFER,ribbonVertex);glBufferData(GL_ARRAY_BUFFER,strip.size()*sizeof(glm::vec3),strip.data(),GL_STATIC_DRAW);
        const auto position=ribbonShader.attribute("position");glEnableVertexAttribArray(position);glVertexAttribPointer(position,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),nullptr);
        glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);
        glGenTextures(1,&dfg);glBindTexture(GL_TEXTURE_2D,dfg);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RG16F,16,16,0,GL_RG,GL_HALF_FLOAT,CharacterDfg.data());
    }
    ~Impl(){
        const GLuint buffers[]={vertex,index,instance,ribbonVertex};glDeleteBuffers(4,buffers);
        const GLuint arrays[]={vao,ribbonVao};glDeleteVertexArrays(2,arrays);
        if(dfg)glDeleteTextures(1,&dfg);
    }
    Part& part(const std::string& name) {
        auto found=parts.find(name);if(found!=parts.end())return *found->second;
        auto p=std::make_unique<Part>();
        p->shader=std::make_unique<CharacterShader>(CharacterShader::readSource(assets/(name+".vert")),CharacterShader::readSource(assets/(name+".frag")),true);
        std::ifstream file(assets/(name+".bin"),std::ios::binary);char magic[8];std::uint32_t nv=0,ni=0;
        file.read(magic,8);file.read(reinterpret_cast<char*>(&nv),4);file.read(reinterpret_cast<char*>(&ni),4);
        if(!file||std::string(magic,8)!="TSGEOM01"||!nv||nv>1000000||ni>6000000||ni%3)throw std::runtime_error("Invalid source projectile part: "+name);
        std::vector<float> vertices(nv*12);std::vector<std::uint32_t> indices(ni);
        file.read(reinterpret_cast<char*>(vertices.data()),vertices.size()*4);file.read(reinterpret_cast<char*>(indices.data()),indices.size()*4);
        if(!file||std::any_of(indices.begin(),indices.end(),[&](auto i){return i>=nv;}))throw std::runtime_error("Invalid source projectile part indices: "+name);
        glGenVertexArrays(1,&p->vao);glBindVertexArray(p->vao);
        glGenBuffers(1,&p->vertices);glBindBuffer(GL_ARRAY_BUFFER,p->vertices);glBufferData(GL_ARRAY_BUFFER,vertices.size()*4,vertices.data(),GL_STATIC_DRAW);
        const char* names[]={"position","normal","uv","color","aK"};const int sizes[]={3,3,2,3,1};int offset=0;
        for(int i=0;i<5;i++){const auto loc=p->shader->attribute(names[i]);if(loc>=0){glEnableVertexAttribArray(loc);glVertexAttribPointer(loc,sizes[i],GL_FLOAT,GL_FALSE,48,reinterpret_cast<void*>(offset*sizeof(float)));}offset+=sizes[i];}
        glGenBuffers(1,&p->indices);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,p->indices);glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*4,indices.data(),GL_STATIC_DRAW);p->count=static_cast<GLsizei>(ni);
        p->shader->use();std::ifstream defaults(assets/(name+".uniforms"));std::string key;std::size_t count;
        if(!defaults)throw std::runtime_error("Missing source projectile part defaults: "+name);
        while(defaults>>key>>count){if(!count||count>64)throw std::runtime_error("Invalid source projectile part uniform");std::vector<double> values(count);for(auto& value:values)if(!(defaults>>value))throw std::runtime_error("Truncated source projectile part uniform");p->shader->uniform(key,values);}
        return *parts.emplace(name,std::move(p)).first->second;
    }
};
ProjectileRenderer::ProjectileRenderer(const std::filesystem::path& assets):impl_(std::make_unique<Impl>(assets)){}
ProjectileRenderer::~ProjectileRenderer()=default;
const std::vector<ProjectileBlob>& ProjectileRenderer::blobs()const{return impl_->blobs;}
std::size_t ProjectileRenderer::ribbons()const{return impl_->beams.size()+impl_->sights.size();}
void ProjectileRenderer::Update(double dt,const CombatWorld& world,const std::vector<WorldVisualEvent>& events) {
    impl_->blobs=BuildProjectileBlobs(world.projectiles());
    impl_->custom.clear();
    for(const auto& p:world.projectiles())if(p.owner&&p.delay<=0&&(p.kind=="arrow"||p.kind=="fist"||p.kind=="bladewave"))impl_->custom.push_back(p);
    for(auto& beam:impl_->beams)beam.age+=dt;
    auto& beams=impl_->beams;
    beams.erase(std::remove_if(beams.begin(),beams.end(),[](const auto& b){return b.age>=b.life;}),beams.end());
    for(const auto& event:events)if(event.kind=="beam"&&beams.size()<64)
        beams.push_back({event.pos,event.normal,teamColor(event.team),0,.3+.1*event.charge,event.radius,event.width,event.charge,event.seed,false});
    impl_->sights.clear();
    for(const auto* actor:world.actors()) {
        if(!actor||!actor->state.alive||actor->weapon().kind!="charger")continue;
        const auto pose=actor->combatPose();if(!pose.charging)continue;
        const auto muzzle=actor->muzzle();
        auto direction=actor->input.aimPoint-muzzle;
        const auto fallback=glm::dvec3(std::sin(actor->input.aimYaw)*std::cos(actor->input.aimPitch),std::sin(actor->input.aimPitch),std::cos(actor->input.aimYaw)*std::cos(actor->input.aimPitch));
        direction=actor->input.hasAimPoint&&glm::length(direction)>=2&&glm::dot(direction,fallback)>=0?glm::normalize(direction):fallback;
        const auto& weapon=actor->weapon();const double range=weapon.number("rangeMin")+(weapon.number("rangeMax")-weapon.number("rangeMin"))*pose.charge;
        const auto hit=world.geometry().raycast(muzzle,direction,range);
        impl_->sights.push_back({muzzle,direction,teamColor(actor->team()),world.time(),1,hit.hit?hit.dist:range,.014+pose.charge*.02,pose.charge,0,true});
    }
}
int ProjectileRenderer::Draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& lights) {
    if(impl_->blobs.empty()&&ribbons()==0&&impl_->custom.empty())return 0;
    GLint program,vao,buffer,srcRgb,dstRgb,srcAlpha,dstAlpha,activeTexture,oldDfg;GLboolean depth;
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);
    glGetIntegerv(GL_BLEND_SRC_RGB,&srcRgb);glGetIntegerv(GL_BLEND_DST_RGB,&dstRgb);glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);glActiveTexture(GL_TEXTURE6);glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldDfg);glBindTexture(GL_TEXTURE_2D,impl_->dfg);
    const bool blend=glIsEnabled(GL_BLEND),cull=glIsEnabled(GL_CULL_FACE);
    int draws=0;
    if(!impl_->blobs.empty()) {
        glDisable(GL_BLEND);glEnable(GL_CULL_FACE);glDepthMask(GL_TRUE);
        auto& shader=impl_->blobShader;shader.use();shader.matrices(glm::dmat4(1),view,projection,eye);shader.lights(lights,view);
        shader.uniform("dfgLUT",6);
        glBindVertexArray(impl_->vao);glBindBuffer(GL_ARRAY_BUFFER,impl_->instance);
        glBufferData(GL_ARRAY_BUFFER,impl_->blobs.size()*sizeof(ProjectileBlob),impl_->blobs.data(),GL_STREAM_DRAW);
        glDrawElementsInstanced(GL_TRIANGLES,impl_->indexCount,GL_UNSIGNED_INT,nullptr,static_cast<GLsizei>(impl_->blobs.size()));++draws;
    }
    const auto drawPart=[&](const char* name,const glm::dmat4& model,glm::dvec3 color,double time=0) {
        const std::string key(name);const bool additive=key=="arrowGlow"||key=="arrowStreak",alpha=key=="wave";
        if(additive||alpha){glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,additive?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);}
        else{glDisable(GL_BLEND);glDepthMask(GL_TRUE);glEnable(GL_CULL_FACE);}
        auto& part=impl_->part(key);auto& shader=*part.shader;shader.use();shader.matrices(model,view,projection,eye);shader.lights(lights,view);shader.uniform("dfgLUT",6);
        shader.uniform("diffuse",{color.x,color.y,color.z});shader.uniform("uColor",{color.x,color.y,color.z});shader.uniform("uTint",{color.x,color.y,color.z});shader.uniform("uT",time);shader.uniform("uFade",1);
        glBindVertexArray(part.vao);glDrawElements(GL_TRIANGLES,part.count,GL_UNSIGNED_INT,nullptr);++draws;
    };
    for(const auto& p:impl_->custom) {
        const auto color=teamColor(p.owner->team());
        auto direction=p.kind=="arrow"?p.flightDir:p.vel;
        direction=glm::length(direction)>1e-8?glm::normalize(direction):glm::dvec3(0,0,1);
        const auto rotation=glm::mat4_cast(glm::rotation(glm::dvec3(0,0,1),direction));
        if(p.kind=="arrow") {
            const auto model=glm::translate(glm::dmat4(1),p.pos)*rotation;
            drawPart("arrowInk",model,color);drawPart("arrowFletch",model,glm::dvec3(1));
            glm::dvec3 at;double radius,brightness;
            if(p.state=="lodged") {
                const double k=std::clamp(p.age/std::max(.001,p.fuse),0.0,1.0),beat=.5+.5*std::sin(p.age*(14+30*k));
                at=p.hitPoint+p.hitNormal*.05;radius=.07+.07*k+.035*beat;brightness=1.2+2.2*k+1.6*beat;
            }else{at=p.pos-direction*.05;radius=p.tier==2?.085:p.tier?.07:.05;brightness=p.tier==2?2.4:p.tier?1.6:.9;}
            drawPart("arrowGlow",glm::translate(glm::dmat4(1),at)*rotation*glm::scale(glm::dmat4(1),glm::dvec3(radius)),color*brightness);
            if(p.state!="lodged"){
                const double length=std::min(p.distance,glm::length(p.vel)*.06+.4),r=p.tier==2?.045:p.tier?.036:.026;
                drawPart("arrowStreak",glm::translate(glm::dmat4(1),p.pos-direction*.25)*rotation*glm::scale(glm::dmat4(1),glm::dvec3(r,r,length)),color*(p.tier==2?1.5:1));
            }
        }else {
            auto x=glm::cross(glm::dvec3(0,1,0),direction);x=glm::length(x)>1e-6?glm::normalize(x):glm::dvec3(1,0,0);const auto y=glm::normalize(glm::cross(direction,x));
            const auto basis=glm::dmat4(glm::dmat3(x,y,direction));
            if(p.kind=="fist"){
                const double grow=std::min(1.0,.45+p.age/.05*.55),wob=std::sin(p.age*46+p.seed*6)*.1,k=1.25*grow;
                drawPart("fist",glm::translate(glm::dmat4(1),p.pos)*basis*glm::scale(glm::dmat4(1),glm::dvec3(k*(1+wob*.6),k*(1+wob*.6),k*(1-wob))),color);
            }else{
                const auto& w=GetEquipment(EquipmentCategory::Main,"blade");const double grow=std::min(1.0,.55+p.age*6);
                drawPart("wave",glm::translate(glm::dmat4(1),p.pos)*basis*glm::scale(glm::dmat4(1),glm::dvec3(w.number("waveWidth")*1.45*grow,w.number("waveHeight")*.5*grow,1+std::min(1.0,p.distance/4)*.9)),color,p.age);
            }
        }
    }
    if(ribbons()) {
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDisable(GL_CULL_FACE);glDepthMask(GL_FALSE);
        auto& shader=impl_->ribbonShader;shader.use();glBindVertexArray(impl_->ribbonVao);
        const auto draw=[&](const auto& ribbon){
            auto direction=glm::length(ribbon.dir)>1e-8?glm::normalize(ribbon.dir):glm::dvec3(0,0,1);
            const auto model=glm::translate(glm::dmat4(1),ribbon.pos)*glm::mat4_cast(glm::rotation(glm::dvec3(0,0,1),direction))*glm::scale(glm::dmat4(1),glm::dvec3(1,1,ribbon.length));
            shader.matrices(model,view,projection,eye);shader.uniform("uColor",{ribbon.color.x,ribbon.color.y,ribbon.color.z});
            shader.uniform("uT",ribbon.age);shader.uniform("uLife",ribbon.life);shader.uniform("uLen",ribbon.length);shader.uniform("uCharge",ribbon.charge);
            shader.uniform("uSeed",ribbon.seed);shader.uniform("uMode",ribbon.sight?1:0);shader.uniform("uWidth",ribbon.width);
            glDrawArrays(GL_TRIANGLES,0,96);++draws;
        };
        for(const auto& beam:impl_->beams)draw(beam);for(const auto& sight:impl_->sights)draw(sight);
    }
    glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,buffer);glDepthMask(depth);glBlendFuncSeparate(srcRgb,dstRgb,srcAlpha,dstAlpha);
    if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,oldDfg);glActiveTexture(activeTexture);
    return draws;
}
}
