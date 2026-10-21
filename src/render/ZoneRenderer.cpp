#include "ZoneRenderer.hpp"
#include "ZoneControl.hpp"
#include "CharacterShader.hpp"
#include "TeamPalette.hpp"
#include <glad/glad.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace restoration {
namespace {
glm::dvec3 neutralColor(){
    for(const auto& c:SrgbInkColors){
        const double hi=std::max({c.r,c.g,c.b}),lo=std::min({c.r,c.g,c.b}),d=hi-lo;
        if(d<.001||d/hi<.5)continue;
        double h=(hi==c.r?(c.g-c.b)/d:hi==c.g?(c.b-c.r)/d+2:(c.r-c.g)/d+4)*60;
        h=std::fmod(h+360,360.0);
        if(std::abs(h-48)<14)return {1,.93,.78};
    }
    return {1,.8,.3};
}
}
struct ZoneRenderer::Impl {
    struct State{glm::dvec3 color{neutralColor()};double active=0,sweep=0,reach=0,flash=0,visible=0,held=0,warn=0;bool on=false;};
    CharacterShader shader;
    GLuint vao=0,vbo=0,ebo=0;GLsizei count=0;
    std::vector<State> state;
    std::size_t eventCursor=0;
    double previous=-1;
    Impl(const std::filesystem::path& assets,const std::string& stage)
      :shader(CharacterShader::readSource(assets/"marks.vert"),CharacterShader::readSource(assets/"marks.frag"),true){
        std::ifstream file(assets/(stage+".bin"),std::ios::binary);char magic[8]{};std::uint32_t vertices=0,indices=0;
        file.read(magic,8);file.read(reinterpret_cast<char*>(&vertices),4);file.read(reinterpret_cast<char*>(&indices),4);
        if(!file||std::string(magic,8)!="TSZMARK1"||!vertices||vertices>1000000||indices>3000000||indices%3)throw std::runtime_error("Invalid source zone geometry header");
        std::vector<float> data(vertices*7);std::vector<std::uint32_t> elements(indices);
        file.read(reinterpret_cast<char*>(data.data()),data.size()*sizeof(float));file.read(reinterpret_cast<char*>(elements.data()),elements.size()*4);
        if(!file||std::any_of(elements.begin(),elements.end(),[&](auto i){return i>=vertices;})||std::any_of(data.begin(),data.end(),[](auto v){return !std::isfinite(v);}))throw std::runtime_error("Invalid source zone geometry data");
        count=static_cast<GLsizei>(indices);
        glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,data.size()*sizeof(float),data.data(),GL_STATIC_DRAW);
        for(const auto& item:std::array<std::pair<const char*,int>,2>{{{"position",3},{"aInfo",4}}}){
            const int location=shader.attribute(item.first);if(location<0)throw std::runtime_error("Missing source zone attribute");
            glEnableVertexAttribArray(location);glVertexAttribPointer(location,item.second,GL_FLOAT,GL_FALSE,7*sizeof(float),reinterpret_cast<void*>(item.second==3?0:3*sizeof(float)));
        }
        glGenBuffers(1,&ebo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,elements.size()*4,elements.data(),GL_STATIC_DRAW);glBindVertexArray(0);
    }
    ~Impl(){glDeleteBuffers(1,&vbo);glDeleteBuffers(1,&ebo);glDeleteVertexArrays(1,&vao);}
    int draw(const ZoneControl& zones,const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,double mapView,bool finished,double visualTime){
        const auto& zs=zones.zones();if(zs.size()>8)throw std::runtime_error("Source zone uniform capacity exceeded");
        const double now=zones.time(),animationTime=visualTime>=0?visualTime:now,dt=previous<0?0:std::clamp(animationTime-previous,0.,.25);const bool first=previous<0;previous=animationTime;
        if(state.size()!=zs.size()){
            state.resize(zs.size());for(std::size_t i=0;i<zs.size();++i){state[i].reach=zs[i].reach+1.5;state[i].sweep=state[i].reach+2;}
        }
        const auto& events=zones.events();if(eventCursor>events.size())eventCursor=0;
        for(;eventCursor<events.size();++eventCursor){const auto& e=events[eventCursor];if(e.type==ZoneEventType::ZoneOwnerChanged&&e.zone>=0&&e.zone<int(state.size()))state[e.zone].flash=1;}
        std::vector<double> colors(24),status(32),centers(32),floods(32),teams;
        for(const auto& c:InkColors)for(int k=0;k<3;++k)teams.push_back(c[k]);
        for(std::size_t i=0;i<zs.size();++i){const auto& z=zs[i];auto& s=state[i];
            const bool on=std::find(zones.activeIndices().begin(),zones.activeIndices().end(),int(i))!=zones.activeIndices().end();
            if(on&&!s.on&&!first){s.sweep=0;s.flash=.6;}s.on=on;
            const double k=1-std::exp(-dt*9),cz=on&&z.owner>=0?std::clamp((z.share[1-z.owner]-.25)/.15,0.,1.):0;
            s.warn+=(cz-s.warn)*(1-std::exp(-dt*6));s.color+=( (z.owner>=0?InkColors[z.owner]:neutralColor())-s.color)*k;
            s.active+=((on?1.:0.)-s.active)*(1-std::exp(-dt*(on?14:6)));
            if(on&&s.sweep<s.reach+2)s.sweep=std::min(s.reach+2,s.sweep+dt*(s.reach+2)/.7);
            s.flash=std::max(0.,s.flash-dt*1.6);s.held+=((z.owner>=0?1.:0.)-s.held)*k;
            s.visible+=((finished?.55:1.)-s.visible)*(1-std::exp(-dt*4));
            for(int c=0;c<3;++c)colors[i*3+c]=s.color[c];
            status[i*4]=s.active;status[i*4+1]=s.sweep;status[i*4+2]=s.flash;status[i*4+3]=s.visible;
            centers[i*4]=z.center.x;centers[i*4+1]=z.center.z;centers[i*4+2]=s.reach;centers[i*4+3]=s.held;
            floods[i*4+2]=s.warn;floods[i*4+3]=z.owner>=0?1-z.owner:0;
            if(z.flood&&z.flood->team>=0&&now-z.flood->start<z.flood->duration){const auto& f=*z.flood;const double x=std::max(0.,(now-f.start)/f.duration);floods[i*4]=f.reach*(f.easeOut?1-(1-x)*(1-x):x);floods[i*4+1]=x<.85?1:(1-x)/.15;}
        }
        GLint vaoBefore=0,program=0,srcRgb=0,dstRgb=0,srcA=0,dstA=0,eqRgb=0,eqA=0;GLboolean depthMask=GL_TRUE;
        const auto blend=glIsEnabled(GL_BLEND),cull=glIsEnabled(GL_CULL_FACE),offset=glIsEnabled(GL_POLYGON_OFFSET_FILL);
        GLfloat factor=0,units=0;glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vaoBefore);glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
        glGetIntegerv(GL_BLEND_SRC_RGB,&srcRgb);glGetIntegerv(GL_BLEND_DST_RGB,&dstRgb);glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcA);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstA);
        glGetIntegerv(GL_BLEND_EQUATION_RGB,&eqRgb);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&eqA);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&factor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&units);
        shader.use();shader.matrices(glm::dmat4(1),view,projection,eye);shader.uniform("uTime",animationTime);shader.uniform("uWide",std::clamp(mapView,0.,1.));shader.uniform("uFarK",1.);
        shader.uniform("uCol",colors);shader.uniform("uSt",status);shader.uniform("uCen",centers);shader.uniform("uFl",floods);shader.uniform("uTeamC",teams);
        glEnable(GL_BLEND);glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glDisable(GL_CULL_FACE);glDepthMask(GL_FALSE);glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1,-2);
        glBindVertexArray(vao);glDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_INT,nullptr);
        glBindVertexArray(vaoBefore);glUseProgram(program);glDepthMask(depthMask);glBlendEquationSeparate(eqRgb,eqA);glBlendFuncSeparate(srcRgb,dstRgb,srcA,dstA);glPolygonOffset(factor,units);
        if(!blend)glDisable(GL_BLEND);if(cull)glEnable(GL_CULL_FACE);if(!offset)glDisable(GL_POLYGON_OFFSET_FILL);
        return 1;
    }
};
ZoneRenderer::ZoneRenderer(const std::filesystem::path& assets,const std::string& stage):impl_(std::make_unique<Impl>(assets,stage)){}
ZoneRenderer::~ZoneRenderer()=default;
int ZoneRenderer::Draw(const ZoneControl& zones,const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,double mapView,bool finished,double visualTime){return impl_->draw(zones,view,projection,eye,mapView,finished,visualTime);}
}
