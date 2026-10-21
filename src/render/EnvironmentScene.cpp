#include "EnvironmentScene.hpp"
#include "CharacterRenderData.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <cstring>
#include <zlib.h>
namespace restoration {
namespace {
struct Reader {
    std::vector<unsigned char> bytes;std::size_t at=0;
    void need(std::size_t n){if(n>bytes.size()-at)throw std::runtime_error("Truncated source environment mesh");}
    unsigned u32(){need(4);unsigned v=0;std::memcpy(&v,bytes.data()+at,4);at+=4;return v;}
    unsigned count(unsigned max){const auto n=u32();if(n>max)throw std::runtime_error("Oversized source environment mesh");return n;}
    float f(){const auto u=u32();float f;std::memcpy(&f,&u,4);if(!std::isfinite(f))throw std::runtime_error("Nonfinite source environment vertex");return f;}
    std::string str(){const auto n=count(1024);need(n);std::string s(reinterpret_cast<const char*>(bytes.data()+at),n);at+=n;return s;}
};
glm::dmat4 pose(glm::dvec3 p,double x,double y,double z,double scale=1){
    auto m=glm::translate(glm::dmat4(1),p);m=glm::rotate(m,y,glm::dvec3(0,1,0));m=glm::rotate(m,x,glm::dvec3(1,0,0));m=glm::rotate(m,z,glm::dvec3(0,0,1));return glm::scale(m,glm::dvec3(scale));
}
}
struct EnvironmentScene::Impl {
    struct Instance {glm::dmat4 matrix{1};glm::dvec3 color{1};float phase=0;std::string kind="static";std::vector<double> p;};
    struct Mesh {unsigned vao=0,index=0,instanceBuffer=0;std::vector<unsigned> buffers;std::string shader,name;int count=0;bool transparent=false,depthWrite=true,depthTest=true,casts=false;int side=0,blending=1;std::vector<Instance> instances;
        ~Mesh(){if(vao)glDeleteVertexArrays(1,&vao);if(index)glDeleteBuffers(1,&index);if(!buffers.empty())glDeleteBuffers(static_cast<GLsizei>(buffers.size()),buffers.data());}};
    std::vector<std::unique_ptr<Mesh>> meshes;
    std::map<std::string,std::unique_ptr<CharacterShader>> shaders;
    std::map<std::string,std::map<std::string,std::vector<double>>> defaults;
    unsigned dfg=0;
    ~Impl(){if(dfg)glDeleteTextures(1,&dfg);}
};
EnvironmentScene::EnvironmentScene():impl_(std::make_unique<Impl>()){}
EnvironmentScene::~EnvironmentScene()=default;
void EnvironmentScene::Load(const std::filesystem::path& assets,const std::string& stage){
    impl_->meshes.clear();impl_->shaders.clear();impl_->defaults.clear();
    std::ifstream f(assets/("scene-"+stage+".bin.z"),std::ios::binary);if(!f)throw std::runtime_error("Missing source environment scene: "+stage);
    std::vector<unsigned char> packed((std::istreambuf_iterator<char>(f)),{});
    if(packed.size()<12||std::string(packed.begin(),packed.begin()+8)!="TSENVZ01")throw std::runtime_error("Invalid environment scene header");
    unsigned n=0;std::memcpy(&n,packed.data()+8,4);if(n<12||n>512u*1024u*1024u)throw std::runtime_error("Invalid environment scene size");
    Reader r;r.bytes.resize(n);uLongf size=n;if(uncompress(r.bytes.data(),&size,packed.data()+12,static_cast<uLong>(packed.size()-12))!=Z_OK||size!=n)throw std::runtime_error("Invalid environment scene compression");
    r.need(8);if(std::string(r.bytes.begin(),r.bytes.begin()+8)!="TSENVG01")throw std::runtime_error("Invalid environment geometry magic");r.at=8;
    const auto meshCount=r.count(256);
    for(unsigned m=0;m<meshCount;m++){
        auto mesh=std::make_unique<Impl::Mesh>();mesh->name=r.str();mesh->shader=r.str();
        if(mesh->shader.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid environment material key");
        if(!impl_->shaders.count(mesh->shader)){
            auto shader=std::make_unique<CharacterShader>(CharacterShader::readSource(assets/(mesh->shader+".vert")),CharacterShader::readSource(assets/(mesh->shader+".frag")),true,true,false);
            std::ifstream u(assets/(mesh->shader+".uniforms"));std::string key;std::size_t count;
            if(!u)throw std::runtime_error("Missing environment material defaults");
            while(u>>key>>count){if(!count||count>4096)throw std::runtime_error("Invalid environment uniform size");std::vector<double> v(count);for(auto& x:v)if(!(u>>x)||!std::isfinite(x))throw std::runtime_error("Invalid environment uniform");impl_->defaults[mesh->shader][key]=std::move(v);}
            impl_->shaders.emplace(mesh->shader,std::move(shader));
        }
        auto& shader=*impl_->shaders.at(mesh->shader);
        glGenVertexArrays(1,&mesh->vao);glBindVertexArray(mesh->vao);
        const auto attributes=r.count(32);unsigned vertexCount=0;
        for(unsigned a=0;a<attributes;a++){
            const auto name=r.str();const auto components=r.count(4),count=r.count(10000000);if(!components)throw std::runtime_error("Empty environment attribute");
            if(name=="position")vertexCount=count;
            std::vector<float> data(std::size_t(components)*count);for(auto& x:data)x=r.f();const int at=shader.attribute(name);if(at<0)continue;
            unsigned buffer=0;glGenBuffers(1,&buffer);mesh->buffers.push_back(buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,data.size()*4,data.data(),GL_STATIC_DRAW);glEnableVertexAttribArray(at);glVertexAttribPointer(at,components,GL_FLOAT,GL_FALSE,0,nullptr);
        }
        const auto indices=r.count(30000000);if(!vertexCount||indices%3)throw std::runtime_error("Invalid environment triangle count");std::vector<unsigned> data(indices);for(auto& x:data){x=r.u32();if(x>=vertexCount)throw std::runtime_error("Environment vertex index out of range");}
        glGenBuffers(1,&mesh->index);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,mesh->index);glBufferData(GL_ELEMENT_ARRAY_BUFFER,data.size()*4,data.data(),GL_STATIC_DRAW);mesh->count=static_cast<int>(indices);
        const auto instances=r.count(100000);for(unsigned i=0;i<instances;i++){Impl::Instance instance;for(int c=0;c<4;c++)for(int row=0;row<4;row++)instance.matrix[c][row]=r.f();for(int c=0;c<3;c++)instance.color[c]=r.f();instance.phase=r.f();mesh->instances.push_back(instance);}
        mesh->transparent=r.f()!=0;mesh->depthWrite=r.f()!=0;mesh->depthTest=r.f()!=0;mesh->side=static_cast<int>(r.f());mesh->blending=static_cast<int>(r.f());mesh->casts=r.f()!=0;
        const int instance=shader.attribute("instanceMatrix");
        if(instance>=0){
            glGenBuffers(1,&mesh->instanceBuffer);mesh->buffers.push_back(mesh->instanceBuffer);glBindBuffer(GL_ARRAY_BUFFER,mesh->instanceBuffer);
            for(int c=0;c<4;c++){glEnableVertexAttribArray(instance+c);glVertexAttribPointer(instance+c,4,GL_FLOAT,GL_FALSE,20*sizeof(float),reinterpret_cast<void*>(c*4*sizeof(float)));glVertexAttribDivisor(instance+c,1);}
            for(const auto& attribute:std::map<std::string,std::pair<int,int>>{{"instanceColor",{3,16}},{"aPhase",{1,19}}}){const int at=shader.attribute(attribute.first);if(at>=0){glEnableVertexAttribArray(at);glVertexAttribPointer(at,attribute.second.first,GL_FLOAT,GL_FALSE,20*sizeof(float),reinterpret_cast<void*>(attribute.second.second*sizeof(float)));glVertexAttribDivisor(at,1);}}
        }
        impl_->meshes.push_back(std::move(mesh));
    }
    if(r.at!=r.bytes.size())throw std::runtime_error("Trailing environment scene bytes");
    std::ifstream motions(assets/("scene-"+stage+".motion"));if(!motions)throw std::runtime_error("Missing environment motion records");unsigned m,i,count;std::string kind;
    while(motions>>m>>i>>kind>>count){if(m>=impl_->meshes.size()||i>=impl_->meshes[m]->instances.size()||count>16)throw std::runtime_error("Invalid environment motion record");auto& a=impl_->meshes[m]->instances[i];a.kind=kind;a.p.resize(count);for(auto& x:a.p)if(!(motions>>x)||!std::isfinite(x))throw std::runtime_error("Invalid environment motion parameter");}
    if(!impl_->dfg){glGenTextures(1,&impl_->dfg);glBindTexture(GL_TEXTURE_2D,impl_->dfg);glTexImage2D(GL_TEXTURE_2D,0,GL_RG16F,16,16,0,GL_RG,GL_HALF_FLOAT,CharacterDfg.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
    glBindVertexArray(0);
}
int EnvironmentScene::Draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& light,double t,
    const std::map<std::string,std::vector<float>>& theme,const std::function<double(double,double,double)>& water,int selection) const{
    int draws=0;
    glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,impl_->dfg);
    for(bool transparent:{false,true})for(const auto& ptr:impl_->meshes){
        const auto& mesh=*ptr;if(mesh.transparent!=transparent)continue;
        const bool far=mesh.name=="Terrain"||mesh.name=="CitySkyline"||mesh.name=="FarScenery"||mesh.name=="Trees"||mesh.name=="FerrisWheel";
        if(selection==1&&!far)continue;
        if(selection==2&&(far||mesh.name=="Sailboats"||mesh.name=="Gulls"||mesh.name=="LighthouseBeam"))continue;
        if(selection==3&&!mesh.casts)continue;
        auto& shader=*impl_->shaders.at(mesh.shader);shader.use();
        shader.uniform("iwDepthOnly",selection==3?1:0);
        for(const auto& [k,v]:impl_->defaults.at(mesh.shader))shader.uniform(k,v);
        for(const auto& [k,v]:theme)if(k.size()>1&&k[0]=='u')shader.uniform(k,std::vector<double>(v.begin(),v.end()));
        shader.uniform("uTime",t);shader.uniform("dfgLUT",6);shader.lights(light,view);
        shader.uniform("uSunDir",std::vector<double>{light.sunDirection.x,light.sunDirection.y,light.sunDirection.z});
        shader.uniform("uWaveTex",1);shader.uniform("uFoamTex",2);
        if(transparent){glEnable(GL_BLEND);if(mesh.name=="WaterlineStrips"){glBlendFuncSeparate(GL_ONE,GL_SRC_ALPHA,GL_ZERO,GL_ONE);glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1,-4);}else glBlendFunc(GL_SRC_ALPHA,mesh.blending==2?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);}else glDisable(GL_BLEND);
        if(mesh.depthTest)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);glDepthMask(mesh.depthWrite?GL_TRUE:GL_FALSE);glDepthFunc(GL_LEQUAL);
        if(mesh.side==2)glDisable(GL_CULL_FACE);else{glEnable(GL_CULL_FACE);glCullFace(mesh.side==1?GL_FRONT:GL_BACK);}
        glBindVertexArray(mesh.vao);
        std::vector<float> instanceData;
        for(const auto& a:mesh.instances){
            auto matrix=a.matrix;const auto& p=a.p;
            if(a.kind=="sail"&&p.size()==5){const double angle=p[0]+p[1]*t,sign=p[1]<0?-1.:1.;const double x=std::cos(angle)*p[2],z=std::sin(angle)*p[2];matrix=pose({x,water(x,z,t),z},.03*std::sin(t*.8+p[3]),std::atan2(-std::sin(angle)*sign,std::cos(angle)*sign),(.13+.05*std::sin(t*.6+p[3]))*sign,p[4]);}
            else if(a.kind=="buoy"&&p.size()==4)matrix=pose({p[0],water(p[0],p[1],t)+.06*std::sin(t*1.7+p[2]),p[1]},.09*std::sin(t*1.1+p[2]),p[2],.09*std::cos(t*.93+p[2]*1.3),p[3]);
            else if(a.kind=="gull"&&p.size()==7){const double angle=p[0]+p[1]*t,sign=p[1]<0?-1.:1.;matrix=pose({p[2]+std::cos(angle)*p[4],p[5]+std::sin(t*.4+p[0])*1.5,p[3]+std::sin(angle)*p[4]},0,std::atan2(-std::sin(angle)*sign,std::cos(angle)*sign),-.35*sign,p[6]);}
            else if(a.kind=="ferris"&&p.size()==4)matrix=pose({p[0],p[1],p[2]},0,p[3],t*.045);
            else if(a.kind=="lighthouse"&&p.size()==3)matrix=pose({p[0],p[1],p[2]},0,t*.55,0);
            else if(a.kind=="moored"&&p.size()==5){matrix=glm::translate(glm::dmat4(1),glm::dvec3(p[0],water(p[0],p[1],t)+.03*std::sin(t*1.3+p[2]),p[1]));matrix=glm::rotate(matrix,.012*std::sin(t*.7+p[2]*2),glm::dvec3(1,0,0));matrix=glm::rotate(matrix,p[4],glm::dvec3(0,1,0));matrix=glm::rotate(matrix,p[3]*std::sin(t*.9+p[2]),glm::dvec3(0,0,1));}
            if(mesh.instanceBuffer){for(int c=0;c<4;c++)for(int row=0;row<4;row++)instanceData.push_back(static_cast<float>(matrix[c][row]));for(int c=0;c<3;c++)instanceData.push_back(static_cast<float>(a.color[c]));instanceData.push_back(a.phase);continue;}
            auto diffuse=impl_->defaults.at(mesh.shader).find("diffuse");if(diffuse!=impl_->defaults.at(mesh.shader).end()&&diffuse->second.size()==3){auto c=diffuse->second;for(int k=0;k<3;k++)c[k]*=a.color[k];shader.uniform("diffuse",c);}
            const int phase=shader.attribute("aPhase");if(phase>=0)glVertexAttrib1f(phase,a.phase);
            shader.matrices(matrix,view,projection,eye);glDrawElements(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr);++draws;
        }
        if(mesh.instanceBuffer){glBindBuffer(GL_ARRAY_BUFFER,mesh.instanceBuffer);glBufferData(GL_ARRAY_BUFFER,instanceData.size()*sizeof(float),instanceData.data(),GL_STREAM_DRAW);shader.matrices(glm::dmat4(1),view,projection,eye);glDrawElementsInstanced(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr,static_cast<GLsizei>(mesh.instances.size()));++draws;}
        if(mesh.name=="WaterlineStrips")glDisable(GL_POLYGON_OFFSET_FILL);
    }
    glBindVertexArray(0);glDepthMask(GL_TRUE);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);return draws;
}
}
