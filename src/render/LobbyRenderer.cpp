#include "LobbyRenderer.hpp"
#include "../data/Json.hpp"
#include "CharacterRenderData.hpp"
#include "EnvironmentTextureCatalog.hpp"
#include <glad/glad.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <zlib.h>

namespace restoration {
namespace {
using data::JsonValue;
constexpr std::size_t MaxBundleBytes=512u*1024u*1024u;
constexpr std::size_t MaxTextureBytes=256u*1024u*1024u;
constexpr unsigned MaxDimension=16384;

std::vector<unsigned char> readBytes(const std::filesystem::path& path,std::size_t limit=MaxBundleBytes){
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Missing source lobby asset: "+path.string());
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});
    if(bytes.empty()||bytes.size()>limit)throw std::runtime_error("Invalid source lobby asset size: "+path.string());return bytes;
}
std::string readText(const std::filesystem::path& path,std::size_t limit=16u*1024u*1024u){
    const auto bytes=readBytes(path,limit);return {bytes.begin(),bytes.end()};
}
const JsonValue& required(const JsonValue& value,const char* key){const auto* result=value.get(key);if(!result)throw std::runtime_error(std::string("Missing lobby asset field: ")+key);return *result;}
const JsonValue::Object& object(const JsonValue& value,const char* label){const auto* result=value.object();if(!result)throw std::runtime_error(std::string("Invalid lobby asset object: ")+label);return *result;}
const JsonValue::Array& array(const JsonValue& value,const char* label){const auto* result=std::get_if<JsonValue::Array>(&value.value);if(!result)throw std::runtime_error(std::string("Invalid lobby asset array: ")+label);return *result;}
std::string string(const JsonValue& value,const char* label){const auto* result=std::get_if<std::string>(&value.value);if(!result||result->empty())throw std::runtime_error(std::string("Invalid lobby asset string: ")+label);return *result;}
double number(const JsonValue& value,const char* label){const auto* result=std::get_if<double>(&value.value);if(!result||!std::isfinite(*result))throw std::runtime_error(std::string("Invalid lobby asset number: ")+label);return *result;}
unsigned integer(const JsonValue& value,const char* label,unsigned maximum=std::numeric_limits<unsigned>::max()){
    const auto n=number(value,label);if(n<0||std::floor(n)!=n||n>maximum)throw std::runtime_error(std::string("Invalid lobby asset integer: ")+label);return static_cast<unsigned>(n);
}
bool boolean(const JsonValue& value,const char* label){const auto* result=std::get_if<bool>(&value.value);if(!result)throw std::runtime_error(std::string("Invalid lobby asset boolean: ")+label);return *result;}
glm::dvec3 vec3(const JsonValue& value,const char* label){const auto& values=array(value,label);if(values.size()!=3)throw std::runtime_error(std::string("Invalid lobby asset vector: ")+label);return {number(values[0],label),number(values[1],label),number(values[2],label)};}
LobbySpot spot(const JsonValue& value,const char* label){return {vec3(required(value,"position"),label),number(required(value,"yaw"),label)};}
LobbyCamera camera(const JsonValue& value,const char* label){LobbyCamera c;c.position=vec3(required(value,"position"),label);c.target=vec3(required(value,"target"),label);c.fov=number(required(value,"fov"),label);c.nearPlane=number(required(value,"near"),label);c.farPlane=number(required(value,"far"),label);if(c.fov<=0||c.fov>=180||c.nearPlane<=0||c.farPlane<=c.nearPlane)throw std::runtime_error(std::string("Invalid lobby camera: ")+label);return c;}
LobbyLayout parseLayout(const JsonValue& root){
    if(integer(required(root,"schema"),"layout.schema")!=1)throw std::runtime_error("Unsupported source lobby layout schema");
    LobbyLayout result;result.lobbyCamera=camera(required(root,"lobbyCamera"),"lobbyCamera");result.hubCamera=camera(required(root,"hubCamera"),"hubCamera");result.hubSpot=spot(required(root,"hubSpot"),"hubSpot");
    const auto& spots=array(required(root,"lineupSpots"),"lineupSpots");const auto& arrivals=array(required(root,"arrivalPaths"),"arrivalPaths");const auto& exits=array(required(root,"exitPaths"),"exitPaths");
    if(spots.size()!=8||arrivals.size()!=8||exits.size()!=8)throw std::runtime_error("Source lobby layout must have eight lineup spots and routes");
    for(std::size_t i=0;i<8;i++){
        result.lineupSpots[i]=spot(spots[i],"lineupSpot");
        for(const auto& p:array(arrivals[i],"arrivalPath"))result.arrivalPaths[i].points.push_back(vec3(p,"arrivalPath point"));
        for(const auto& p:array(exits[i],"exitPath"))result.exitPaths[i].points.push_back(vec3(p,"exitPath point"));
        if(result.arrivalPaths[i].points.empty()||result.exitPaths[i].points.empty())throw std::runtime_error("Empty source lobby route");
    }
    return result;
}
struct Reader {
    std::vector<unsigned char> bytes;std::size_t at=0;
    void need(std::size_t n){if(at>bytes.size()||n>bytes.size()-at)throw std::runtime_error("Truncated source lobby geometry");}
    unsigned u32(){need(4);unsigned v=0;for(unsigned i=0;i<4;i++)v|=unsigned(bytes[at+i])<<(8*i);at+=4;return v;}
    unsigned count(unsigned max,const char* label){const unsigned n=u32();if(n>max)throw std::runtime_error(std::string("Oversized source lobby ")+label);return n;}
    float f(){const auto u=u32();float value=0;std::memcpy(&value,&u,4);if(!std::isfinite(value))throw std::runtime_error("Non-finite source lobby vertex");return value;}
    std::string str(){const auto n=count(4096,"string");need(n);std::string value(reinterpret_cast<const char*>(bytes.data()+at),n);at+=n;if(!data::detail::ValidUtf8(value,true))throw std::runtime_error("Invalid source lobby asset text");return value;}
};
std::vector<unsigned char> decompress(const std::filesystem::path& path,const char* outer,const char* inner){
    const auto packed=readBytes(path);if(packed.size()<12||std::string(packed.begin(),packed.begin()+8)!=outer)throw std::runtime_error(std::string("Invalid source lobby bundle header: ")+path.string());
    const unsigned size=unsigned(packed[8])|(unsigned(packed[9])<<8)|(unsigned(packed[10])<<16)|(unsigned(packed[11])<<24);if(size<12||size>MaxBundleBytes)throw std::runtime_error("Invalid source lobby bundle length");
    Reader r;r.bytes.resize(size);uLongf output=size;if(packed.size()-12>std::numeric_limits<uLong>::max()||uncompress(r.bytes.data(),&output,packed.data()+12,static_cast<uLong>(packed.size()-12))!=Z_OK||output!=size)throw std::runtime_error("Invalid source lobby compression");
    r.need(8);if(std::string(r.bytes.begin(),r.bytes.begin()+8)!=inner)throw std::runtime_error("Invalid source lobby geometry magic");r.at=8;return r.bytes;
}
struct TextureInfo {std::string file;unsigned width=0,height=0,layers=0,bits=0;bool srgb=false;};
std::uint32_t word(const std::vector<unsigned char>& bytes,std::size_t at){if(at+4>bytes.size())throw std::runtime_error("Truncated source lobby texture header");return unsigned(bytes[at])|(unsigned(bytes[at+1])<<8)|(unsigned(bytes[at+2])<<16)|(unsigned(bytes[at+3])<<24);}
std::vector<unsigned char> texturePixels(const std::filesystem::path& path,const TextureInfo& info){
    if(!info.width||!info.height||!info.layers||info.width>MaxDimension||info.height>MaxDimension||info.layers>2048||info.bits!=8)throw std::runtime_error("Invalid source lobby texture descriptor: "+path.string());
    const std::uint64_t pixelBytes=std::uint64_t(info.width)*info.height*info.layers*4;if(pixelBytes>MaxTextureBytes)throw std::runtime_error("Source lobby texture is too large: "+path.string());
    const auto compressed=readBytes(path,MaxTextureBytes);std::vector<unsigned char> raw(24+static_cast<std::size_t>(pixelBytes));uLongf size=static_cast<uLongf>(raw.size());
    if(uncompress(raw.data(),&size,compressed.data(),static_cast<uLong>(compressed.size()))!=Z_OK||size!=raw.size()||std::string(raw.begin(),raw.begin()+8)!="TSTEX001")throw std::runtime_error("Invalid source lobby texture: "+path.string());
    if(word(raw,8)!=info.width||word(raw,12)!=info.height||word(raw,16)!=info.layers||word(raw,20)!=unsigned(info.srgb))throw std::runtime_error("Source lobby texture header mismatch: "+path.string());
    return {raw.begin()+24,raw.end()};
}
GLenum wrap(unsigned value){switch(value){case 1000:return GL_REPEAT;case 1002:return GL_MIRRORED_REPEAT;case 1001:return GL_CLAMP_TO_EDGE;default:throw std::runtime_error("Unsupported source lobby texture wrap");}}
GLenum filter(unsigned value){switch(value){case 1003:return GL_NEAREST;case 1004:return GL_NEAREST_MIPMAP_NEAREST;case 1005:return GL_NEAREST_MIPMAP_LINEAR;case 1006:return GL_LINEAR;case 1007:return GL_LINEAR_MIPMAP_NEAREST;case 1008:return GL_LINEAR_MIPMAP_LINEAR;default:throw std::runtime_error("Unsupported source lobby texture filter");}}
GLenum blendFactor(unsigned value){switch(value){case 200:return GL_ZERO;case 201:return GL_ONE;case 202:return GL_SRC_COLOR;case 203:return GL_ONE_MINUS_SRC_COLOR;case 204:return GL_SRC_ALPHA;case 205:return GL_ONE_MINUS_SRC_ALPHA;case 206:return GL_DST_ALPHA;case 207:return GL_ONE_MINUS_DST_ALPHA;case 208:return GL_DST_COLOR;case 209:return GL_ONE_MINUS_DST_COLOR;case 210:return GL_SRC_ALPHA_SATURATE;case 211:return GL_CONSTANT_COLOR;case 212:return GL_ONE_MINUS_CONSTANT_COLOR;case 213:return GL_CONSTANT_ALPHA;case 214:return GL_ONE_MINUS_CONSTANT_ALPHA;default:throw std::runtime_error("Unsupported source lobby blend factor");}}
GLenum blendEquation(unsigned value){switch(value){case 100:return GL_FUNC_ADD;case 101:return GL_FUNC_SUBTRACT;case 102:return GL_FUNC_REVERSE_SUBTRACT;case 103:return GL_MIN;case 104:return GL_MAX;default:throw std::runtime_error("Unsupported source lobby blend equation");}}
GLenum depthFunction(unsigned value){switch(value){case 0:return GL_NEVER;case 1:return GL_ALWAYS;case 2:return GL_LESS;case 3:return GL_LEQUAL;case 4:return GL_EQUAL;case 5:return GL_GEQUAL;case 6:return GL_GREATER;case 7:return GL_NOTEQUAL;default:throw std::runtime_error("Unsupported source lobby depth function");}}
struct GlState {
    GLint program=0,vao=0,array=0,active=0,activeTexture2d=0,activeTextureArray=0,depthFunc=0,cullFace=0,frontFace=0,blendSrcRgb=0,blendDstRgb=0,blendSrcAlpha=0,blendDstAlpha=0,blendEqRgb=0,blendEqAlpha=0,unpack=0,unpackBuffer=0,drawFramebuffer=0,readFramebuffer=0,renderbuffer=0,viewport[4]{};
    GLfloat clearColor[4]{};GLdouble clearDepth=1.0;GLboolean colorMask[4]{GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE};GLboolean depthMask=GL_TRUE;bool depth=false,cull=false,blend=false,scissor=false;
    std::array<GLint,22> textures{};
    GlState(){
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_TEXTURE_BINDING_2D,&activeTexture2d);glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&activeTextureArray);
        glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);glGetIntegerv(GL_CULL_FACE_MODE,&cullFace);glGetIntegerv(GL_FRONT_FACE,&frontFace);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFramebuffer);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFramebuffer);glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer);glGetIntegerv(GL_VIEWPORT,viewport);glGetFloatv(GL_COLOR_CLEAR_VALUE,clearColor);glGetDoublev(GL_DEPTH_CLEAR_VALUE,&clearDepth);glGetBooleanv(GL_COLOR_WRITEMASK,colorMask);
        glGetIntegerv(GL_BLEND_SRC_RGB,&blendSrcRgb);glGetIntegerv(GL_BLEND_DST_RGB,&blendDstRgb);glGetIntegerv(GL_BLEND_SRC_ALPHA,&blendSrcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&blendDstAlpha);glGetIntegerv(GL_BLEND_EQUATION_RGB,&blendEqRgb);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&blendEqAlpha);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpackBuffer);depth=glIsEnabled(GL_DEPTH_TEST);cull=glIsEnabled(GL_CULL_FACE);blend=glIsEnabled(GL_BLEND);scissor=glIsEnabled(GL_SCISSOR_TEST);
        for(int i=0;i<11;i++){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i*2]);glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&textures[i*2+1]);}glActiveTexture(active);
    }
    ~GlState(){
        glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,array);glDepthFunc(depthFunc);glDepthMask(depthMask);glCullFace(cullFace);glFrontFace(frontFace);glClearDepth(clearDepth);
        glBlendFuncSeparate(blendSrcRgb,blendDstRgb,blendSrcAlpha,blendDstAlpha);glBlendEquationSeparate(blendEqRgb,blendEqAlpha);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(drawFramebuffer));glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(readFramebuffer));glBindRenderbuffer(GL_RENDERBUFFER,static_cast<GLuint>(renderbuffer));glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glClearColor(clearColor[0],clearColor[1],clearColor[2],clearColor[3]);glColorMask(colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
        auto enabled=[](GLenum cap,bool value){if(value)glEnable(cap);else glDisable(cap);};enabled(GL_DEPTH_TEST,depth);enabled(GL_CULL_FACE,cull);enabled(GL_BLEND,blend);enabled(GL_SCISSOR_TEST,scissor);
        for(int i=0;i<11;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(textures[i*2]));glBindTexture(GL_TEXTURE_2D_ARRAY,static_cast<GLuint>(textures[i*2+1]));}glActiveTexture(active);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(activeTexture2d));glBindTexture(GL_TEXTURE_2D_ARRAY,static_cast<GLuint>(activeTextureArray));
        glPixelStorei(GL_UNPACK_ALIGNMENT,unpack);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpackBuffer);
    }
};
}

struct LobbyRenderer::Impl {
    struct Texture {GLuint id=0;GLenum target=GL_TEXTURE_2D;~Texture(){if(id)glDeleteTextures(1,&id);}};
    struct Binding {std::string uniform,key,file;unsigned unit=0,wrapS=1001,wrapT=1001,minFilter=1008,magFilter=1006;bool mipmaps=true,dynamic=false;};
    struct Instance {glm::dmat4 matrix{1};glm::dvec3 color{1};float phase=0;};
    struct Mesh {GLuint vao=0,index=0,instanceBuffer=0;std::vector<GLuint> buffers;std::string name,shaderKey;GLsizei count=0;bool transparent=false,depthWrite=true,depthTest=true,casts=false,instanced=false,dynamicDrips=false;unsigned side=0,blending=1,depthFunc=3,srcRgb=204,dstRgb=205,srcAlpha=204,dstAlpha=205,eqRgb=100,eqAlpha=100;std::size_t activeInstances=0,instanceCapacity=0;std::vector<Instance> instances;std::vector<Binding> textures;~Mesh(){if(vao)glDeleteVertexArrays(1,&vao);if(index)glDeleteBuffers(1,&index);if(instanceBuffer)glDeleteBuffers(1,&instanceBuffer);if(!buffers.empty())glDeleteBuffers(static_cast<GLsizei>(buffers.size()),buffers.data());}};
    struct Drip {glm::dvec3 position{0};double velocity=0;bool active=false;};
    struct BulbString {glm::dvec3 a{0},b{0};double sag=0;std::size_t count=0,first=0;};
    struct Spot {glm::dvec3 position{0},target{0},color{1};glm::dmat4 cookieMatrix{1};std::string cookieKey;double intensity=0,distance=0,angle=0,penumbra=0,decay=2,shadowNear=0.5,shadowFar=500,shadowBias=0,shadowNormalBias=0,shadowRadius=1,shadowAspect=1;unsigned shadowSize=0;bool visible=true,castsShadow=false;};
    struct Point {glm::dvec3 position{0},color{1};double intensity=0,distance=0,decay=2;bool visible=true;};
    std::filesystem::path root;LobbyLayout layout;CharacterLightState lights;std::unique_ptr<Texture> pmrem,dfg;
    GLuint reflectionFbo=0,reflectionTexture=0,reflectionDepth=0,keyShadowFbo=0,keyShadowTexture=0;int reflectionWidth=0,reflectionHeight=0;unsigned keyShadowSize=0;bool reflectionsEnabled=true,reflectionReady=false,keyShadowEnabled=true,keyShadowReady=false;glm::dmat4 reflectionMatrix{1},keyShadowView{1},keyShadowProjection{1};glm::dvec2 reflectionResolution{0};
    std::array<glm::vec4,6> ripples{};std::size_t nextRipple=0;
    std::vector<glm::dvec3> dripSources;std::vector<Drip> drips;Mesh* dripMesh=nullptr;double nextDrip=0,lastDripTime=0,dripGravity=9.8,dripIntervalMin=1.4,dripIntervalJitter=1.8;bool haveDripTime=false;
    Mesh *bulbMesh=nullptr,*glowMesh=nullptr,*mothMesh=nullptr;std::vector<BulbString> bulbStrings;std::array<std::size_t,2> headlightIndices{};std::array<Instance,2> headlightInitial{};glm::dvec3 mothCenter{0},headlightColor{1},sweepPosition{0},sweepTarget{0},lastTeamA{1,0.216,0.03},lastTeamB{0.03,0.216,1};Spot keySpot,spillSpot,rimSpot,sweepSpot;std::map<std::string,Point> pointLights;std::string spillCookieFile;std::size_t bulbCount=0,bulbGlowCount=0;double sweepIntensity=0,streetZ=-34,nextSweep=3,sweepDuration=3.2,sweepIntervalMin=7,sweepIntervalJitter=7,sweepHalfTravel=26,sweepSeparation=1.5,sweepElapsed=0,lastChoreographyTime=0,lastDrawTime=0;int sweepDirection=0;bool choreographyEnabled=false,haveChoreographyTime=false,hasKeySpot=false,hasSpillSpot=false,hasRimSpot=false,hasSweepSpot=false;
    std::vector<std::unique_ptr<Mesh>> meshes;std::map<std::string,std::unique_ptr<CharacterShader>> shaders;std::map<std::string,std::map<std::string,std::vector<double>>> defaults;std::map<std::string,std::unique_ptr<Texture>> textureCache;
    explicit Impl(std::filesystem::path path):root(std::move(path)){load();}
    ~Impl(){meshes.clear();shaders.clear();textureCache.clear();if(reflectionTexture)glDeleteTextures(1,&reflectionTexture);if(reflectionFbo)glDeleteFramebuffers(1,&reflectionFbo);if(reflectionDepth)glDeleteRenderbuffers(1,&reflectionDepth);if(keyShadowTexture)glDeleteTextures(1,&keyShadowTexture);if(keyShadowFbo)glDeleteFramebuffers(1,&keyShadowFbo);}
    std::map<std::string,std::vector<double>> loadUniforms(const std::string& key){
        std::ifstream in(root/(key+".uniforms"));if(!in)throw std::runtime_error("Missing source lobby material defaults: "+key);std::map<std::string,std::vector<double>> result;std::string name;
        while(in>>name){std::size_t count=0;if(!(in>>count)||name.empty()||count==0||count>65536)throw std::runtime_error("Invalid source lobby material defaults: "+key);std::vector<double> values(count);for(auto& x:values)if(!(in>>x)||!std::isfinite(x))throw std::runtime_error("Invalid source lobby material defaults: "+key);if(!result.emplace(name,std::move(values)).second)throw std::runtime_error("Duplicate source lobby uniform: "+name);}
        if(!in.eof())throw std::runtime_error("Malformed source lobby material defaults: "+key);return result;
    }
    GLuint loadTexture(const std::string& file,const JsonValue& desc){
        if(auto found=textureCache.find(file);found!=textureCache.end())return found->second->id;
        TextureInfo info;info.file=file;info.width=integer(required(desc,"width"),"texture.width",MaxDimension);info.height=integer(required(desc,"height"),"texture.height",MaxDimension);info.layers=integer(required(desc,"layers"),"texture.layers",2048);info.bits=integer(required(desc,"bits"),"texture.bits",16);info.srgb=boolean(required(desc,"srgb"),"texture.srgb");
        const auto pixels=texturePixels(root/file,info);auto texture=std::make_unique<Texture>();texture->target=info.layers>1?GL_TEXTURE_2D_ARRAY:GL_TEXTURE_2D;
        glGenTextures(1,&texture->id);glBindTexture(texture->target,texture->id);const GLenum internal=info.srgb?GL_SRGB8_ALPHA8:GL_RGBA8;
        glTexParameteri(texture->target,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(texture->target,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(texture->target,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(texture->target,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        if(texture->target==GL_TEXTURE_2D_ARRAY)glTexImage3D(texture->target,0,internal,info.width,info.height,info.layers,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());else glTexImage2D(texture->target,0,internal,info.width,info.height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        glGenerateMipmap(texture->target);const auto id=texture->id;textureCache.emplace(file,std::move(texture));return id;
    }
    void updateDrips(double time){
        if(!dripMesh)return;
        if(!std::isfinite(time))throw std::invalid_argument("Invalid source lobby time");
        // Native lobby frames cap dt at 0.1; Draw receives the matching absolute clock.
        const double dt=haveDripTime?std::clamp(time-lastDripTime,0.0,0.1):0.0;lastDripTime=time;haveDripTime=true;
        const auto hash1=[](double value){const double s=std::sin(value*127.1+311.7)*43758.5453;return s-std::floor(s);};
        nextDrip-=dt;
        if(nextDrip<=0){
            nextDrip=dripIntervalMin+hash1(std::floor(time*10))*dripIntervalJitter;
            const auto source=std::min(dripSources.size()-1,static_cast<std::size_t>(std::floor(hash1(std::floor(time*3))*dripSources.size())));
            const auto drip=std::find_if(drips.begin(),drips.end(),[](const Drip& item){return !item.active;});
            if(drip!=drips.end())*drip={dripSources[source],0,true};
        }
        std::vector<float> data;data.reserve(dripMesh->instanceCapacity*20);
        for(auto& drip:drips){
            if(!drip.active)continue;
            drip.velocity+=dripGravity*dt;drip.position.y-=drip.velocity*dt;
            if(drip.position.y<=0){ripples[nextRipple++%ripples.size()]={static_cast<float>(drip.position.x),static_cast<float>(drip.position.z),static_cast<float>(time),1.0f};drip.active=false;continue;}
            const auto matrix=glm::translate(glm::dmat4(1),drip.position)*glm::scale(glm::dmat4(1),glm::dvec3(1,1+drip.velocity*.25,1));
            for(int c=0;c<4;c++)for(int r=0;r<4;r++)data.push_back(static_cast<float>(matrix[c][r]));
            data.insert(data.end(),{1.0f,1.0f,1.0f,0.0f});
        }
        dripMesh->activeInstances=data.size()/20;
        if(data.empty())return;
        glBindBuffer(GL_ARRAY_BUFFER,dripMesh->instanceBuffer);glBufferSubData(GL_ARRAY_BUFFER,0,static_cast<GLsizeiptr>(data.size()*sizeof(float)),data.data());
    }
    void uploadInstances(Mesh& mesh){
        if(!mesh.instanceBuffer)return;std::vector<float> data;data.reserve(mesh.instances.size()*20);
        for(const auto& instance:mesh.instances){for(int c=0;c<4;c++)for(int r=0;r<4;r++)data.push_back(static_cast<float>(instance.matrix[c][r]));for(int c=0;c<3;c++)data.push_back(static_cast<float>(instance.color[c]));data.push_back(instance.phase);}
        if(!data.empty()){glBindBuffer(GL_ARRAY_BUFFER,mesh.instanceBuffer);glBufferSubData(GL_ARRAY_BUFFER,0,static_cast<GLsizeiptr>(data.size()*sizeof(float)),data.data());}
    }
    void updateChoreography(double time){
        if(!choreographyEnabled)return;if(!std::isfinite(time))throw std::invalid_argument("Invalid source lobby time");
        const double dt=haveChoreographyTime?std::clamp(time-lastChoreographyTime,0.0,0.1):0.0;lastChoreographyTime=time;haveChoreographyTime=true;
        sweepIntensity=0;
        std::size_t index=0;
        for(const auto& string:bulbStrings)for(std::size_t k=0;k<string.count;k++,index++){
            const double f=(double(k)+0.5)/double(string.count);glm::dvec3 position=string.a+(string.b-string.a)*f;position.y-=string.sag*4*f*(1-f);
            const double sway=std::sin(time*1.3+string.a.z*0.7)*0.035*std::sin(glm::pi<double>()*f);position.z+=sway;position.y-=0.045+std::abs(sway)*0.2;
            if(bulbMesh&&index<bulbMesh->instances.size())bulbMesh->instances[index].matrix=glm::translate(glm::dmat4(1),position);
            if(glowMesh&&index<bulbGlowCount&&index<glowMesh->instances.size()){
                auto& instance=glowMesh->instances[index];const glm::dvec3 scale{glm::length(glm::dvec3(instance.matrix[0])),glm::length(glm::dvec3(instance.matrix[1])),glm::length(glm::dvec3(instance.matrix[2]))};
                instance.matrix=glm::translate(glm::dmat4(1),position)*glm::scale(glm::dmat4(1),scale);
            }
        }
        if(bulbMesh)uploadInstances(*bulbMesh);
        if(glowMesh){for(std::size_t i=0;i<headlightIndices.size();i++)glowMesh->instances[headlightIndices[i]]=headlightInitial[i];}
        const auto hash1=[](double value){const double s=std::sin(value*127.1+311.7)*43758.5453;return s-std::floor(s);};
        nextSweep-=dt;if(!sweepDirection&&nextSweep<=0)sweepDirection=hash1(std::floor(time))>0.5?1:-1;
        if(sweepDirection){
            sweepElapsed+=dt;const double x=sweepDirection*(-sweepHalfTravel+2*sweepHalfTravel*sweepElapsed/sweepDuration),z=streetZ-(sweepDirection>0?4.5:8.5);
            const double visibility=std::max(0.0,1.0-std::abs(x)/sweepHalfTravel),fade=0.9*std::min(1.0,sweepElapsed*3)*std::min(1.0,(sweepDuration-sweepElapsed)*3);
            sweepPosition={x,0.8,z};sweepTarget={x*0.3+sweepDirection*4.0,1.0,-27.0};sweepIntensity=260*visibility*visibility*fade;
            for(std::size_t i=0;i<headlightIndices.size();i++){
                auto& instance=glowMesh->instances[headlightIndices[i]];const double offset=i? -sweepDirection*sweepSeparation:0.0;
                instance.matrix=glm::translate(glm::dmat4(1),glm::dvec3(x+offset,0.7,z))*glm::scale(glm::dmat4(1),glm::dvec3(1.8));instance.color=headlightColor*(fade*1.2);
            }
            if(sweepElapsed>=sweepDuration){for(const auto indexValue:headlightIndices)glowMesh->instances[indexValue].color=glm::dvec3(0);sweepDirection=0;sweepElapsed=0;nextSweep=sweepIntervalMin+hash1(std::floor(time*2))*sweepIntervalJitter;}
        }
        if(glowMesh)uploadInstances(*glowMesh);
        if(mothMesh&&!mothMesh->instances.empty()){
            const double a=time*2.3;const glm::dvec3 position{mothCenter.x+std::sin(a*1.7)*0.22+std::sin(time*9.1)*0.03,mothCenter.y+std::sin(a*1.3+1)*0.12+std::sin(time*11.3)*0.02,mothCenter.z+std::cos(a*1.1)*0.25};
            const double yaw=a*1.7+glm::pi<double>()/2,roll=0.3*std::sin(time*5),wing=0.6+0.4*std::abs(std::sin(time*57));
            mothMesh->instances[0].matrix=glm::translate(glm::dmat4(1),position)*glm::rotate(glm::dmat4(1),roll,glm::dvec3(0,0,1))*glm::rotate(glm::dmat4(1),yaw,glm::dvec3(0,1,0))*glm::scale(glm::dmat4(1),glm::dvec3(wing,1,1));
        }
    }
    static Spot parseSpot(const JsonValue& value,const char* label){
        Spot result;result.position=vec3(required(value,"position"),label);result.target=vec3(required(value,"target"),label);result.color=vec3(required(value,"color"),label);result.intensity=number(required(value,"intensity"),label);result.distance=number(required(value,"distance"),label);result.angle=number(required(value,"angle"),label);result.penumbra=number(required(value,"penumbra"),label);result.decay=number(required(value,"decay"),label);
        if(const auto* visible=value.get("visible"))result.visible=boolean(*visible,label);
        if(const auto* cookie=value.get("cookieKey")){result.cookieKey=string(*cookie,label);const auto& matrix=array(required(value,"cookieMatrix"),"spot.cookieMatrix");if(matrix.size()!=16)throw std::runtime_error("Invalid source lobby cookie matrix");std::size_t i=0;for(int c=0;c<4;c++)for(int r=0;r<4;r++)result.cookieMatrix[c][r]=number(matrix[i++],"spot.cookieMatrix");}
        if(const auto* casts=value.get("castShadow"))result.castsShadow=boolean(*casts,label);
        if(const auto* shadow=value.get("shadow")){result.shadowSize=integer(required(*shadow,"size"),"spot.shadow.size",MaxDimension);result.shadowAspect=number(required(*shadow,"aspect"),"spot.shadow.aspect");result.shadowNear=number(required(*shadow,"near"),"spot.shadow.near");result.shadowFar=number(required(*shadow,"far"),"spot.shadow.far");result.shadowBias=number(required(*shadow,"bias"),"spot.shadow.bias");result.shadowNormalBias=number(required(*shadow,"normalBias"),"spot.shadow.normalBias");result.shadowRadius=number(required(*shadow,"radius"),"spot.shadow.radius");}
        if(result.intensity<0||result.distance<0||result.angle<0||result.angle>=glm::pi<double>()/2||result.penumbra<0||result.penumbra>1||result.decay<0||glm::length(result.position-result.target)<1e-6||(result.castsShadow&&(!result.shadowSize||result.shadowAspect<=0||result.shadowNear<=0||result.shadowFar<=result.shadowNear||result.shadowRadius<0)))throw std::runtime_error(std::string("Invalid source lobby spotlight: ")+label);return result;
    }
    static Point parsePoint(const JsonValue& value,const char* label){
        Point result;result.position=vec3(required(value,"position"),label);result.color=vec3(required(value,"color"),label);result.intensity=number(required(value,"intensity"),label);result.distance=number(required(value,"distance"),label);result.decay=number(required(value,"decay"),label);if(const auto* visible=value.get("visible"))result.visible=boolean(*visible,label);
        if(result.intensity<0||result.distance<0||result.decay<0)throw std::runtime_error(std::string("Invalid source lobby point light: ")+label);return result;
    }
    static double distanceAttenuation(double distance,double cutoff,double decay){double attenuation=1.0/std::max(std::pow(distance,decay),0.01);if(cutoff>0){const double value=std::max(0.0,1.0-std::pow(distance/cutoff,4));attenuation*=value*value;}return attenuation;}
    static double spotAttenuation(const Spot& spot,glm::dvec3 position,glm::dvec3 lightPosition,glm::dvec3 target){
        const glm::dvec3 toLight=lightPosition-position;const double distance=glm::length(toLight);if(distance<1e-10)return 0;const double outer=std::cos(spot.angle),inner=std::cos(spot.angle*(1.0-spot.penumbra));const double cosine=glm::dot(toLight/distance,glm::normalize(lightPosition-target));const double t=std::clamp((cosine-outer)/std::max(inner-outer,1e-9),0.0,1.0);return t*t*(3.0-2.0*t)*distanceAttenuation(distance,spot.distance,spot.decay);
    }
    CharacterLightState lightingAt(glm::dvec3 position)const{
        auto result=lights;glm::dvec3 sum(0),directionMoment(0);
        const auto add=[&](glm::dvec3 direction,glm::dvec3 color){const double weight=glm::dot(color,glm::dvec3(0.2126,0.7152,0.0722));sum+=color;directionMoment+=direction*weight;};
        const auto addSpot=[&](const Spot& spot,glm::dvec3 lightPosition,glm::dvec3 target,double intensity){if(!spot.visible||intensity<=0)return;const glm::dvec3 toLight=lightPosition-position;const double distance=glm::length(toLight);if(distance<1e-10)return;const double attenuation=spotAttenuation(spot,position,lightPosition,target);add(toLight/distance,spot.color*(intensity*attenuation));};
        const auto addPoint=[&](const Point& light,glm::dvec3 color,double intensity){if(!light.visible||intensity<=0)return;const glm::dvec3 toLight=light.position-position;const double distance=glm::length(toLight);if(distance<1e-10)return;add(toLight/distance,color*(intensity*distanceAttenuation(distance,light.distance,light.decay)));};
        if(hasKeySpot)addSpot(keySpot,keySpot.position,keySpot.target,keySpot.intensity);
        if(hasSpillSpot)addSpot(spillSpot,spillSpot.position,spillSpot.target,spillSpot.intensity);
        if(hasRimSpot)addSpot(rimSpot,rimSpot.position,rimSpot.target,rimSpot.intensity);
        if(hasSweepSpot)addSpot(sweepSpot,sweepPosition,sweepTarget,sweepIntensity);
        const auto lookup=[&](const char* name)->const Point*{const auto found=pointLights.find(name);return found==pointLights.end()?nullptr:&found->second;};
        if(const auto* p=lookup("neonA")){const double shimmer=1.0+0.02*std::sin(lastDrawTime*120.0)*std::sin(lastDrawTime*7.3);const bool burst=std::sin(lastDrawTime*.37)*std::sin(lastDrawTime*.91+1.3)>.55;const double h=std::sin(std::floor(lastDrawTime*22.0)*127.1+311.7)*43758.5453;const double buzz=burst?(h-std::floor(h)>.45?1.0:.08):1.0;const glm::dvec3 c=lastTeamA/std::max({lastTeamA.r,lastTeamA.g,lastTeamA.b,1e-3});addPoint(*p,c,16*shimmer*(.93+.07*buzz));}
        if(const auto* p=lookup("neonB")){const glm::dvec3 c=lastTeamB/std::max({lastTeamB.r,lastTeamB.g,lastTeamB.b,1e-3});addPoint(*p,c,26*(1.0+.03*std::sin(lastDrawTime*1.7)));}
        for(const char* name:{"bulbs","vend","street"})if(const auto* p=lookup(name))addPoint(*p,p->color,p->intensity);
        if(glm::length(directionMoment)>1e-10){result.sunDirection=glm::vec3(glm::normalize(directionMoment));result.sunColor=glm::vec3(sum);}return result;
    }
    CharacterShadowState keyShadowState(bool depthOnly=false)const{
        CharacterShadowState result;result.depthOnly=depthOnly;result.size=keySpot.shadowSize?static_cast<int>(keySpot.shadowSize):2048;result.matrix=keyShadowProjection*keyShadowView;
        result.intensity=keyShadowEnabled&&keyShadowReady?1.0:0.0;result.bias=keySpot.shadowBias;result.normalBias=keySpot.shadowNormalBias;result.radius=keySpot.shadowRadius;
        if(!depthOnly&&keyShadowEnabled&&keyShadowReady)result.texture=keyShadowTexture;return result;
    }
    void ensureKeyShadowTarget(){
        if(!keySpot.castsShadow||!keySpot.shadowSize)throw std::logic_error("Source lobby key shadow is not configured");
        if(!keyShadowFbo){glGenFramebuffers(1,&keyShadowFbo);glGenTextures(1,&keyShadowTexture);}
        if(keyShadowSize!=keySpot.shadowSize){glBindTexture(GL_TEXTURE_2D,keyShadowTexture);glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT32F,static_cast<GLsizei>(keySpot.shadowSize),static_cast<GLsizei>(keySpot.shadowSize),0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);const GLfloat border[]={1,1,1,1};glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_FUNC,GL_LEQUAL);keyShadowSize=keySpot.shadowSize;}
        glBindFramebuffer(GL_FRAMEBUFFER,keyShadowFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,keyShadowTexture,0);glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete source lobby spotlight shadow framebuffer");
    }
    int drawShadowMeshes(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,double time,glm::dvec3 teamA,glm::dvec3 teamB){
        int draws=0;const std::vector<double> a{teamA.x,teamA.y,teamA.z},b{teamB.x,teamB.y,teamB.z};
        for(const auto& pointer:meshes){auto& mesh=*pointer;if(!mesh.casts||(mesh.dynamicDrips&&!mesh.activeInstances))continue;auto& shader=*shaders.at(mesh.shaderKey);shader.use();for(const auto& [name,values]:defaults.at(mesh.shaderKey))shader.uniform(name,values);shader.uniform("iwDepthOnly",1.0);shader.uniform("uTime",time);
            for(const auto& binding:mesh.textures){if(!binding.dynamic){glActiveTexture(GL_TEXTURE0+binding.unit);const auto& texture=*textureCache.at(binding.file);glBindTexture(texture.target,texture.id);glTexParameteri(texture.target,GL_TEXTURE_MIN_FILTER,static_cast<GLint>(filter(binding.minFilter)));glTexParameteri(texture.target,GL_TEXTURE_MAG_FILTER,static_cast<GLint>(filter(binding.magFilter)));glTexParameteri(texture.target,GL_TEXTURE_WRAP_S,static_cast<GLint>(wrap(binding.wrapS)));glTexParameteri(texture.target,GL_TEXTURE_WRAP_T,static_cast<GLint>(wrap(binding.wrapT)));}shader.uniform(binding.uniform,binding.unit);}
            const auto setColor=[&](const char* name,const std::vector<double>& values){const auto found=defaults.at(mesh.shaderKey).find(name);if(found!=defaults.at(mesh.shaderKey).end()&&found->second.size()==values.size())shader.uniform(name,values);};setColor("uTeamA",a);setColor("uTeamB",b);setColor("uColA",a);setColor("uColB",b);setColor("uCol",mesh.name=="lobbySet:halo1"?b:a);shader.lights(lights,view);shader.matrices(glm::dmat4(1),view,projection,eye);
            glDisable(GL_BLEND);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);if(mesh.side==2)glDisable(GL_CULL_FACE);else{glEnable(GL_CULL_FACE);glCullFace(mesh.side==1?GL_FRONT:GL_BACK);}glBindVertexArray(mesh.vao);
            if(mesh.instanced){glDrawElementsInstanced(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr,static_cast<GLsizei>(mesh.activeInstances));++draws;}else for(const auto& instance:mesh.instances){const auto diffuse=defaults.at(mesh.shaderKey).find("diffuse");if(diffuse!=defaults.at(mesh.shaderKey).end()&&diffuse->second.size()==3){auto color=diffuse->second;for(int c=0;c<3;c++)color[c]*=instance.color[c];shader.uniform("diffuse",color);}const int phase=shader.attribute("aPhase");if(phase>=0)glVertexAttrib1f(phase,instance.phase);shader.matrices(instance.matrix,view,projection,eye);glDrawElements(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr);++draws;}
        }
        return draws;
    }
    void ensureReflectionTarget(int width,int height){
        if(!reflectionFbo){glGenFramebuffers(1,&reflectionFbo);glGenTextures(1,&reflectionTexture);glGenRenderbuffers(1,&reflectionDepth);glBindTexture(GL_TEXTURE_2D,reflectionTexture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
        glBindFramebuffer(GL_FRAMEBUFFER,reflectionFbo);
        if(width!=reflectionWidth||height!=reflectionHeight){glBindTexture(GL_TEXTURE_2D,reflectionTexture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,width,height,0,GL_RGBA,GL_HALF_FLOAT,nullptr);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,reflectionTexture,0);glBindRenderbuffer(GL_RENDERBUFFER,reflectionDepth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,reflectionDepth);reflectionWidth=width;reflectionHeight=height;reflectionResolution={double(width),double(height)};}
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete source lobby planar reflection framebuffer");
    }
    glm::dmat4 reflectedViewProjection(const glm::dmat4& view,const glm::dmat4& projection,glm::dmat4& reflectedView,glm::dvec3& reflectedEye){
        const auto cameraWorld=glm::inverse(view);glm::dvec3 forward=-glm::dvec3(cameraWorld[2]),up=glm::dvec3(cameraWorld[1]);reflectedEye=glm::dvec3(cameraWorld[3]);reflectedEye.y=-reflectedEye.y;forward.y=-forward.y;up.y=-up.y;reflectedView=glm::lookAt(reflectedEye,reflectedEye+forward,up);
        glm::dmat4 clipped=projection;glm::dvec4 clip=glm::transpose(glm::inverse(reflectedView))*glm::dvec4(0,1,0,0);const auto sign=[](double value){return value>0?1.0:value<0?-1.0:0.0;};
        const glm::dvec4 q{(sign(clip.x)+projection[2][0])/projection[0][0],(sign(clip.y)+projection[2][1])/projection[1][1],-1.0,(1.0+projection[2][2])/projection[3][2]};const double denominator=glm::dot(clip,q);if(!std::isfinite(denominator)||std::abs(denominator)<1e-10)throw std::runtime_error("Invalid source lobby reflection clip plane");clip*=2.0/denominator;
        clipped[0][2]=clip.x;clipped[1][2]=clip.y;clipped[2][2]=clip.z+1.0-0.003;clipped[3][2]=clip.w;
        glm::dmat4 bias(1);bias[0][0]=bias[1][1]=bias[2][2]=0.5;bias[3]={0.5,0.5,0.5,1.0};reflectionMatrix=bias*clipped*reflectedView;return clipped;
    }
    void load(){
        GlState state;
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        const auto index=data::JsonParser(readText(root/"scene-index.json")).parse();if(integer(required(index,"schema"),"scene-index.schema")!=1)throw std::runtime_error("Unsupported source lobby scene schema");
        const auto layoutData=data::JsonParser(readText(root/"layout.json")).parse();layout=parseLayout(layoutData);
        const auto& dripEmitter=required(layoutData,"dripEmitter");const unsigned dripCapacity=integer(required(dripEmitter,"capacity"),"dripEmitter.capacity",64);
        if(!dripCapacity)throw std::runtime_error("Invalid source lobby drip capacity");
        nextDrip=number(required(dripEmitter,"initialDelay"),"dripEmitter.initialDelay");dripGravity=number(required(dripEmitter,"gravity"),"dripEmitter.gravity");dripIntervalMin=number(required(dripEmitter,"intervalMin"),"dripEmitter.intervalMin");dripIntervalJitter=number(required(dripEmitter,"intervalJitter"),"dripEmitter.intervalJitter");
        if(nextDrip<0||dripGravity<=0||dripIntervalMin<0||dripIntervalJitter<0)throw std::runtime_error("Invalid source lobby drip settings");
        const auto& sourceRows=array(required(dripEmitter,"sources"),"dripEmitter.sources");if(sourceRows.empty()||sourceRows.size()>16)throw std::runtime_error("Invalid source lobby drip positions");
        for(const auto& source:sourceRows)dripSources.push_back(vec3(source,"dripEmitter.source"));drips.resize(dripCapacity);
        if(const auto* motions=layoutData.get("motions")){
            const auto& bulb=required(*motions,"bulbSway");const auto& strings=array(required(bulb,"strings"),"motions.bulbSway.strings");if(strings.empty()||strings.size()>32)throw std::runtime_error("Invalid source lobby bulb strings");
            std::size_t expectedFirst=0;for(const auto& row:strings){BulbString stringRow;stringRow.a=vec3(required(row,"a"),"bulbString.a");stringRow.b=vec3(required(row,"b"),"bulbString.b");stringRow.sag=number(required(row,"sag"),"bulbString.sag");stringRow.count=integer(required(row,"count"),"bulbString.count",128);stringRow.first=integer(required(row,"first"),"bulbString.first",100000);if(!stringRow.count||stringRow.first!=expectedFirst||stringRow.sag<0)throw std::runtime_error("Invalid source lobby bulb string ordering");expectedFirst+=stringRow.count;bulbStrings.push_back(stringRow);}
            bulbCount=integer(required(bulb,"count"),"motions.bulbSway.count",100000);bulbGlowCount=integer(required(bulb,"glowCount"),"motions.bulbSway.glowCount",100000);if(bulbCount!=expectedFirst||bulbGlowCount!=bulbCount)throw std::runtime_error("Source lobby bulb motion count mismatch");
            const auto& headlight=array(required(bulb,"headlightGlows"),"motions.bulbSway.headlightGlows");if(headlight.size()!=2)throw std::runtime_error("Invalid source lobby headlight glow indices");for(std::size_t i=0;i<2;i++)headlightIndices[i]=integer(headlight[i],"headlightGlow",100000);headlightColor=vec3(required(bulb,"headlightColor"),"motions.bulbSway.headlightColor");streetZ=number(required(bulb,"streetZ"),"motions.bulbSway.streetZ");
            nextSweep=number(required(bulb,"initialDelay"),"sweep.initialDelay");sweepDuration=number(required(bulb,"duration"),"sweep.duration");sweepIntervalMin=number(required(bulb,"intervalMin"),"sweep.intervalMin");sweepIntervalJitter=number(required(bulb,"intervalJitter"),"sweep.intervalJitter");sweepHalfTravel=number(required(bulb,"halfTravel"),"sweep.halfTravel");sweepSeparation=number(required(bulb,"separation"),"sweep.separation");if(nextSweep<0||sweepDuration<=0||sweepIntervalMin<0||sweepIntervalJitter<0||sweepHalfTravel<=0||sweepSeparation<0)throw std::runtime_error("Invalid source lobby headlight sweep settings");
            const auto& moth=required(*motions,"moth");mothCenter=vec3(required(moth,"center"),"motions.moth.center");choreographyEnabled=true;
        }
        const auto& textureCatalog=required(index,"textures");const auto& catalog=object(textureCatalog,"textures");
        const auto& textureRows=array(required(index,"textureBindings"),"textureBindings");const auto& meshRows=array(required(index,"meshes"),"meshes");
        if(meshRows.empty()||meshRows.size()>512||textureRows.size()!=meshRows.size())throw std::runtime_error("Invalid source lobby scene index");
        if(hasSpillSpot){if(spillSpot.cookieKey.empty()||!hasRimSpot||!hasSweepSpot||pointLights.size()!=5)throw std::runtime_error("Incomplete source lobby local-light export");for(const char* name:{"neonA","neonB","bulbs","vend","street"})if(!pointLights.count(name))throw std::runtime_error(std::string("Missing source lobby point light: ")+name);const auto found=catalog.find(spillSpot.cookieKey);if(found==catalog.end())throw std::runtime_error("Missing source lobby spill cookie descriptor");spillCookieFile=string(required(found->second,"file"),"spillCookie.file");(void)loadTexture(spillCookieFile,found->second);}
        const auto lighting=required(index,"lighting");lights.ambient=glm::vec3(vec3(required(lighting,"ambient"),"ambient"));lights.sunDirection=glm::vec3(vec3(required(lighting,"sunDirection"),"sunDirection"));lights.sunColor=glm::vec3(vec3(required(lighting,"sunColor"),"sunColor"));lights.skyDirection=glm::vec3(vec3(required(lighting,"skyDirection"),"skyDirection"));lights.skyColor=glm::vec3(vec3(required(lighting,"skyColor"),"skyColor"));lights.groundColor=glm::vec3(vec3(required(lighting,"groundColor"),"groundColor"));lights.environmentIntensity=static_cast<float>(number(required(lighting,"environmentIntensity"),"environmentIntensity"));
        if(const auto* sourceKey=lighting.get("keySpot")){keySpot=parseSpot(*sourceKey,"keySpot");hasKeySpot=true;lights.sunColor=glm::vec3(0);if(keySpot.castsShadow){keyShadowView=glm::lookAt(keySpot.position,keySpot.target,glm::dvec3(0,1,0));keyShadowProjection=glm::perspective(2.0*keySpot.angle,keySpot.shadowAspect,keySpot.shadowNear,keySpot.shadowFar);}}
        if(const auto* sourceSpill=lighting.get("spillSpot")){spillSpot=parseSpot(*sourceSpill,"spillSpot");hasSpillSpot=true;}
        if(const auto* sourceRim=lighting.get("rimSpot")){rimSpot=parseSpot(*sourceRim,"rimSpot");hasRimSpot=true;}
        if(const auto* sourceSweep=lighting.get("sweep")){sweepSpot=parseSpot(*sourceSweep,"sweep");hasSweepSpot=true;sweepPosition=sweepSpot.position;sweepTarget=sweepSpot.target;sweepIntensity=sweepSpot.intensity;}
        if(const auto* sourcePoints=lighting.get("points"))for(const auto& [name,value]:object(*sourcePoints,"lighting.points"))if(!pointLights.emplace(name,parsePoint(value,name.c_str())).second)throw std::runtime_error("Duplicate source lobby point light");
        if(glm::length(lights.sunDirection)<1e-4||glm::length(lights.skyDirection)<1e-4||lights.environmentIntensity<0)throw std::runtime_error("Invalid source lobby lighting");lights.sunDirection=glm::normalize(lights.sunDirection);lights.skyDirection=glm::normalize(lights.skyDirection);
        const auto pmremObject=required(index,"pmrem");environment_detail::TextureDescriptor pmremDescriptor{integer(required(pmremObject,"width"),"pmrem.width",MaxDimension),integer(required(pmremObject,"height"),"pmrem.height",MaxDimension),integer(required(pmremObject,"bits"),"pmrem.bits",16)};
        if(pmremDescriptor.width!=768||pmremDescriptor.height!=1024||pmremDescriptor.bits!=16)throw std::runtime_error("Unsupported source lobby PMREM dimensions");
        const auto pmremPixels=environment_detail::readTextureBlob(root/string(required(pmremObject,"file"),"pmrem.file"),pmremDescriptor);pmrem=std::make_unique<Texture>();glGenTextures(1,&pmrem->id);glBindTexture(GL_TEXTURE_2D,pmrem->id);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,768,1024,0,GL_RGBA,GL_HALF_FLOAT,pmremPixels.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);lights.environmentTexture=pmrem->id;
        dfg=std::make_unique<Texture>();glGenTextures(1,&dfg->id);glBindTexture(GL_TEXTURE_2D,dfg->id);glTexImage2D(GL_TEXTURE_2D,0,GL_RG16F,16,16,0,GL_RG,GL_HALF_FLOAT,CharacterDfg.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        Reader reader;reader.bytes=decompress(root/"scene.bin.z","TSLBZ001","TSLBG001");reader.at=8;const unsigned meshCount=reader.count(512,"mesh count");if(meshCount!=meshRows.size())throw std::runtime_error("Source lobby scene index mesh count mismatch");
        for(unsigned m=0;m<meshCount;m++){
            const auto& row=meshRows[m];const auto& bindRow=textureRows[m];auto mesh=std::make_unique<Mesh>();mesh->name=reader.str();mesh->shaderKey=reader.str();
            if(mesh->name!=string(required(row,"name"),"mesh.name")||mesh->shaderKey!=string(required(row,"shader"),"mesh.shader")||mesh->name!=string(required(bindRow,"name"),"binding.name"))throw std::runtime_error("Source lobby scene/index order mismatch");
            if(mesh->shaderKey.size()!=20||mesh->shaderKey.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid source lobby shader key");
            if(!shaders.count(mesh->shaderKey)){
                const auto vertex=root/(mesh->shaderKey+".vert"),fragment=root/(mesh->shaderKey+".frag");std::unique_ptr<CharacterShader> shader;
                try{shader=std::make_unique<CharacterShader>(CharacterShader::readSource(vertex),CharacterShader::readSource(fragment),true,true,false);}
                catch(const std::exception& error){throw std::runtime_error("Source lobby mesh "+mesh->name+" shader "+mesh->shaderKey+" ("+vertex.string()+", "+fragment.string()+"): "+error.what());}
                defaults.emplace(mesh->shaderKey,loadUniforms(mesh->shaderKey));shaders.emplace(mesh->shaderKey,std::move(shader));
            }
            auto& shader=*shaders.at(mesh->shaderKey);glGenVertexArrays(1,&mesh->vao);glBindVertexArray(mesh->vao);
            const unsigned attrCount=reader.count(32,"attribute count");unsigned vertexCount=0;std::set<std::string> attributeNames;
            for(unsigned a=0;a<attrCount;a++){
                const auto name=reader.str();const unsigned components=reader.count(4,"attribute components"),count=reader.count(10000000,"vertex count");if(!components||count==0||!attributeNames.insert(name).second)throw std::runtime_error("Invalid source lobby attribute");if(name=="position")vertexCount=count;
                std::vector<float> values(std::size_t(components)*count);for(auto& value:values)value=reader.f();const int location=shader.attribute(name);if(location<0)continue;
                GLuint buffer=0;glGenBuffers(1,&buffer);mesh->buffers.push_back(buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,values.size()*sizeof(float),values.data(),GL_STATIC_DRAW);glEnableVertexAttribArray(static_cast<GLuint>(location));glVertexAttribPointer(static_cast<GLuint>(location),static_cast<GLint>(components),GL_FLOAT,GL_FALSE,0,nullptr);
            }
            if(!vertexCount)throw std::runtime_error("Source lobby mesh has no position attribute");const unsigned indices=reader.count(30000000,"index count");if(!indices||indices%3)throw std::runtime_error("Invalid source lobby triangle index count");std::vector<unsigned> indexData(indices);for(auto& indexValue:indexData){indexValue=reader.u32();if(indexValue>=vertexCount)throw std::runtime_error("Source lobby vertex index out of range");}
            glGenBuffers(1,&mesh->index);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,mesh->index);glBufferData(GL_ELEMENT_ARRAY_BUFFER,indexData.size()*sizeof(unsigned),indexData.data(),GL_STATIC_DRAW);mesh->count=static_cast<GLsizei>(indices);
            const unsigned instances=reader.count(100000,"instance count");if(instances!=integer(required(row,"instances"),"mesh.instances",100000))throw std::runtime_error("Source lobby instance count mismatch");mesh->activeInstances=instances;mesh->instances.reserve(instances);
            for(unsigned i=0;i<instances;i++){Instance instance;for(int c=0;c<4;c++)for(int r=0;r<4;r++)instance.matrix[c][r]=reader.f();for(int c=0;c<3;c++)instance.color[c]=reader.f();instance.phase=reader.f();mesh->instances.push_back(instance);}
            mesh->transparent=reader.f()!=0;mesh->depthWrite=reader.f()!=0;mesh->depthTest=reader.f()!=0;mesh->side=static_cast<unsigned>(reader.f());mesh->blending=static_cast<unsigned>(reader.f());mesh->casts=reader.f()!=0;
            mesh->depthFunc=static_cast<unsigned>(reader.f());mesh->srcRgb=static_cast<unsigned>(reader.f());mesh->dstRgb=static_cast<unsigned>(reader.f());mesh->srcAlpha=static_cast<unsigned>(reader.f());mesh->dstAlpha=static_cast<unsigned>(reader.f());mesh->eqRgb=static_cast<unsigned>(reader.f());mesh->eqAlpha=static_cast<unsigned>(reader.f());const bool premultiplied=reader.f()!=0;(void)premultiplied;
            mesh->instanced=reader.u32()!=0;mesh->instanceCapacity=reader.count(100000,"instance capacity");const bool jsonInstanced=boolean(required(row,"instanced"),"mesh.instanced");const auto* dynamicMetadata=row.get("dynamicInstances");if(mesh->instanced&&!dynamicMetadata)throw std::runtime_error("Missing source lobby instance motion metadata");mesh->dynamicDrips=dynamicMetadata?boolean(*dynamicMetadata,"mesh.dynamicInstances"):false;
            if(mesh->instanced!=jsonInstanced||mesh->instanceCapacity!=integer(required(row,"instanceCapacity"),"mesh.instanceCapacity",100000)||mesh->instanceCapacity<instances||!mesh->instanceCapacity||mesh->dynamicDrips!=(mesh->instanced&&!instances))throw std::runtime_error("Source lobby instance metadata mismatch");
            if(mesh->dynamicDrips){if(!mesh->instanced||instances||mesh->name!="lobbySet:drips"||mesh->instanceCapacity!=drips.size()||dripMesh)throw std::runtime_error("Unsupported source lobby dynamic instance pool");dripMesh=mesh.get();}
            else if(!instances)throw std::runtime_error("Source lobby mesh has no instances");
            if(!mesh->instanced&&(instances!=1||mesh->instanceCapacity!=1))throw std::runtime_error("Invalid source lobby single-instance mesh");
            if(mesh->instanced){const int location=shader.attribute("instanceMatrix");if(location<0)throw std::runtime_error("Source lobby instanced shader lacks instanceMatrix");mesh->instanceBuffer=0;glGenBuffers(1,&mesh->instanceBuffer);mesh->buffers.push_back(mesh->instanceBuffer);glBindBuffer(GL_ARRAY_BUFFER,mesh->instanceBuffer);
                for(int c=0;c<4;c++){glEnableVertexAttribArray(static_cast<GLuint>(location+c));glVertexAttribPointer(static_cast<GLuint>(location+c),4,GL_FLOAT,GL_FALSE,20*sizeof(float),reinterpret_cast<void*>(static_cast<std::uintptr_t>(c*4*sizeof(float))));glVertexAttribDivisor(static_cast<GLuint>(location+c),1);}
                for(const auto& attribute:std::array<std::pair<const char*,std::pair<int,int>>,2>{{{"instanceColor",{3,16}},{"aPhase",{1,19}}}}){const int at=shader.attribute(attribute.first);if(at>=0){glEnableVertexAttribArray(static_cast<GLuint>(at));glVertexAttribPointer(static_cast<GLuint>(at),attribute.second.first,GL_FLOAT,GL_FALSE,20*sizeof(float),reinterpret_cast<void*>(static_cast<std::uintptr_t>(attribute.second.second*sizeof(float))));glVertexAttribDivisor(static_cast<GLuint>(at),1);}}
                std::vector<float> instanceData;instanceData.reserve(mesh->instances.size()*20);for(const auto& instance:mesh->instances){for(int c=0;c<4;c++)for(int r=0;r<4;r++)instanceData.push_back(static_cast<float>(instance.matrix[c][r]));for(int c=0;c<3;c++)instanceData.push_back(static_cast<float>(instance.color[c]));instanceData.push_back(instance.phase);}const bool changingInstances=mesh->dynamicDrips||mesh->name=="lobbySet:bulbs"||mesh->name=="lobbySet:glows";glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(mesh->instanceCapacity*20*sizeof(float)),nullptr,changingInstances?GL_DYNAMIC_DRAW:GL_STATIC_DRAW);if(!instanceData.empty())glBufferSubData(GL_ARRAY_BUFFER,0,static_cast<GLsizeiptr>(instanceData.size()*sizeof(float)),instanceData.data());
            }
            const auto& bindings=array(required(bindRow,"textures"),"mesh texture bindings");if(bindings.size()>8)throw std::runtime_error("Source lobby material exceeds reserved texture units");unsigned unit=0;
            for(const auto& binding:bindings){Binding item;item.uniform=string(required(binding,"uniform"),"texture.uniform");item.key=string(required(binding,"key"),"texture.key");item.dynamic=boolean(required(binding,"dynamic"),"texture.dynamic");
                if(unit==6)unit++;item.unit=unit++;
                if(item.dynamic){if(mesh->name!="lobbySet:ground"||item.uniform!="tRefl")throw std::runtime_error("Unsupported source lobby dynamic texture: "+item.key);}
                else{item.file=string(required(binding,"file"),"texture.file");const auto found=catalog.find(item.key);if(found==catalog.end())throw std::runtime_error("Missing source lobby texture descriptor: "+item.key);
                    const auto& desc=found->second;const auto file=string(required(desc,"file"),"texture.file");if(file!=item.file)throw std::runtime_error("Source lobby texture path mismatch: "+item.key);(void)loadTexture(file,desc);}
                // Texture settings are attached to each material binding, not to the pixel blob.
                if(const auto* sampler=binding.get("sampler")){item.wrapS=integer(required(*sampler,"wrapS"),"texture.wrapS",1002);item.wrapT=integer(required(*sampler,"wrapT"),"texture.wrapT",1002);item.minFilter=integer(required(*sampler,"minFilter"),"texture.minFilter",1008);item.magFilter=integer(required(*sampler,"magFilter"),"texture.magFilter",1006);item.mipmaps=boolean(required(*sampler,"generateMipmaps"),"texture.generateMipmaps");}
                mesh->textures.push_back(std::move(item));
            }
            if(mesh->side>2)throw std::runtime_error("Invalid source lobby material side");if(mesh->transparent&&mesh->blending==0)throw std::runtime_error("Transparent source lobby material has no blending mode");
            if(mesh->name=="lobbySet:drips")dripMesh=mesh.get();
            else if(mesh->name=="lobbySet:bulbs"){if(bulbMesh)throw std::runtime_error("Duplicate source lobby bulb mesh");bulbMesh=mesh.get();}
            else if(mesh->name=="lobbySet:glows"){if(glowMesh)throw std::runtime_error("Duplicate source lobby glow mesh");glowMesh=mesh.get();}
            else if(mesh->name=="lobbySet:moth"){if(mothMesh)throw std::runtime_error("Duplicate source lobby moth mesh");mothMesh=mesh.get();}
            meshes.push_back(std::move(mesh));
        }
        if(!dripMesh)throw std::runtime_error("Missing source lobby drip instance pool");if(choreographyEnabled){if(!bulbMesh||!glowMesh||!mothMesh||!bulbMesh->instanced||!glowMesh->instanced||bulbMesh->instances.size()!=bulbCount||glowMesh->instances.size()<bulbGlowCount||headlightIndices[0]>=glowMesh->instances.size()||headlightIndices[1]>=glowMesh->instances.size()||headlightIndices[0]==headlightIndices[1]||mothMesh->instanced||mothMesh->instances.size()!=1)throw std::runtime_error("Source lobby motion metadata/mesh mismatch");for(std::size_t i=0;i<2;i++)headlightInitial[i]=glowMesh->instances[headlightIndices[i]];}if(reader.at!=reader.bytes.size())throw std::runtime_error("Trailing source lobby geometry data");glBindVertexArray(0);
    }
    int draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,double time,glm::dvec3 teamA,glm::dvec3 teamB,const LobbyRenderer::ReflectionDraw& reflectionDraw,const LobbyRenderer::ShadowDraw& shadowDraw){
        GlState state;lastDrawTime=time;lastTeamA=teamA;lastTeamB=teamB;updateDrips(time);updateChoreography(time);
        if(!spillCookieFile.empty()){glActiveTexture(GL_TEXTURE9);glBindTexture(GL_TEXTURE_2D,textureCache.at(spillCookieFile)->id);}
        keyShadowReady=false;
        if(keyShadowEnabled&&hasKeySpot&&keySpot.castsShadow){GlState shadowState;ensureKeyShadowTarget();glViewport(0,0,static_cast<GLsizei>(keyShadowSize),static_cast<GLsizei>(keyShadowSize));glDisable(GL_SCISSOR_TEST);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDisable(GL_BLEND);glClearDepth(1.0);glClear(GL_DEPTH_BUFFER_BIT);drawShadowMeshes(keyShadowView,keyShadowProjection,keySpot.position,time,teamA,teamB);if(shadowDraw){const auto shadow=keyShadowState(true);GlState actorState;shadowDraw(keyShadowView,keyShadowProjection,keySpot.position,shadow);}keyShadowReady=true;}
        if(keyShadowReady){glActiveTexture(GL_TEXTURE0+10);glBindTexture(GL_TEXTURE_2D,keyShadowTexture);}
        const auto drawMeshes=[&](const glm::dmat4& drawView,const glm::dmat4& drawProjection,glm::dvec3 drawEye,bool reflectionPass){
            glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,dfg->id);int draws=0;
            for(bool transparent:{false,true})for(const auto& pointer:meshes){const auto& mesh=*pointer;
                if(mesh.transparent!=transparent||(mesh.dynamicDrips&&!mesh.activeInstances)||(reflectionPass&&(mesh.name=="lobbySet:ground"||mesh.name=="lobbySet:steam"||mesh.name=="lobbySet:curbs")))continue;auto& shader=*shaders.at(mesh.shaderKey);shader.use();
                for(const auto& [name,values]:defaults.at(mesh.shaderKey))shader.uniform(name,values);
                if(const auto rip=defaults.at(mesh.shaderKey).find("uRip");rip!=defaults.at(mesh.shaderKey).end()&&rip->second.size()==ripples.size()*4){
                    std::vector<double> values;values.reserve(ripples.size()*4);for(const auto& ripple:ripples)for(int c=0;c<4;c++)values.push_back(ripple[c]);shader.uniform("uRip",values);
                }
                const bool useReflection=!reflectionPass&&reflectionsEnabled&&reflectionReady;
                for(const auto& binding:mesh.textures){
                    if(binding.dynamic){if(useReflection){glActiveTexture(GL_TEXTURE0+binding.unit);glBindTexture(GL_TEXTURE_2D,reflectionTexture);}shader.uniform(binding.uniform,binding.unit);continue;}
                    glActiveTexture(GL_TEXTURE0+binding.unit);const auto& texture=*textureCache.at(binding.file);glBindTexture(texture.target,texture.id);glTexParameteri(texture.target,GL_TEXTURE_MIN_FILTER,static_cast<GLint>(filter(binding.minFilter)));glTexParameteri(texture.target,GL_TEXTURE_MAG_FILTER,static_cast<GLint>(filter(binding.magFilter)));glTexParameteri(texture.target,GL_TEXTURE_WRAP_S,static_cast<GLint>(wrap(binding.wrapS)));glTexParameteri(texture.target,GL_TEXTURE_WRAP_T,static_cast<GLint>(wrap(binding.wrapT)));shader.uniform(binding.uniform,binding.unit);
                }
                const auto& materialDefaults=defaults.at(mesh.shaderKey);if(materialDefaults.count("uReflOn"))shader.uniform("uReflOn",useReflection?1.0:0.0);
                if(useReflection&&materialDefaults.count("uReflMat")){std::vector<double> values;values.reserve(16);for(int c=0;c<4;c++)for(int r=0;r<4;r++)values.push_back(reflectionMatrix[c][r]);shader.uniform("uReflMat",values);}
                if(useReflection&&materialDefaults.count("uReflRes"))shader.uniform("uReflRes",std::vector<double>{reflectionResolution.x,reflectionResolution.y});
                shader.uniform("uTime",time);const std::vector<double> teamAValues{teamA.x,teamA.y,teamA.z},teamBValues{teamB.x,teamB.y,teamB.z};
                const auto setColor=[&](const char* name,const std::vector<double>& values){const auto found=materialDefaults.find(name);if(found!=materialDefaults.end()&&found->second.size()==values.size())shader.uniform(name,values);};
                setColor("uTeamA",teamAValues);setColor("uTeamB",teamBValues);setColor("uColA",teamAValues);setColor("uColB",teamBValues);setColor("uCol",mesh.name=="lobbySet:halo1"?teamBValues:teamAValues);
                const double shimmer=1.0+0.02*std::sin(time*120.0)*std::sin(time*7.3);const bool burst=std::sin(time*.37)*std::sin(time*.91+1.3)>.55;const double h=std::sin(std::floor(time*22.0)*127.1+311.7)*43758.5453;const double buzz=burst?(h-std::floor(h)>.45?1.0:.08):1.0;
                const auto glow=materialDefaults.find("uI");if(glow!=materialDefaults.end()){
                    if(glow->second.size()==4)shader.uniform("uI",std::vector<double>{7.0*shimmer,7.5*(1.0+.03*std::sin(time*1.7)),buzz,5.0});
                    else if(glow->second.size()==1){if(mesh.name=="lobbySet:halo0")shader.uniform("uI",shimmer*(.93+.07*buzz));else if(mesh.name=="lobbySet:halo1")shader.uniform("uI",1.05*(1.0+.03*std::sin(time*1.7)));}
                }
                shader.uniform("dfgLUT",6);shader.lights(lights,drawView);shader.uniform("uSunDir",std::vector<double>{lights.sunDirection.x,lights.sunDirection.y,lights.sunDirection.z});
                const auto setSpot=[&](const char* prefix,const Spot& spot,glm::dvec3 position,glm::dvec3 target,double intensity){const std::string name(prefix);const double cone=std::cos(spot.angle),penumbra=std::cos(spot.angle*(1.0-spot.penumbra));shader.uniform(name+"Position",std::vector<double>{position.x,position.y,position.z});shader.uniform(name+"Target",std::vector<double>{target.x,target.y,target.z});shader.uniform(name+"Color",std::vector<double>{spot.color.x,spot.color.y,spot.color.z});shader.uniform(name+"Intensity",spot.visible?intensity:0.0);shader.uniform(name+"Distance",spot.distance);shader.uniform(name+"Decay",spot.decay);shader.uniform(name+"ConeCos",cone);shader.uniform(name+"PenumbraCos",penumbra);};
                const auto setPoint=[&](const char* prefix,const Point* point,glm::dvec3 color,double intensity){const std::string name(prefix);const Point empty{};const auto& light=point?*point:empty;shader.uniform(name+"Position",std::vector<double>{light.position.x,light.position.y,light.position.z});shader.uniform(name+"Color",std::vector<double>{color.x,color.y,color.z});shader.uniform(name+"Intensity",light.visible?intensity:0.0);shader.uniform(name+"Distance",light.distance);shader.uniform(name+"Decay",light.decay);};
                if(hasKeySpot)setSpot("uLobbyKey",keySpot,keySpot.position,keySpot.target,keySpot.intensity);
                if(hasSpillSpot){setSpot("uSpill",spillSpot,spillSpot.position,spillSpot.target,spillSpot.intensity);shader.uniform("uSpillCookie",9.0);std::vector<double> matrix;matrix.reserve(16);for(int c=0;c<4;c++)for(int r=0;r<4;r++)matrix.push_back(spillSpot.cookieMatrix[c][r]);shader.uniform("uSpillMatrix",matrix);}
                if(hasKeySpot&&keySpot.castsShadow){glm::dmat4 bias(1);bias[0][0]=bias[1][1]=bias[2][2]=0.5;bias[3]={0.5,0.5,0.5,1.0};const auto shadowMatrix=bias*keyShadowProjection*keyShadowView;std::vector<double> matrix;matrix.reserve(16);for(int c=0;c<4;c++)for(int r=0;r<4;r++)matrix.push_back(shadowMatrix[c][r]);shader.uniform("uLobbyKeyShadow",10.0);shader.uniform("uLobbyKeyShadowMatrix",matrix);shader.uniform("uLobbyKeyShadowIntensity",keyShadowEnabled&&keyShadowReady?1.0:0.0);shader.uniform("uLobbyKeyShadowBias",keySpot.shadowBias);shader.uniform("uLobbyKeyShadowNormalBias",keySpot.shadowNormalBias);shader.uniform("uLobbyKeyShadowRadius",keySpot.shadowRadius);shader.uniform("uLobbyKeyShadowMapSize",std::vector<double>{double(keySpot.shadowSize),double(keySpot.shadowSize)});}
                if(hasRimSpot)setSpot("uRim",rimSpot,rimSpot.position,rimSpot.target,rimSpot.intensity);
                if(hasSweepSpot)setSpot("uSweep",sweepSpot,sweepPosition,sweepTarget,sweepIntensity);
                const auto findPoint=[&](const char* name)->const Point*{const auto found=pointLights.find(name);return found==pointLights.end()?nullptr:&found->second;};
                const glm::dvec3 neonA=teamA/std::max({teamA.x,teamA.y,teamA.z,1e-3}),neonB=teamB/std::max({teamB.x,teamB.y,teamB.z,1e-3});
                setPoint("uNeonA",findPoint("neonA"),neonA,16*shimmer*(.93+.07*buzz));setPoint("uNeonB",findPoint("neonB"),neonB,26*(1.0+.03*std::sin(time*1.7)));
                for(const auto& [name,uniform]:std::array<std::pair<const char*,const char*>,3>{{{"bulbs","uBulbs"},{"vend","uVend"},{"street","uStreet"}}}){const Point* point=findPoint(name);setPoint(uniform,point,point?point->color:glm::dvec3(0),point?point->intensity:0.0);}
                if(mesh.blending==0)glDisable(GL_BLEND);else{glEnable(GL_BLEND);glBlendFuncSeparate(blendFactor(mesh.srcRgb),blendFactor(mesh.dstRgb),blendFactor(mesh.srcAlpha),blendFactor(mesh.dstAlpha));glBlendEquationSeparate(blendEquation(mesh.eqRgb),blendEquation(mesh.eqAlpha));}
                if(mesh.depthTest)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);glDepthMask(mesh.depthWrite?GL_TRUE:GL_FALSE);glDepthFunc(depthFunction(mesh.depthFunc));
                if(mesh.side==2)glDisable(GL_CULL_FACE);else{glEnable(GL_CULL_FACE);glCullFace(mesh.side==1?GL_FRONT:GL_BACK);}
                glBindVertexArray(mesh.vao);
                if(mesh.instanced){shader.matrices(glm::dmat4(1),drawView,drawProjection,drawEye);glDrawElementsInstanced(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr,static_cast<GLsizei>(mesh.activeInstances));++draws;}
                else for(const auto& instance:mesh.instances){const auto diffuse=materialDefaults.find("diffuse");if(diffuse!=materialDefaults.end()&&diffuse->second.size()==3){auto color=diffuse->second;for(int c=0;c<3;c++)color[c]*=instance.color[c];shader.uniform("diffuse",color);}const int phase=shader.attribute("aPhase");if(phase>=0)glVertexAttrib1f(phase,instance.phase);shader.matrices(instance.matrix,drawView,drawProjection,drawEye);glDrawElements(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr);++draws;}
            }
            return draws;
        };
        const bool hasDynamicReflection=std::any_of(meshes.begin(),meshes.end(),[](const auto& mesh){return std::any_of(mesh->textures.begin(),mesh->textures.end(),[](const Binding& binding){return binding.dynamic;});});
        reflectionReady=false;
        if(reflectionsEnabled&&hasDynamicReflection&&eye.y>0.02){
            GLint viewport[4]{};glGetIntegerv(GL_VIEWPORT,viewport);const int width=std::max(64,int(std::round(viewport[2]*0.5))),height=std::max(64,int(std::round(viewport[3]*0.5)));GlState reflectionState;
            ensureReflectionTarget(width,height);glm::dmat4 reflectedView;glm::dvec3 reflectedEye;const auto reflectedProjection=reflectedViewProjection(view,projection,reflectedView,reflectedEye);
            glViewport(0,0,width,height);glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            drawMeshes(reflectedView,reflectedProjection,reflectedEye,true);if(reflectionDraw)reflectionDraw(reflectedView,reflectedProjection,reflectedEye);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,reflectionTexture);glGenerateMipmap(GL_TEXTURE_2D);reflectionReady=true;
        }
        return drawMeshes(view,projection,eye,false);
    }
};

LobbyRenderer::LobbyRenderer(std::filesystem::path assets):impl_(std::make_unique<Impl>(std::move(assets))){}
LobbyRenderer::~LobbyRenderer()=default;
const LobbyLayout& LobbyRenderer::Layout()const{return impl_->layout;}
const CharacterLightState& LobbyRenderer::Lights()const{return impl_->lights;}
CharacterLightState LobbyRenderer::LightingAt(glm::dvec3 worldPosition)const{
    if(!std::isfinite(worldPosition.x)||!std::isfinite(worldPosition.y)||!std::isfinite(worldPosition.z))throw std::invalid_argument("Invalid source lobby lighting position");return impl_->lightingAt(worldPosition);
}
void LobbyRenderer::AddRipple(glm::dvec3 position,double absoluteTime,double strength){
    if(!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z)||!std::isfinite(absoluteTime)||!std::isfinite(strength))throw std::invalid_argument("Invalid source lobby ripple");
    impl_->ripples[impl_->nextRipple++%impl_->ripples.size()]={static_cast<float>(position.x),static_cast<float>(position.z),static_cast<float>(absoluteTime),static_cast<float>(std::clamp(strength,0.0,1.0))};
}
void LobbyRenderer::ResetRipples(){impl_->ripples.fill(glm::vec4(0.0f));impl_->nextRipple=0;}
void LobbyRenderer::SetReflectionsEnabled(bool enabled){impl_->reflectionsEnabled=enabled;if(!enabled)impl_->reflectionReady=false;}
void LobbyRenderer::SetKeyShadowEnabled(bool enabled){impl_->keyShadowEnabled=enabled;if(!enabled)impl_->keyShadowReady=false;}
CharacterShadowState LobbyRenderer::KeyShadowState()const{return impl_->keyShadowState(false);}
int LobbyRenderer::Draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,double time,glm::dvec3 teamA,glm::dvec3 teamB,const ReflectionDraw& reflectionDraw,const ShadowDraw& shadowDraw)const{return impl_->draw(view,projection,eye,time,teamA,teamB,reflectionDraw,shadowDraw);}
}
