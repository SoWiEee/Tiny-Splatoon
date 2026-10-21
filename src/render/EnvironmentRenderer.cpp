#include "EnvironmentRenderer.hpp"
#include "EnvironmentScene.hpp"
#include "DirectionalShadow.hpp"
#include "ReferenceData.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <zlib.h>

namespace restoration {
namespace {
std::string readText(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);
    if(!stream) throw std::runtime_error("Missing source environment asset: "+path.string());
    return {std::istreambuf_iterator<char>(stream),{}};
}
GLuint shader(GLenum type,const std::string& source) {
    GLuint id=glCreateShader(type);const char* ptr=source.c_str();glShaderSource(id,1,&ptr,nullptr);glCompileShader(id);
    GLint okay=0;glGetShaderiv(id,GL_COMPILE_STATUS,&okay);
    if(!okay){GLint length=0;glGetShaderiv(id,GL_INFO_LOG_LENGTH,&length);std::string log(length,'\0');glGetShaderInfoLog(id,length,nullptr,log.data());glDeleteShader(id);throw std::runtime_error("Source environment shader: "+log);}
    return id;
}
GLuint program(std::string vertex,std::string fragment,bool marina=false) {
    if(marina){vertex.insert(vertex.find('\n')+1,"#define MARINA\n");fragment.insert(fragment.find('\n')+1,"#define MARINA\n");}
    const GLuint vs=shader(GL_VERTEX_SHADER,vertex);GLuint fs=0;
    try{fs=shader(GL_FRAGMENT_SHADER,fragment);}catch(...){glDeleteShader(vs);throw;}
    GLuint id=glCreateProgram();glAttachShader(id,vs);glAttachShader(id,fs);glLinkProgram(id);glDeleteShader(vs);glDeleteShader(fs);
    GLint okay=0;glGetProgramiv(id,GL_LINK_STATUS,&okay);
    if(!okay){GLint length=0;glGetProgramiv(id,GL_INFO_LOG_LENGTH,&length);std::string log(length,'\0');glGetProgramInfoLog(id,length,nullptr,log.data());glDeleteProgram(id);throw std::runtime_error("Source environment program: "+log);}
    return id;
}
GLuint texture(const std::filesystem::path& path,int width,int height,int bits,bool repeat=false) {
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Missing source environment texture: "+path.string());
    const std::vector<unsigned char> zipped((std::istreambuf_iterator<char>(file)),{});
    std::vector<unsigned char> raw(20+static_cast<std::size_t>(width)*height*4*bits/8);
    uLongf size=static_cast<uLongf>(raw.size());
    if(uncompress(raw.data(),&size,zipped.data(),static_cast<uLong>(zipped.size()))!=Z_OK||size!=raw.size()||
       std::string(raw.begin(),raw.begin()+8)!="TSENV001")throw std::runtime_error("Invalid source environment texture: "+path.string());
    auto word=[&](int p){return static_cast<unsigned>(raw[p])|(static_cast<unsigned>(raw[p+1])<<8)|(static_cast<unsigned>(raw[p+2])<<16)|(static_cast<unsigned>(raw[p+3])<<24);};
    if(word(8)!=static_cast<unsigned>(width)||word(12)!=static_cast<unsigned>(height)||word(16)!=static_cast<unsigned>(bits))throw std::runtime_error("Source environment texture dimension mismatch: "+path.string());
    GLuint id=0;glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);
    glTexImage2D(GL_TEXTURE_2D,0,bits==16?GL_RGBA16F:GL_RGBA8,width,height,0,GL_RGBA,bits==16?GL_HALF_FLOAT:GL_UNSIGNED_BYTE,raw.data()+20);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,repeat?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,repeat?GL_REPEAT:GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,repeat?GL_REPEAT:GL_CLAMP_TO_EDGE);
    if(repeat)glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,0);return id;
}
void set(GLuint program,const char* name,const std::vector<float>& values){
    const GLint location=glGetUniformLocation(program,name);if(location<0)return;
    switch(values.size()){
    case 1:glUniform1f(location,values[0]);break;
    case 2:glUniform2fv(location,1,values.data());break;
    case 3:glUniform3fv(location,1,values.data());break;
    case 4:glUniform4fv(location,1,values.data());break;
    default:throw std::runtime_error(std::string("Invalid source environment uniform: ")+name);
    }
}
std::vector<float> value(const std::map<std::string,std::vector<float>>& theme,const char* key){
    const auto found=theme.find(key);if(found==theme.end())throw std::runtime_error(std::string("Missing source environment theme value: ")+key);return found->second;
}
struct State {
    GLint program=0,vao=0,array=0,element=0,active=0,depthFunc=0,depthMask=0,cull=0,blend=0,unit[10]{},cube[10]{},drawFbo=0,readFbo=0,viewport[4]{},srcRgb=0,dstRgb=0,srcAlpha=0,dstAlpha=0;
    GLfloat clear[4]{},offsetFactor=0,offsetUnits=0;GLboolean depth=GL_FALSE,offset=GL_FALSE;GLint renderbuffer=0,cullMode=GL_BACK;
    State(){
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array);glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&element);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);glGetIntegerv(GL_DEPTH_WRITEMASK,&depthMask);cull=glIsEnabled(GL_CULL_FACE);blend=glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);glGetIntegerv(GL_VIEWPORT,viewport);glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);depth=glIsEnabled(GL_DEPTH_TEST);
        glGetIntegerv(GL_BLEND_SRC_RGB,&srcRgb);glGetIntegerv(GL_BLEND_DST_RGB,&dstRgb);glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);
        glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer);glGetIntegerv(GL_CULL_FACE_MODE,&cullMode);offset=glIsEnabled(GL_POLYGON_OFFSET_FILL);glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&offsetFactor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&offsetUnits);
        for(int i=0;i<10;++i){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&unit[i]);glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&cube[i]);}glActiveTexture(active);
    }
    ~State(){glBindRenderbuffer(GL_RENDERBUFFER,renderbuffer);glCullFace(cullMode);glPolygonOffset(offsetFactor,offsetUnits);if(offset)glEnable(GL_POLYGON_OFFSET_FILL);else glDisable(GL_POLYGON_OFFSET_FILL);glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,array);for(int i=0;i<10;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,unit[i]);glBindTexture(GL_TEXTURE_CUBE_MAP,cube[i]);}glActiveTexture(active);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,drawFbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,readFbo);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glClearColor(clear[0],clear[1],clear[2],clear[3]);glBlendFuncSeparate(srcRgb,dstRgb,srcAlpha,dstAlpha);glDepthFunc(depthFunc);glDepthMask(depthMask);if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);}
};
}

EnvironmentRenderer::EnvironmentRenderer(std::filesystem::path assets):assets_(std::move(assets)) {}
EnvironmentRenderer::~EnvironmentRenderer(){
    glDeleteProgram(skyProgram_);glDeleteProgram(seaProgram_);glDeleteTextures(1,&cloud_);glDeleteTextures(1,&wave_);glDeleteTextures(1,&pmrem_);glDeleteTextures(1,&foam_);
    glDeleteTextures(1,&deckField_);glDeleteBuffers(1,&skyVbo_);glDeleteBuffers(1,&seaVbo_);glDeleteBuffers(1,&seaEbo_);glDeleteVertexArrays(1,&skyVao_);glDeleteVertexArrays(1,&seaVao_);
    glDeleteTextures(1,&planarColor_);glDeleteTextures(1,&farCube_);glDeleteFramebuffers(1,&planarFbo_);glDeleteRenderbuffers(1,&planarDepth_);
}

void EnvironmentRenderer::Load(const StageData& stage){
    State state;
    marina_=stage.water=="marina";time_=0;
    bounds_={stage.bounds.x,stage.bounds.z,stage.bounds.y,stage.bounds.w};
    const std::string name=stage.theme=="sunset"?"sunset":stage.theme=="golden"?"golden":"day";
    theme_.clear();std::istringstream lines(readText(assets_/("theme-"+name+".txt")));std::string line;
    while(std::getline(lines,line)){std::istringstream row(line);std::string key;row>>key;float n;std::vector<float> values;while(row>>n)values.push_back(n);if(!values.empty())theme_[key]=std::move(values);}
    glDeleteTextures(1,&cloud_);glDeleteTextures(1,&pmrem_);
    cloud_=texture(assets_/("cloud-"+name+".rgba16f.z"),2048,640,16);
    pmrem_=texture(assets_/("pmrem-"+name+".rgba16f.z"),768,1024,16);
    if(!wave_)wave_=texture(assets_/"wave.rgba8.z",256,256,8,true);
    glDeleteTextures(1,&foam_);
    std::ifstream foamInfo(assets_/("foam-"+std::string(stage.id)+".txt"));int foamWidth=0,foamHeight=0;
    if(!(foamInfo>>foamWidth>>foamHeight>>foamRect_.x>>foamRect_.y>>foamRect_.z>>foamRect_.w>>foamRange_))throw std::runtime_error("Missing source environment foam field metadata");
    if(foamWidth<=0||foamHeight<=0||foamWidth>4096||foamHeight>4096)throw std::runtime_error("Invalid source foam field dimensions");
    foam_=texture(assets_/("foam-"+std::string(stage.id)+".rgba8.z"),foamWidth,foamHeight,8);
    glBindTexture(GL_TEXTURE_2D,cloud_);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glDeleteProgram(skyProgram_);auto skyFragment=readText(assets_/"sky.frag");if(value(theme_,"skyShafts")[0]>0)skyFragment.insert(skyFragment.find('\n')+1,"#define SKY_SHAFTS\n");skyProgram_=program(readText(assets_/"sky.vert"),skyFragment);
    glDeleteProgram(seaProgram_);seaProgram_=program(readText(assets_/"sea.vert"),readText(assets_/"sea.frag"),marina_);
    decks_.clear();wet_.clear();std::ifstream rectFile(assets_/("scene-"+std::string(stage.id)+".rects"));int setIndex=0;Rect rect;
    if(!rectFile)throw std::runtime_error("Missing source environment rectangle sets");
    while(rectFile>>setIndex>>rect.x>>rect.z>>rect.hx>>rect.hz>>rect.ax>>rect.az){if(setIndex<0||setIndex>1||rect.hx<=0||rect.hz<=0)throw std::runtime_error("Invalid source environment rectangle");(setIndex?wet_:decks_).push_back(rect);}
    glDeleteTextures(1,&deckField_);deckField_=0;deckUniforms_.clear();
    if(decks_.size()>48||wet_.size()>24){
        bakeDeckField(std::string(stage.id));
        auto vertex=readText(assets_/"sea.vert"),fragment=readText(assets_/"sea.frag");
        vertex.insert(vertex.find('\n')+1,"#define DECK_FIELD\n");fragment.insert(fragment.find('\n')+1,"#define DECK_FIELD\n");
        glDeleteProgram(seaProgram_);seaProgram_=program(vertex,fragment,marina_);
    }
    if(!scenery_)scenery_=std::make_unique<EnvironmentScene>();
    scenery_->Load(assets_,std::string(stage.id));
    bakeFarReflection();
    if(!skyVao_){
        std::ifstream geometry(assets_/"sky.geometry.bin",std::ios::binary);unsigned count=0,indices=0;geometry.read(reinterpret_cast<char*>(&count),4);geometry.read(reinterpret_cast<char*>(&indices),4);if(!geometry||!count||count>1000000||indices)throw std::runtime_error("Invalid source sky geometry");
        std::vector<float> positions(std::size_t(count)*3);geometry.read(reinterpret_cast<char*>(positions.data()),positions.size()*4);if(!geometry)throw std::runtime_error("Truncated source sky geometry");
        skyVertices_=static_cast<GLsizei>(count);glGenVertexArrays(1,&skyVao_);glGenBuffers(1,&skyVbo_);glBindVertexArray(skyVao_);glBindBuffer(GL_ARRAY_BUFFER,skyVbo_);glBufferData(GL_ARRAY_BUFFER,positions.size()*4,positions.data(),GL_STATIC_DRAW);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,nullptr);
    }
    if(!seaVao_){
        std::ifstream geometry(assets_/"sea.geometry.bin",std::ios::binary);unsigned count=0,indices=0;geometry.read(reinterpret_cast<char*>(&count),4);geometry.read(reinterpret_cast<char*>(&indices),4);if(!geometry||!count||count>1000000||indices>6000000||indices%3)throw std::runtime_error("Invalid source sea geometry");
        std::vector<float> positions(std::size_t(count)*3);std::vector<unsigned> elements(indices);geometry.read(reinterpret_cast<char*>(positions.data()),positions.size()*4);geometry.read(reinterpret_cast<char*>(elements.data()),elements.size()*4);if(!geometry||std::any_of(elements.begin(),elements.end(),[&](auto i){return i>=count;}))throw std::runtime_error("Invalid source sea geometry data");
        seaIndices_=static_cast<GLsizei>(indices);glGenVertexArrays(1,&seaVao_);glGenBuffers(1,&seaVbo_);glGenBuffers(1,&seaEbo_);glBindVertexArray(seaVao_);glBindBuffer(GL_ARRAY_BUFFER,seaVbo_);glBufferData(GL_ARRAY_BUFFER,positions.size()*4,positions.data(),GL_STATIC_DRAW);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,seaEbo_);glBufferData(GL_ELEMENT_ARRAY_BUFFER,elements.size()*4,elements.data(),GL_STATIC_DRAW);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,nullptr);
    }
}

void EnvironmentRenderer::Update(double dt){time_+=std::clamp(dt,0.0,0.1);}
void EnvironmentRenderer::bakeDeckField(const std::string& stage){
    State state;std::ifstream input(assets_/("deck-"+stage+".txt"));int width=0,height=0;
    if(!(input>>width>>height)||width<1||height<1||width>8192||height>8192||std::uint64_t(width)*height>2300000)throw std::runtime_error("Invalid source deck field dimensions");
    std::string name;unsigned count=0;
    while(input>>name>>count){if(count<1||count>4)throw std::runtime_error("Invalid source deck field uniform");std::vector<float> values(count);for(auto& v:values)if(!(input>>v)||!std::isfinite(v))throw std::runtime_error("Invalid source deck field value");deckUniforms_[name]=std::move(values);}
    for(const auto* key:{"uN","uField","uR","uDeckFieldRect","uDeckFieldK","uDeckBox","uWetBox"})if(!deckUniforms_.count(key))throw std::runtime_error("Missing source deck field uniform");
    struct Bake {GLuint program=0,rects=0,output=0,fbo=0,vao=0,vbo=0;~Bake(){glDeleteProgram(program);glDeleteTextures(1,&rects);glDeleteTextures(1,&output);glDeleteFramebuffers(1,&fbo);glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);}} bake;
    bake.program=program(readText(assets_/"deck-field.vert"),readText(assets_/"deck-field.frag"));
    const auto n=std::max<std::size_t>({1,decks_.size(),wet_.size()});if(n>4096)throw std::runtime_error("Too many source deck rectangles");std::vector<float> rectangles(n*16,0);
    for(int row=0;row<2;row++){const auto& set=row?wet_:decks_;for(std::size_t i=0;i<set.size();i++){const auto& r=set[i];const std::size_t at=row*n*8+i*8;rectangles[at]=r.x;rectangles[at+1]=r.z;rectangles[at+2]=r.hx;rectangles[at+3]=r.hz;rectangles[at+4]=r.ax;rectangles[at+5]=r.az;}}
    glActiveTexture(GL_TEXTURE0);glGenTextures(1,&bake.rects);glBindTexture(GL_TEXTURE_2D,bake.rects);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,static_cast<GLsizei>(n*2),2,0,GL_RGBA,GL_FLOAT,rectangles.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenTextures(1,&bake.output);glBindTexture(GL_TEXTURE_2D,bake.output);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,width,height,0,GL_RGBA,GL_HALF_FLOAT,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1,&bake.fbo);glBindFramebuffer(GL_FRAMEBUFFER,bake.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,bake.output,0);if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Source deck field framebuffer incomplete");
    const float vertices[]={-1,-1,0,0,0, 1,-1,0,1,0, -1,1,0,0,1, -1,1,0,0,1, 1,-1,0,1,0, 1,1,0,1,1};
    glGenVertexArrays(1,&bake.vao);glBindVertexArray(bake.vao);glGenBuffers(1,&bake.vbo);glBindBuffer(GL_ARRAY_BUFFER,bake.vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    for(const auto* attr:{"position","uv"}){const GLint at=glGetAttribLocation(bake.program,attr);if(at<0)throw std::runtime_error("Missing source deck field vertex attribute");const bool uv=std::string(attr)=="uv";glEnableVertexAttribArray(at);glVertexAttribPointer(at,uv?2:3,GL_FLOAT,GL_FALSE,5*sizeof(float),reinterpret_cast<const void*>(uv?3*sizeof(float):0));}
    glUseProgram(bake.program);for(const auto& [key,values]:deckUniforms_)set(bake.program,key.c_str(),values);glBindTexture(GL_TEXTURE_2D,bake.rects);glUniform1i(glGetUniformLocation(bake.program,"uRectTex"),0);glViewport(0,0,width,height);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDrawArrays(GL_TRIANGLES,0,6);
    deckField_=bake.output;bake.output=0;
}
void EnvironmentRenderer::uniforms(GLuint program,const glm::vec3& eye) const{
    for(const auto& [name,values]:theme_)if(name.size()>1&&name[0]=='u'&&name!="uMarinaK")set(program,name.c_str(),values);
    set(program,"uSunDir",value(theme_,"sunDirection"));
    glUniform1f(glGetUniformLocation(program,"uTime"),static_cast<float>(time_));
    glUniform3fv(glGetUniformLocation(program,"cameraPosition"),1,glm::value_ptr(eye));
    glUniform4fv(glGetUniformLocation(program,"uArena"),1,glm::value_ptr(bounds_));
    if(marina_)set(program,"uMarinaK",value(theme_,"uMarinaK"));
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,cloud_);glUniform1i(glGetUniformLocation(program,"uCloudTex"),0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,wave_);glUniform1i(glGetUniformLocation(program,"uWaveTex"),1);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,foam_);glUniform1i(glGetUniformLocation(program,"uFoamTex"),2);
    glUniform4fv(glGetUniformLocation(program,"uFoamRect"),1,glm::value_ptr(foamRect_));
    if(marina_){const auto k=value(theme_,"uMarinaK");glUniform4f(glGetUniformLocation(program,"uMarinaK"),k[0],k[1],k[2],foamRange_);}
    glUniform1i(glGetUniformLocation(program,"uRectCount"),deckField_?0:static_cast<GLint>(decks_.size()));
    glUniform1i(glGetUniformLocation(program,"uWetCount"),deckField_?0:static_cast<GLint>(wet_.size()));
    if(deckField_){for(const auto& [name,values]:deckUniforms_)set(program,name.c_str(),values);glActiveTexture(GL_TEXTURE5);glBindTexture(GL_TEXTURE_2D,deckField_);glUniform1i(glGetUniformLocation(program,"uDeckField"),5);}
    else {
        for(std::size_t i=0;i<decks_.size();++i){const auto& r=decks_[i];const std::string n=std::to_string(i);glUniform4f(glGetUniformLocation(program,("uRects["+n+"]").c_str()),r.x,r.z,r.hx,r.hz);glUniform2f(glGetUniformLocation(program,("uRectAx["+n+"]").c_str()),r.ax,r.az);}
        for(std::size_t i=0;i<wet_.size();++i){const auto& r=wet_[i];const std::string n=std::to_string(i);glUniform4f(glGetUniformLocation(program,("uWet["+n+"]").c_str()),r.x,r.z,r.hx,r.hz);glUniform2f(glGetUniformLocation(program,("uWetAx["+n+"]").c_str()),r.ax,r.az);}
    }
    glUniform1f(glGetUniformLocation(program,"uFarOn"),reflectionsEnabled_&&marina_&&farCube_?1.f:0.f);glUniform1f(glGetUniformLocation(program,"uReflOn"),reflectionsEnabled_&&reflection_?1.f:0.f);
    glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_CUBE_MAP,farCube_);glUniform1i(glGetUniformLocation(program,"uFarCube"),4);
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,reflection_);glUniform1i(glGetUniformLocation(program,"uReflTex"),3);
    glUniformMatrix4fv(glGetUniformLocation(program,"uReflMat"),1,GL_FALSE,glm::value_ptr(reflectionMatrix_));
}

int EnvironmentRenderer::DrawSky(const glm::mat4& view,const glm::mat4& projection,const glm::vec3& eye) const{
    if(!skyProgram_)return 0;State state;glUseProgram(skyProgram_);uniforms(skyProgram_,eye);
    glUniformMatrix4fv(glGetUniformLocation(skyProgram_,"viewMatrix"),1,GL_FALSE,glm::value_ptr(view));glUniformMatrix4fv(glGetUniformLocation(skyProgram_,"projectionMatrix"),1,GL_FALSE,glm::value_ptr(projection));
    glDisable(GL_CULL_FACE);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);glBindVertexArray(skyVao_);glDrawArrays(GL_TRIANGLES,0,skyVertices_);return 1;
}
int EnvironmentRenderer::DrawSea(const glm::mat4& view,const glm::mat4& projection,const glm::vec3& eye,const DirectionalShadow* shadow) const{
    if(!seaProgram_)return 0;State state;glUseProgram(seaProgram_);uniforms(seaProgram_,eye);
    const glm::mat4 model=glm::translate(glm::mat4(1.0f),glm::vec3(0,-1.6f,0));
    glUniformMatrix4fv(glGetUniformLocation(seaProgram_,"modelMatrix"),1,GL_FALSE,glm::value_ptr(model));glUniformMatrix4fv(glGetUniformLocation(seaProgram_,"viewMatrix"),1,GL_FALSE,glm::value_ptr(view));glUniformMatrix4fv(glGetUniformLocation(seaProgram_,"projectionMatrix"),1,GL_FALSE,glm::value_ptr(projection));
    const glm::mat4 mv=view*model;const glm::mat3 normal=glm::transpose(glm::inverse(glm::mat3(mv)));glUniformMatrix4fv(glGetUniformLocation(seaProgram_,"modelViewMatrix"),1,GL_FALSE,glm::value_ptr(mv));glUniformMatrix3fv(glGetUniformLocation(seaProgram_,"normalMatrix"),1,GL_FALSE,glm::value_ptr(normal));
    glUniform1i(glGetUniformLocation(seaProgram_,"receiveShadow"),shadow?1:0);
    if(shadow){
        glm::mat4 bias(1);bias[0][0]=bias[1][1]=bias[2][2]=.5f;bias[3]={.5f,.5f,.5f,1};const glm::mat4 matrix=bias*glm::mat4(shadow->matrix());
        glUniformMatrix4fv(glGetUniformLocation(seaProgram_,"directionalShadowMatrix[0]"),1,GL_FALSE,glm::value_ptr(matrix));
        glUniform1f(glGetUniformLocation(seaProgram_,"directionalLightShadows[0].shadowIntensity"),1);glUniform1f(glGetUniformLocation(seaProgram_,"directionalLightShadows[0].shadowBias"),float(shadow->bias()));glUniform1f(glGetUniformLocation(seaProgram_,"directionalLightShadows[0].shadowNormalBias"),float(shadow->normalBias()));glUniform1f(glGetUniformLocation(seaProgram_,"directionalLightShadows[0].shadowRadius"),float(shadow->radius()));glUniform2f(glGetUniformLocation(seaProgram_,"directionalLightShadows[0].shadowMapSize"),float(shadow->size()),float(shadow->size()));
        glActiveTexture(GL_TEXTURE7);glBindTexture(GL_TEXTURE_2D,shadow->texture());glUniform1i(glGetUniformLocation(seaProgram_,"directionalShadowMap[0]"),7);
    }
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);glBindVertexArray(seaVao_);glDrawElements(GL_TRIANGLES,seaIndices_,GL_UNSIGNED_INT,nullptr);return 1;
}
CharacterLightState EnvironmentRenderer::Lights() const{
    CharacterLightState lights;auto vec=[&](const char* name){const auto v=value(theme_,name);return glm::vec3(v[0],v[1],v[2]);};
    lights.sunDirection=vec("sunDirection");lights.sunColor=vec("sunColor")*value(theme_,"sunIntensity")[0];
    lights.skyDirection={0,1,0};lights.skyColor=vec("hemisphereSky");lights.groundColor=vec("hemisphereGround");
    lights.environmentTexture=pmrem_;lights.environmentIntensity=value(theme_,"environmentIntensity")[0];
    lights.fogColor=vec("fogColor");const auto fog=value(theme_,"fog");lights.fogNear=fog[0];lights.fogFar=fog[1];
    return lights;
}
void EnvironmentRenderer::SetReflectionTexture(GLuint texture,const glm::mat4& matrix){reflection_=texture;reflectionMatrix_=matrix;}
int EnvironmentRenderer::DrawScenery(const glm::mat4& view,const glm::mat4& projection,const glm::vec3& eye)const{
    if(!scenery_)return 0;State state;glUseProgram(seaProgram_);uniforms(seaProgram_,eye);
    return scenery_->Draw(glm::dmat4(view),glm::dmat4(projection),glm::dvec3(eye),Lights(),time_,theme_,[&](double x,double z,double t){return waterHeight(x,z,t);});
}
double EnvironmentRenderer::waterHeight(double x,double z,double t)const{
    double d=1e5;
    const auto distance=[&](const Rect& r){const double dx=x-r.x,dz=z-r.z;const double qx=std::abs(dx*r.ax+dz*r.az)-r.hx,qz=std::abs(-dx*r.az+dz*r.ax)-r.hz;return std::hypot(std::max(qx,0.),std::max(qz,0.))+std::min(std::max(qx,qz),0.);};
    for(const auto& r:decks_)d=std::min(d,distance(r));if(marina_)for(const auto& r:wet_)d=std::min(d,distance(r));
    const auto smooth=[](double a,double b,double v){const double u=std::clamp((v-a)/(b-a),0.,1.);return u*u*(3-2*u);};
    double amp=.035+(.16-.035)*smooth(3,70,d);
    if(marina_){const double qx=std::abs(x-(bounds_.x+bounds_.z)/2)-(bounds_.z-bounds_.x)/2,qz=std::abs(z-(bounds_.y+bounds_.w)/2)-(bounds_.w-bounds_.y)/2;const double da=std::hypot(std::max(qx,0.),std::max(qz,0.))+std::min(std::max(qx,qz),0.);amp*=.45+.55*smooth(60,210,da);}
    return -1.6+amp*(.45*std::sin(x*.11+z*.047+t*.95)+.35*std::sin(-x*.052+z*.097+t*1.13+1.7)+.2*std::sin(x*.173-z*.141+t*1.61+4.1));
}
int EnvironmentRenderer::DrawShadow(const glm::dmat4& lightViewProjection)const{
    if(!scenery_)return 0;State state;
    return scenery_->Draw(glm::dmat4(1),lightViewProjection,glm::dvec3(0),Lights(),time_,theme_,[&](double x,double z,double t){return waterHeight(x,z,t);},3);
}
void EnvironmentRenderer::bakeFarReflection(){
    if(!marina_||!reflectionsEnabled_)return;State state;
    glDeleteTextures(1,&farCube_);glGenTextures(1,&farCube_);glBindTexture(GL_TEXTURE_CUBE_MAP,farCube_);
    for(int face=0;face<6;face++)glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,GL_RGBA16F,512,512,0,GL_RGBA,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    for(const auto axis:{GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_TEXTURE_WRAP_R})glTexParameteri(GL_TEXTURE_CUBE_MAP,axis,GL_CLAMP_TO_EDGE);
    GLuint fbo=0,depth=0;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,512,512);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
    const glm::vec3 eye((bounds_.x+bounds_.z)/2,-.1f,(bounds_.y+bounds_.w)/2);
    const glm::vec3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}},ups[]={{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
    for(int face=0;face<6;face++){
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,farCube_,0);if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete far reflection framebuffer");
        glViewport(0,0,512,512);glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        scenery_->Draw(glm::dmat4(glm::lookAt(eye,eye+directions[face],ups[face])),glm::dmat4(glm::perspective(glm::radians(90.f),1.f,2.f,30000.f)),glm::dvec3(eye),Lights(),0,theme_,[&](double x,double z,double t){return waterHeight(x,z,t);},1);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP,farCube_);glGenerateMipmap(GL_TEXTURE_CUBE_MAP);glDeleteFramebuffers(1,&fbo);glDeleteRenderbuffers(1,&depth);
}
int EnvironmentRenderer::RenderReflection(const glm::mat4& view,const glm::mat4& projection,const glm::vec3& eye,int width,int height,
    const std::function<int(const glm::mat4&,const glm::mat4&,const glm::vec3&)>& draw){
    if(!marina_||!reflectionsEnabled_||eye.y<-1.55f){reflection_=0;return 0;}State state;
    const int w=std::max(64,int(std::round(width*.4))),h=std::max(64,int(std::round(height*.4)));
    if(!planarFbo_){glGenFramebuffers(1,&planarFbo_);glGenTextures(1,&planarColor_);glGenRenderbuffers(1,&planarDepth_);}
    glBindFramebuffer(GL_FRAMEBUFFER,planarFbo_);
    if(w!=reflectionWidth_||h!=reflectionHeight_){glBindTexture(GL_TEXTURE_2D,planarColor_);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,planarColor_,0);glBindRenderbuffer(GL_RENDERBUFFER,planarDepth_);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,planarDepth_);reflectionWidth_=w;reflectionHeight_=h;}
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete planar reflection framebuffer");
    const glm::mat4 camera=glm::inverse(view);glm::vec3 fwd=-glm::vec3(camera[2]),up(camera[1]),reflectedEye=eye;reflectedEye.y=-3.2f-eye.y;fwd.y=-fwd.y;up.y=-up.y;
    const glm::mat4 reflectedView=glm::lookAt(reflectedEye,reflectedEye+fwd,up);glm::mat4 clipped=projection;
    glm::vec4 plane=glm::transpose(glm::inverse(reflectedView))*glm::vec4(0,1,0,1.6f);
    const auto sign=[](float x){return x>0?1.f:x<0?-1.f:0.f;};const glm::vec4 q((sign(plane.x)+clipped[2][0])/clipped[0][0],(sign(plane.y)+clipped[2][1])/clipped[1][1],-1,(1+clipped[2][2])/clipped[3][2]);plane*=2/glm::dot(plane,q);
    clipped[0][2]=plane.x;clipped[1][2]=plane.y;clipped[2][2]=plane.z+1;clipped[3][2]=plane.w;
    glViewport(0,0,w,h);glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    int draws=draw(reflectedView,clipped,reflectedEye);
    draws+=scenery_->Draw(glm::dmat4(reflectedView),glm::dmat4(clipped),glm::dvec3(reflectedEye),Lights(),time_,theme_,[&](double x,double z,double t){return waterHeight(x,z,t);},2);
    reflection_=planarColor_;glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,planarColor_);glGenerateMipmap(GL_TEXTURE_2D);
    glm::mat4 bias(1);bias[0][0]=bias[1][1]=bias[2][2]=.5f;bias[3]={.5f,.5f,.5f,1};reflectionMatrix_=bias*clipped*reflectedView;
    return draws;
}
} // namespace restoration
