#include "StageRenderer.hpp"
#include "StagePropAnimation.hpp"
#include "FacePaint.hpp"
#include "PlayerSimulation.hpp"
#include "StageGeometry.hpp"
#include "CombatWorld.hpp"
#include "DirectionalShadow.hpp"
#include "CharacterShader.hpp"
#include "CharacterRenderData.hpp"
#include "SwimWake.hpp"
#include "PaintRipples.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <zlib.h>

namespace restoration {
namespace {
GLuint Compile(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Stage shader: ") + log);
    }
    return shader;
}

struct RenderVertex { glm::vec3 position, normal, color; glm::vec2 inkUV; };

struct GLStateGuard {
    GLint program=0,vao=0,arrayBuffer=0,drawFbo=0,readFbo=0,viewport[4]{},activeTexture=0;
    GLint blendSrcRgb=0,blendDstRgb=0,blendSrcAlpha=0,blendDstAlpha=0,blendEqRgb=0,blendEqAlpha=0;
    GLint depthFunc=GL_LESS,cullMode=GL_BACK,polygon[2]{GL_FILL,GL_FILL},unpack=4,pack=4,pixelPack=0,pixelUnpack=0;
    GLint textureUnits[10]={0,1,2,3,4,5,6,7,8,9},textureBindings[10]{},arrayTextureBindings[10]{};
    GLfloat clearColor[4]{},clearDepth=1.0f,polygonFactor=0.0f,polygonUnits=0.0f;
    GLboolean blend=GL_FALSE,depth=GL_FALSE,cull=GL_FALSE,scissor=GL_FALSE,sampleAlphaToCoverage=GL_FALSE,polygonOffsetFill=GL_FALSE,depthMask=GL_TRUE,colorMask[4]{GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE};
    GLStateGuard(){
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&arrayBuffer);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);glGetIntegerv(GL_VIEWPORT,viewport);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);glGetIntegerv(GL_BLEND_SRC_RGB,&blendSrcRgb);glGetIntegerv(GL_BLEND_DST_RGB,&blendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&blendSrcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&blendDstAlpha);glGetIntegerv(GL_BLEND_EQUATION_RGB,&blendEqRgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&blendEqAlpha);glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);glGetIntegerv(GL_CULL_FACE_MODE,&cullMode);
        glGetIntegerv(GL_POLYGON_MODE,polygon);glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack);glGetIntegerv(GL_PACK_ALIGNMENT,&pack);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pixelPack);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&pixelUnpack);
        glGetFloatv(GL_COLOR_CLEAR_VALUE,clearColor);glGetFloatv(GL_DEPTH_CLEAR_VALUE,&clearDepth);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&polygonFactor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&polygonUnits);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);glGetBooleanv(GL_COLOR_WRITEMASK,colorMask);
        blend=glIsEnabled(GL_BLEND);depth=glIsEnabled(GL_DEPTH_TEST);cull=glIsEnabled(GL_CULL_FACE);scissor=glIsEnabled(GL_SCISSOR_TEST);
        sampleAlphaToCoverage=glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);polygonOffsetFill=glIsEnabled(GL_POLYGON_OFFSET_FILL);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);
        for(std::size_t i=0;i<10;++i){glActiveTexture(GL_TEXTURE0+textureUnits[i]);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textureBindings[i]);glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&arrayTextureBindings[i]);}
        glActiveTexture(activeTexture);
    }
    ~GLStateGuard(){
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,drawFbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,readFbo);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,arrayBuffer);
        for(std::size_t i=0;i<10;++i){glActiveTexture(GL_TEXTURE0+textureUnits[i]);glBindTexture(GL_TEXTURE_2D,textureBindings[i]);glBindTexture(GL_TEXTURE_2D_ARRAY,arrayTextureBindings[i]);}
        glActiveTexture(activeTexture);glBlendFuncSeparate(blendSrcRgb,blendDstRgb,blendSrcAlpha,blendDstAlpha);glBlendEquationSeparate(blendEqRgb,blendEqAlpha);
        glDepthFunc(depthFunc);glCullFace(cullMode);glPolygonMode(GL_FRONT_AND_BACK,polygon[0]);
        glPolygonOffset(polygonFactor,polygonUnits);
        glClearColor(clearColor[0],clearColor[1],clearColor[2],clearColor[3]);glClearDepth(clearDepth);
        glPixelStorei(GL_UNPACK_ALIGNMENT,unpack);glPixelStorei(GL_PACK_ALIGNMENT,pack);glBindBuffer(GL_PIXEL_PACK_BUFFER,pixelPack);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pixelUnpack);
        glDepthMask(depthMask);glColorMask(colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
        if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
        if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
        if(sampleAlphaToCoverage)glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        if(polygonOffsetFill)glEnable(GL_POLYGON_OFFSET_FILL);else glDisable(GL_POLYGON_OFFSET_FILL);
    }
};

struct PackedMesh { std::string name; std::uint32_t vertices=0,indices=0,stride=0;std::vector<float> data;std::vector<std::uint32_t> elements; };
std::uint16_t readU16(const std::vector<unsigned char>& b,std::size_t& p){if(p+2>b.size())throw std::runtime_error("Truncated source stage mesh");const auto v=static_cast<std::uint16_t>(b[p]|(b[p+1]<<8));p+=2;return v;}
std::uint32_t readU32(const std::vector<unsigned char>& b,std::size_t& p){if(p+4>b.size())throw std::runtime_error("Truncated source stage mesh");const auto v=static_cast<std::uint32_t>(b[p])|(static_cast<std::uint32_t>(b[p+1])<<8)|(static_cast<std::uint32_t>(b[p+2])<<16)|(static_cast<std::uint32_t>(b[p+3])<<24);p+=4;return v;}
std::vector<PackedMesh> readMeshPack(const std::filesystem::path& path){
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Missing source stage visual pack: "+path.string());
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!="TSSTG001")throw std::runtime_error("Invalid source stage visual pack header");
    std::size_t head=8;const auto rawSize=readU32(bytes,head);if(!rawSize||rawSize>1024u*1024u*1024u)throw std::runtime_error("Invalid source stage visual payload length");
    std::vector<unsigned char> raw(rawSize);uLongf output=static_cast<uLongf>(raw.size());
    if(uncompress(raw.data(),&output,bytes.data()+head,static_cast<uLong>(bytes.size()-head))!=Z_OK||output!=raw.size())throw std::runtime_error("Cannot decompress source stage visual pack");
    std::size_t p=0;const auto sections=readU32(raw,p);if(!sections||sections>4096)throw std::runtime_error("Invalid source stage visual section count");
    std::vector<PackedMesh> result;result.reserve(sections);
    for(std::uint32_t i=0;i<sections;++i){
        PackedMesh m;const auto n=readU16(raw,p);if(p+n>raw.size())throw std::runtime_error("Truncated source stage mesh section name");m.name.assign(raw.begin()+p,raw.begin()+p+n);p+=n;
        m.vertices=readU32(raw,p);m.indices=readU32(raw,p);m.stride=readU32(raw,p);
        if(!m.vertices||!m.indices||m.indices%3||m.vertices>20000000||m.indices>100000000||!m.stride||m.stride>64)throw std::runtime_error("Invalid source stage mesh dimensions: "+m.name);
        const std::uint64_t vb=static_cast<std::uint64_t>(m.vertices)*m.stride*sizeof(float),ib=static_cast<std::uint64_t>(m.indices)*sizeof(std::uint32_t);
        if(vb+ib>raw.size()-p)throw std::runtime_error("Truncated source stage mesh data: "+m.name);
        m.data.resize(static_cast<std::size_t>(m.vertices)*m.stride);std::memcpy(m.data.data(),raw.data()+p,static_cast<std::size_t>(vb));p+=static_cast<std::size_t>(vb);
        m.elements.resize(m.indices);std::memcpy(m.elements.data(),raw.data()+p,static_cast<std::size_t>(ib));p+=static_cast<std::size_t>(ib);
        if(std::any_of(m.elements.begin(),m.elements.end(),[&](auto ix){return ix>=m.vertices;}))throw std::runtime_error("Invalid source stage mesh index: "+m.name);
        result.push_back(std::move(m));
    }
    if(p!=raw.size())throw std::runtime_error("Trailing bytes in source stage visual pack");return result;
}

std::string readText(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot read source stage shader: "+path.string());return {std::istreambuf_iterator<char>(f),{}};}
struct TextureInfo { int width=0,height=0,layers=0;bool srgb=false; };
struct TexturePack : TextureInfo { std::vector<unsigned char> pixels; };
TexturePack readTexturePack(const std::filesystem::path& path,const TextureInfo& expected){
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Missing captured source texture: "+path.string());
    std::vector<unsigned char> compressed((std::istreambuf_iterator<char>(f)),{});
    if(expected.width<=0||expected.height<=0||expected.layers<=0||expected.width>8192||expected.height>8192||expected.layers>256)
        throw std::runtime_error("Invalid captured source texture dimensions: "+path.string());
    const std::uint64_t size=static_cast<std::uint64_t>(expected.width)*expected.height*expected.layers*4;
    if(size>512u*1024u*1024u||compressed.empty())throw std::runtime_error("Invalid captured source texture payload: "+path.string());
    std::vector<unsigned char> raw(static_cast<std::size_t>(size)+24);uLongf output=static_cast<uLongf>(raw.size());
    if(uncompress(raw.data(),&output,compressed.data(),static_cast<uLong>(compressed.size()))!=Z_OK||output!=raw.size())throw std::runtime_error("Cannot decompress captured source texture: "+path.string());
    if(std::string(raw.begin(),raw.begin()+8)!="TSTEX001")throw std::runtime_error("Invalid captured source texture header: "+path.string());
    std::size_t p=8;TexturePack pack;pack.width=static_cast<int>(readU32(raw,p));pack.height=static_cast<int>(readU32(raw,p));pack.layers=static_cast<int>(readU32(raw,p));pack.srgb=readU32(raw,p)!=0;
    if(pack.width!=expected.width||pack.height!=expected.height||pack.layers!=expected.layers||pack.srgb!=expected.srgb)
        throw std::runtime_error("Captured source texture header disagrees with texture index: "+path.string());
    pack.pixels.assign(raw.begin()+24,raw.end());
    return pack;
}
std::vector<float> readTlTexture(const std::filesystem::path& path,int& width,int& height){
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot read source texlib table: "+path.string());
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});if(bytes.size()<16||std::string(bytes.begin(),bytes.begin()+8)!="TSTL0001")throw std::runtime_error("Invalid source texlib table header");
    std::size_t p=8;width=static_cast<int>(readU32(bytes,p));height=static_cast<int>(readU32(bytes,p));
    if(width<=0||width>128||height!=3||bytes.size()!=16+static_cast<std::size_t>(width)*height*4*sizeof(float))throw std::runtime_error("Invalid source texlib table dimensions");
    std::vector<float> values(static_cast<std::size_t>(width)*height*4);std::memcpy(values.data(),bytes.data()+16,values.size()*sizeof(float));
    if(std::any_of(values.begin(),values.end(),[](float value){return !std::isfinite(value);}))throw std::runtime_error("Non-finite source texlib table value");
    return values;
}
std::vector<std::pair<std::string,std::vector<double>>> readUniforms(const std::filesystem::path& path){
    std::ifstream f(path);if(!f)throw std::runtime_error("Cannot read source stage material defaults: "+path.string());
    std::vector<std::pair<std::string,std::vector<double>>> out;std::string name;std::size_t count;
    while(f>>name>>count){if(!count||count>4096)throw std::runtime_error("Invalid source material uniform count");std::vector<double> values(count);for(auto& v:values)if(!(f>>v)||!std::isfinite(v))throw std::runtime_error("Truncated source material defaults");out.emplace_back(name,std::move(values));}
    return out;
}
struct RenderState { bool transparent=false,depthWrite=true,depthTest=true,alphaToCoverage=false,polygonOffset=false;int side=0,blending=1,depthFunc=3;float polygonFactor=0.0f,polygonUnits=0.0f; };
RenderState readRenderState(const std::filesystem::path& path){std::ifstream f(path);RenderState state;int transparent=0,depthWrite=1,depthTest=1,alphaToCoverage=0,polygonOffset=0;if(!(f>>transparent>>depthWrite>>depthTest>>state.side>>state.blending>>state.depthFunc>>alphaToCoverage>>polygonOffset>>state.polygonFactor>>state.polygonUnits))throw std::runtime_error("Cannot read source material render state: "+path.string());state.transparent=transparent!=0;state.depthWrite=depthWrite!=0;state.depthTest=depthTest!=0;state.alphaToCoverage=alphaToCoverage!=0;state.polygonOffset=polygonOffset!=0;return state;}
GLenum depthFunction(int source){switch(source){case 0:return GL_NEVER;case 1:return GL_ALWAYS;case 2:return GL_LESS;case 3:return GL_LEQUAL;case 4:return GL_EQUAL;case 5:return GL_GEQUAL;case 6:return GL_GREATER;case 7:return GL_NOTEQUAL;default:throw std::runtime_error("Unsupported source material depth function");}}

}

struct StageRenderer::Source {
    static constexpr int atlasSize=4096;
    struct AnimatedPiece {StagePropAnimation animation;std::vector<float> data;unsigned stride=11;};
    struct GpuMesh {
        GLuint vao=0,vbo=0,ebo=0;GLsizei count=0;std::string name,shader;RenderState state;bool castsShadow=true;
        StagePropAnimation animation;
        std::vector<float> rest;
        std::vector<std::uint32_t> elements;
        unsigned stride=0;
        std::vector<AnimatedPiece> pieces;
        double animatedAt=-1;
        ~GpuMesh(){if(ebo)glDeleteBuffers(1,&ebo);if(vbo)glDeleteBuffers(1,&vbo);if(vao)glDeleteVertexArrays(1,&vao);}
    };
    struct ShaderEntry { std::unique_ptr<CharacterShader> shader;std::vector<std::pair<std::string,std::vector<double>>> defaults;RenderState state; };
    struct Growing { VisualSplat splat;double age=0,dur=0,dripDur=0; };
    struct PaintVertex { float pos[2],local[3],splat[4],stretch[3],grow[4]; };
    struct FloodVertex { float pos[2],world[3]; };
    struct Flood {VisualFlood request;std::vector<FloodVertex> vertices;double age=0,radius=-1;};
    std::filesystem::path root;
    bool environment=false;
    std::string key;
    std::string stageId;
    std::string uploadedTheme;
    std::vector<std::unique_ptr<GpuMesh>> meshes;
    std::map<std::string,ShaderEntry> shaders;
    std::map<std::string,std::filesystem::path> textureFiles;
    std::map<std::string,TextureInfo> textureInfo;
    std::map<std::string,GLuint> texture2D,textureArrays;
    GLuint atlas=0,framebuffer=0,white=0,transparent=0,dfg=0,dummyShadow=0,tlTexture=0,mural=0,paintProgram=0,dryProgram=0,paintVao=0,paintVbo=0,dryVao=0,dryVbo=0;
    std::string muralId;
    std::vector<Growing> growing;
    std::vector<Flood> floods;
    SwimWake swimWake;
    PaintRipples ripples;
    std::optional<glm::dvec3> rippleCamera;
    std::unique_ptr<CharacterShader> floodShader;
    GLuint floodVao=0,floodVbo=0;
    std::vector<AtlasRect> atlasRects;
    double clock=0,dryAcc=0;
    double ppm=0;
    int usedHeight=0;

    explicit Source(std::filesystem::path directory,bool useEnvironment):root(std::move(directory)),environment(useEnvironment) {
        GLStateGuard state;
        GLint maxTexture=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxTexture);if(maxTexture<atlasSize)throw std::runtime_error("Source stage ink atlas requires a 4096 texture size");
        GLuint vertex=0,fragment=0;
        try {
            vertex=Compile(GL_VERTEX_SHADER,readText(root/"shaders/paint.vert").c_str());
            fragment=Compile(GL_FRAGMENT_SHADER,readText(root/"shaders/paint.frag").c_str());
            paintProgram=glCreateProgram();glAttachShader(paintProgram,vertex);glAttachShader(paintProgram,fragment);glLinkProgram(paintProgram);
            GLint ok=0;glGetProgramiv(paintProgram,GL_LINK_STATUS,&ok);if(!ok){char log[4096]{};glGetProgramInfoLog(paintProgram,sizeof(log),nullptr,log);throw std::runtime_error(std::string("Source paint shader link: ")+log);}
            glDeleteShader(vertex);vertex=0;glDeleteShader(fragment);fragment=0;
            const char* dryVs="#version 450 core\nlayout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0.0,1.0);}";
            const char* dryFs="#version 450 core\nlayout(location=0) out vec4 outColor;uniform float uDry;void main(){outColor=vec4(0.0,uDry,0.0,0.0);} ";
            vertex=Compile(GL_VERTEX_SHADER,dryVs);fragment=Compile(GL_FRAGMENT_SHADER,dryFs);dryProgram=glCreateProgram();glAttachShader(dryProgram,vertex);glAttachShader(dryProgram,fragment);glLinkProgram(dryProgram);
            glGetProgramiv(dryProgram,GL_LINK_STATUS,&ok);if(!ok){char log[4096]{};glGetProgramInfoLog(dryProgram,sizeof(log),nullptr,log);throw std::runtime_error(std::string("Source ink dry shader link: ")+log);}
            glDeleteShader(vertex);vertex=0;glDeleteShader(fragment);fragment=0;
            glGenVertexArrays(1,&paintVao);glGenBuffers(1,&paintVbo);glBindVertexArray(paintVao);glBindBuffer(GL_ARRAY_BUFFER,paintVbo);
            glBufferData(GL_ARRAY_BUFFER,sizeof(PaintVertex),nullptr,GL_STREAM_DRAW);
            const auto attrib=[&](const char* name,GLint n,std::size_t offset){const GLint loc=glGetAttribLocation(paintProgram,name);if(loc<0)throw std::runtime_error(std::string("Missing source paint attribute ")+name);glEnableVertexAttribArray(loc);glVertexAttribPointer(loc,n,GL_FLOAT,GL_FALSE,sizeof(PaintVertex),reinterpret_cast<void*>(offset));};
            attrib("aPos",2,offsetof(PaintVertex,pos));attrib("aLocal",3,offsetof(PaintVertex,local));attrib("aSplat",4,offsetof(PaintVertex,splat));attrib("aStretch",3,offsetof(PaintVertex,stretch));attrib("aGrow",4,offsetof(PaintVertex,grow));
            glGenVertexArrays(1,&dryVao);glGenBuffers(1,&dryVbo);glBindVertexArray(dryVao);glBindBuffer(GL_ARRAY_BUFFER,dryVbo);
            const GLint pLoc=glGetAttribLocation(dryProgram,"p");if(pLoc<0)throw std::runtime_error("Missing source ink drying position attribute");
            glEnableVertexAttribArray(pLoc);glVertexAttribPointer(pLoc,2,GL_FLOAT,GL_FALSE,2*sizeof(float),nullptr);glBindVertexArray(0);
            glGenTextures(1,&atlas);glBindTexture(GL_TEXTURE_2D,atlas);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,atlasSize,atlasSize,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glGenerateMipmap(GL_TEXTURE_2D);
            glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,atlas,0);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete source stage ink framebuffer");
            const unsigned char whitePixel[4]={255,255,255,255};glGenTextures(1,&white);glBindTexture(GL_TEXTURE_2D,white);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,whitePixel);
            const unsigned char transparentPixel[4]={255,255,255,0};glGenTextures(1,&transparent);glBindTexture(GL_TEXTURE_2D,transparent);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,transparentPixel);
            glGenTextures(1,&dfg);glBindTexture(GL_TEXTURE_2D,dfg);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RG16F,16,16,0,GL_RG,GL_HALF_FLOAT,CharacterDfg.data());
            const float depthOne=1.0f;glGenTextures(1,&dummyShadow);glBindTexture(GL_TEXTURE_2D,dummyShadow);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
            const float border[]={1,1,1,1};glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_FUNC,GL_LEQUAL);
            glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT32F,1,1,0,GL_DEPTH_COMPONENT,GL_FLOAT,&depthOne);
            loadStaticTextures();
        } catch (...) { if(vertex)glDeleteShader(vertex);if(fragment)glDeleteShader(fragment);cleanup();throw; }
    }
    GLuint uploadTexture(const std::string& name,GLenum target,bool mipmaps,bool repeat){
        const TexturePack pack=readTexturePack(textureFiles.at(name),textureInfo.at(name));GLuint texture=0;glGenTextures(1,&texture);glBindTexture(target,texture);
        const GLenum internal=pack.srgb?GL_SRGB8_ALPHA8:GL_RGBA8;
        glTexParameteri(target,GL_TEXTURE_MIN_FILTER,mipmaps?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(target,GL_TEXTURE_WRAP_S,repeat?GL_REPEAT:GL_CLAMP_TO_EDGE);glTexParameteri(target,GL_TEXTURE_WRAP_T,repeat?GL_REPEAT:GL_CLAMP_TO_EDGE);
        if(target==GL_TEXTURE_2D_ARRAY)glTexImage3D(target,0,internal,pack.width,pack.height,pack.layers,0,GL_RGBA,GL_UNSIGNED_BYTE,pack.pixels.data());
        else glTexImage2D(target,0,internal,pack.width,pack.height,0,GL_RGBA,GL_UNSIGNED_BYTE,pack.pixels.data());
        if(mipmaps)glGenerateMipmap(target);return texture;
    }
    void loadStaticTextures(){
        std::ifstream index(root/"textures.index");if(!index)throw std::runtime_error("Cannot read source stage texture index: "+(root/"textures.index").string());
        std::string name,path;TextureInfo info;while(index>>name>>path>>info.width>>info.height>>info.layers>>info.srgb){
            const auto key=name;const auto texturePath=(root/path).lexically_normal();
            if(!textureFiles.emplace(key,texturePath).second)throw std::runtime_error("Duplicate source texture entry: "+key);
            textureInfo.emplace(key,info);
        }
        if(!index.eof())throw std::runtime_error("Malformed source stage texture index");
        const auto require=[&](const std::string& name){if(!textureFiles.count(name))throw std::runtime_error("Missing source texture index entry: "+name);};
        for(const auto& name:{"albedo","normal","orm","props-atlas","props-chain","props-blob"})require(name);
        textureArrays["albedo"]=uploadTexture("albedo",GL_TEXTURE_2D_ARRAY,true,true);
        textureArrays["normal"]=uploadTexture("normal",GL_TEXTURE_2D_ARRAY,true,true);
        textureArrays["orm"]=uploadTexture("orm",GL_TEXTURE_2D_ARRAY,true,true);
        texture2D["props-atlas"]=uploadTexture("props-atlas",GL_TEXTURE_2D,true,false);
        texture2D["props-chain"]=uploadTexture("props-chain",GL_TEXTURE_2D,true,true);
        texture2D["props-blob"]=uploadTexture("props-blob",GL_TEXTURE_2D,true,false);
        int width=0,height=0;const auto values=readTlTexture(root/"shaders/level.tl",width,height);glGenTextures(1,&tlTexture);glBindTexture(GL_TEXTURE_2D,tlTexture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,width,height,0,GL_RGBA,GL_FLOAT,values.data());
    }
    void loadMural(const std::string& id){
        if(muralId==id&&mural)return;const auto name="mural-"+id;if(!textureFiles.count(name))throw std::runtime_error("Missing source mural texture for stage: "+id);
        const GLuint next=uploadTexture(name,GL_TEXTURE_2D,true,false);if(mural)glDeleteTextures(1,&mural);mural=next;muralId=id;
    }
    void cleanup(){
        floodShader.reset();if(floodVbo)glDeleteBuffers(1,&floodVbo);if(floodVao)glDeleteVertexArrays(1,&floodVao);floodVbo=floodVao=0;
        meshes.clear();for(auto& [_,entry]:shaders)entry.shader.reset();
        const GLuint buffers[]={paintVbo,dryVbo};glDeleteBuffers(2,buffers);paintVbo=dryVbo=0;const GLuint arrays[]={paintVao,dryVao};glDeleteVertexArrays(2,arrays);paintVao=dryVao=0;
        const GLuint textures[]={atlas,white,transparent,dfg,dummyShadow,tlTexture,mural};glDeleteTextures(7,textures);atlas=white=transparent=dfg=dummyShadow=tlTexture=mural=0;
        for(auto& [_,texture]:texture2D)if(texture)glDeleteTextures(1,&texture);texture2D.clear();for(auto& [_,texture]:textureArrays)if(texture)glDeleteTextures(1,&texture);textureArrays.clear();textureFiles.clear();textureInfo.clear();
        const GLuint programs[]={paintProgram,dryProgram};glDeleteProgram(programs[0]);glDeleteProgram(programs[1]);paintProgram=dryProgram=0;if(framebuffer)glDeleteFramebuffers(1,&framebuffer);framebuffer=0;
    }
    ~Source(){cleanup();}

    void clearInk(){
        GLStateGuard state;glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glViewport(0,0,atlasSize,atlasSize);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);glBindTexture(GL_TEXTURE_2D,atlas);glGenerateMipmap(GL_TEXTURE_2D);growing.clear();floods.clear();swimWake.reset();ripples.reset();clock=0;dryAcc=0;
    }
    ShaderEntry& getShader(const std::string& name){
        auto found=shaders.find(name);if(found!=shaders.end())return found->second;
        const std::string file=name=="level"?"level":"props-"+name.substr(6);
        const std::string defaults=name=="level"?"level-"+stageId:file;
        ShaderEntry entry;entry.shader=std::make_unique<CharacterShader>(readText(root/"shaders"/(file+".vert")),readText(root/"shaders"/(file+".frag")),true,environment);
        entry.defaults=readUniforms(root/"shaders"/(defaults+".uniforms"));if(name!="level")entry.state=readRenderState(root/"shaders"/(file+".state"));
        return shaders.emplace(name,std::move(entry)).first->second;
    }
    std::unique_ptr<GpuMesh> makeMesh(const PackedMesh& mesh){
        auto gpu=std::make_unique<GpuMesh>();gpu->name=mesh.name;gpu->shader=mesh.name=="stage"?"level":mesh.name.substr(0,6)=="props."?mesh.name.substr(0,mesh.name.find_first_of("@~")):"";
        if(mesh.name.find('@')!=std::string::npos){gpu->rest=mesh.data;gpu->elements=mesh.elements;gpu->stride=mesh.stride;}
        if(gpu->shader.empty())throw std::runtime_error("Unknown source stage mesh section: "+mesh.name);
        auto& shader=getShader(gpu->shader);gpu->state=shader.state;gpu->castsShadow=mesh.name.find("~ns")==std::string::npos&&gpu->shader!="props.glow"&&gpu->shader!="props.blob";glGenVertexArrays(1,&gpu->vao);glBindVertexArray(gpu->vao);
        glGenBuffers(1,&gpu->vbo);glBindBuffer(GL_ARRAY_BUFFER,gpu->vbo);glBufferData(GL_ARRAY_BUFFER,mesh.data.size()*sizeof(float),mesh.data.data(),GL_STATIC_DRAW);
        glGenBuffers(1,&gpu->ebo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,gpu->ebo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,mesh.elements.size()*sizeof(std::uint32_t),mesh.elements.data(),GL_STATIC_DRAW);
        const std::size_t stride=mesh.stride*sizeof(float);
        if(mesh.name=="stage"){
            const std::array<std::pair<const char*,std::pair<int,std::size_t>>,8> attrs={{{"position",{3,0}}, {"normal",{3,3}}, {"color",{3,6}}, {"paintUv",{2,9}}, {"faceUv",{2,13}}, {"faceData",{4,15}}, {"faceFlags",{3,19}}, {"faceTan",{3,22}}}};
            for(const auto& a:attrs){const GLint loc=shader.shader->attribute(a.first);if(loc<0)continue;glEnableVertexAttribArray(loc);glVertexAttribPointer(loc,a.second.first,GL_FLOAT,GL_FALSE,static_cast<GLsizei>(stride),reinterpret_cast<void*>(a.second.second*sizeof(float)));}
        }else{
            const std::array<std::pair<const char*,std::pair<int,std::size_t>>,4> attrs={{{"position",{3,0}}, {"normal",{3,3}}, {"color",{3,6}}, {"uv",{2,9}}}};
            for(const auto& a:attrs){const GLint loc=shader.shader->attribute(a.first);if(loc<0)continue;glEnableVertexAttribArray(loc);glVertexAttribPointer(loc,a.second.first,GL_FLOAT,GL_FALSE,static_cast<GLsizei>(stride),reinterpret_cast<void*>(a.second.second*sizeof(float)));}
        }
        gpu->count=static_cast<GLsizei>(mesh.indices);glBindVertexArray(0);return gpu;
    }
    void load(const StageData& stage){
        const std::string requested=std::string(stage.id)+"-"+std::string(stage.mode);if(key==requested)return;
        uploadedTheme=std::string(stage.theme);
        GLStateGuard state;meshes.clear();for(auto& [_,entry]:shaders)entry.shader.reset();shaders.clear();stageId=std::string(stage.id);loadMural(stageId);const auto packed=readMeshPack(root/(requested+".stg.z"));
        bool haveStage=false;for(const auto& data:packed){if(data.name=="stage")haveStage=true;meshes.push_back(makeMesh(data));}
        std::ifstream animationFile(root/(requested+".anim"));
        if(!animationFile)throw std::runtime_error("Missing source stage animation metadata: "+requested);
        std::string name;
        while(animationFile>>name){
            const auto found=std::find_if(meshes.begin(),meshes.end(),[&](const auto& m){return m->name==name;});
            if(found==meshes.end())throw std::runtime_error("Source prop animation references missing mesh: "+name);
            auto& a=(*found)->animation;
            if(!(animationFile>>a.kind>>a.speed>>a.phase>>a.rate>>a.lo>>a.hi)||
                (a.kind!="y"&&a.kind!="z"&&a.kind!="blink"&&a.kind!="cloth"))throw std::runtime_error("Invalid source prop animation: "+name);
            for(int c=0;c<4;c++)for(int r=0;r<4;r++)if(!(animationFile>>a.base[c][r])||!std::isfinite(a.base[c][r]))throw std::runtime_error("Invalid source prop animation matrix");
        }
        batchAnimations();
        if(!haveStage||meshes.empty())throw std::runtime_error("Source stage visual pack has no level geometry: "+requested);
        key=requested;usedHeight=0;ppm=0;atlasRects.clear();growing.clear();clock=dryAcc=0;clearInk();
    }

    GLuint propMap(const std::string& shaderName)const{
        if(shaderName=="props.fence")return texture2D.at("props-chain");
        if(shaderName=="props.blob")return texture2D.at("props-blob");
        if(shaderName=="props.paint"||shaderName=="props.glow"||shaderName=="props.cloth")return texture2D.at("props-atlas");
        return white;
    }
    void bindCommonTextures(ShaderEntry& entry,bool level,const std::string& shaderName,const DirectionalShadow* shadow){
        auto& shader=*entry.shader;
        shader.uniform("uPaint",0.0);shader.uniform("uMural",1.0);
        shader.uniform("map",1.0);shader.uniform("alphaMap",1.0);
        shader.uniform("tAlbedo",3.0);shader.uniform("tNormal",4.0);shader.uniform("tOrm",5.0);shader.uniform("uTLTex",2.0);
        shader.uniform("dfgLUT",6.0);shader.uniform("directionalShadowMap[0]",7.0);
        const auto bind=[&](int unit,GLuint texture){glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,texture);};
        bind(0,atlas);bind(1,level?mural:propMap(shaderName));bind(2,tlTexture);bind(6,dfg);
        bind(7,shadow&&shadow->texture()?shadow->texture():dummyShadow);
        const auto bindArray=[&](int unit,GLuint texture){glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D_ARRAY,texture);};
        bindArray(3,textureArrays.at("albedo"));bindArray(4,textureArrays.at("normal"));bindArray(5,textureArrays.at("orm"));
        if(level){shader.uniform("uAtlasSize",static_cast<double>(atlasSize));shader.uniform("uTexel",1.0/atlasSize);shader.uniform("uPpm",ppm);
            shader.uniform("uInkClock",clock);shader.uniform("uTime",clock);}
    }
    void uploadSwimWake(ShaderEntry& entry){
        const auto& values=swimWake.uniformValues();
        entry.shader->uniform("uWake",values.points);entry.shader->uniform("uWakeB",values.bounds);
        entry.shader->uniform("uSwimH",values.heads);entry.shader->uniform("uSwimF",values.forwards);
        entry.shader->uniform("uRip",ripples.positions());entry.shader->uniform("uRipP",ripples.parameters());
    }
    void applyShadow(ShaderEntry& entry,const DirectionalShadow* shadow){
        auto& shader=*entry.shader;
        if(!shadow||!shadow->texture()){
            shader.uniform("directionalLightShadows[0].shadowIntensity",0.0);
            shader.uniform("directionalLightShadows[0].shadowBias",0.0);
            shader.uniform("directionalLightShadows[0].shadowNormalBias",0.0);
            shader.uniform("directionalLightShadows[0].shadowRadius",0.0);
            shader.uniform("directionalLightShadows[0].shadowMapSize",std::vector<double>{1.0,1.0});
            shader.uniform("directionalShadowMatrix[0]",std::vector<double>(16,0.0));
            return;
        }
        const glm::dmat4 bias(glm::dmat4(
            glm::dvec4(0.5,0.0,0.0,0.0),glm::dvec4(0.0,0.5,0.0,0.0),
            glm::dvec4(0.0,0.0,0.5,0.0),glm::dvec4(0.5,0.5,0.5,1.0)));
        const glm::dmat4 matrix=bias*shadow->matrix();std::vector<double> values(16);
        for(int c=0;c<4;++c)for(int r=0;r<4;++r)values[c*4+r]=matrix[c][r];
        shader.uniform("directionalShadowMatrix[0]",values);
        shader.uniform("directionalLightShadows[0].shadowIntensity",1.0);
        shader.uniform("directionalLightShadows[0].shadowBias",shadow->bias());
        shader.uniform("directionalLightShadows[0].shadowNormalBias",shadow->normalBias());
        shader.uniform("directionalLightShadows[0].shadowRadius",shadow->radius());
        shader.uniform("directionalLightShadows[0].shadowMapSize",std::vector<double>{static_cast<double>(shadow->size()),static_cast<double>(shadow->size())});
    }
    void animate(GpuMesh& mesh){
        if(!mesh.pieces.empty()){
            if(mesh.animatedAt==clock)return;
            std::vector<float> data;
            for(const auto& piece:mesh.pieces){
                const auto matrix=glm::mat4(piece.animation.matrix(clock));const auto normal=glm::transpose(glm::inverse(glm::mat3(matrix)));
                const float brightness=static_cast<float>(piece.animation.brightness(clock));
                for(std::size_t i=0;i<piece.data.size();i+=piece.stride){
                    glm::vec3 p(piece.data[i],piece.data[i+1],piece.data[i+2]),n(piece.data[i+3],piece.data[i+4],piece.data[i+5]);
                    if(piece.animation.kind=="cloth")piece.animation.cloth(clock,piece.data[i+11],{piece.data[i+12],piece.data[i+13],piece.data[i+14]},p,n);
                    p=glm::vec3(matrix*glm::vec4(p,1));n=glm::normalize(normal*n);
                    data.insert(data.end(),{p.x,p.y,p.z,n.x,n.y,n.z,piece.data[i+6]*brightness,piece.data[i+7]*brightness,piece.data[i+8]*brightness,piece.data[i+9],piece.data[i+10]});
                }
            }
            glBindBuffer(GL_ARRAY_BUFFER,mesh.vbo);glBufferSubData(GL_ARRAY_BUFFER,0,data.size()*sizeof(float),data.data());mesh.animatedAt=clock;return;
        }
    }
    void batchAnimations(){
        struct Batch {PackedMesh mesh;std::vector<AnimatedPiece> pieces;};std::map<std::string,Batch> batches;
        for(const auto& mesh:meshes){
            if(mesh->animation.kind.empty())continue;auto& batch=batches[mesh->shader];batch.mesh.name=mesh->shader;batch.mesh.stride=11;
            AnimatedPiece piece{mesh->animation,mesh->rest,mesh->stride};
            const auto vertices=static_cast<unsigned>(mesh->rest.size()/mesh->stride);
            for(std::size_t i=0;i<mesh->rest.size();i+=mesh->stride)batch.mesh.data.insert(batch.mesh.data.end(),mesh->rest.begin()+i,mesh->rest.begin()+i+11);
            for(const auto index:mesh->elements)batch.mesh.elements.push_back(batch.mesh.vertices+index);
            batch.mesh.vertices+=vertices;batch.mesh.indices+=static_cast<unsigned>(mesh->elements.size());batch.pieces.push_back(std::move(piece));
        }
        meshes.erase(std::remove_if(meshes.begin(),meshes.end(),[](const auto& m){return !m->animation.kind.empty();}),meshes.end());
        for(auto& [_,batch]:batches){auto gpu=makeMesh(batch.mesh);gpu->pieces=std::move(batch.pieces);gpu->castsShadow=gpu->shader!="props.blink";meshes.push_back(std::move(gpu));}
    }
    int draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& lights,bool wireframe,const DirectionalShadow* shadow,bool reflection=false){
        if(meshes.empty())return 0;
        GLStateGuard state;int draws=0;
        glPolygonMode(GL_FRONT_AND_BACK,wireframe?GL_LINE:GL_FILL);
        for(bool transparentPass:{false,true})for(const auto& mesh:meshes){
            if(mesh->state.transparent!=transparentPass)continue;
            if(reflection&&(mesh->shader=="props.glow"||mesh->shader=="props.blink"||mesh->shader=="props.cloth"||mesh->shader=="props.fence"||mesh->shader=="props.blob"||mesh->shader=="props.foliage"||!mesh->pieces.empty()))continue;
            auto& entry=getShader(mesh->shader);auto& shader=*entry.shader;shader.use();
            shader.uniform("iwDepthOnly",0);
            for(const auto& [name,value]:entry.defaults)shader.uniform(name,value);
            if(mesh->shader=="level")uploadSwimWake(entry);
            animate(*mesh);
            shader.lights(lights,view);shader.matrices(mesh->animation.matrix(clock),view,projection,eye);applyShadow(entry,shadow);bindCommonTextures(entry,mesh->shader=="level",mesh->shader,shadow);
            if(mesh->animation.kind=="blink")shader.uniform("diffuse",std::vector<double>(3,mesh->animation.brightness(clock)));
            if(mesh->shader=="props.glow")shader.uniform("diffuse",std::vector<double>(3,uploadedTheme=="sunset"?1.0:.5));
            glDepthMask(mesh->state.depthWrite?GL_TRUE:GL_FALSE);if(mesh->state.depthTest)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
            glDepthFunc(depthFunction(mesh->state.depthFunc));
            if(mesh->state.side==2)glDisable(GL_CULL_FACE);else{glEnable(GL_CULL_FACE);glCullFace(mesh->state.side==1?GL_FRONT:GL_BACK);}
            if(mesh->state.alphaToCoverage)glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
            if(mesh->state.polygonOffset){glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(mesh->state.polygonFactor,mesh->state.polygonUnits);}else glDisable(GL_POLYGON_OFFSET_FILL);
            if(mesh->state.transparent){glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);if(mesh->state.blending==2)glBlendFunc(GL_SRC_ALPHA,GL_ONE);else glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);}
            else glDisable(GL_BLEND);
            glBindVertexArray(mesh->vao);glDrawElements(GL_TRIANGLES,mesh->count,GL_UNSIGNED_INT,nullptr);++draws;
        }
        glBindVertexArray(0);return draws;
    }
    int drawShadow(const glm::dmat4& lightViewProjection){
        if(meshes.empty())return 0;
        GLStateGuard state;
        glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        const glm::dmat4 identity(1.0);const glm::dvec3 eye(0.0);const CharacterLightState lights{};int draws=0;
        for(const auto& mesh:meshes){
            if(!mesh->castsShadow)continue;
            auto& entry=getShader(mesh->shader);auto& shader=*entry.shader;shader.use();
            shader.uniform("iwDepthOnly",1);
            for(const auto& [name,value]:entry.defaults)shader.uniform(name,value);
            animate(*mesh);
            shader.lights(lights,identity);shader.matrices(mesh->animation.matrix(clock),identity,lightViewProjection,eye);applyShadow(entry,nullptr);bindCommonTextures(entry,mesh->shader=="level",mesh->shader,nullptr);
            glBindVertexArray(mesh->vao);glDrawElements(GL_TRIANGLES,mesh->count,GL_UNSIGNED_INT,nullptr);++draws;
        }
        glBindVertexArray(0);return draws;
    }

    void appendSplat(const StageData& stage,const FacePaint& paint,const Growing& g,bool dripOnly,double tn,double dT,std::vector<PaintVertex>& out)const{
        static constexpr double reachK[8]={2.45,2.1,2.7,2.75,2.3,1.9,1.25,1.35};
        const auto& s=g.splat;const int kind=static_cast<int>(s.kind);if(kind<0||kind>=8||s.radius<=0.02)return;
        double stretchAmt=(s.hasStretch&&s.kind!=SplatKind::Roll)?s.stretchAmt:0.0;
        bool wallDrips=false;
        struct Entry{int face;double lu,lv,dn,sdu,sdv,sa,rr;};std::vector<Entry> entries;entries.reserve(24);
        for(int id=0;id<static_cast<int>(stage.faces.size());++id){
            const auto& f=stage.faces[id];if(!f.paintable)continue;
            const glm::dvec3 rel=s.center-f.origin;const double dn=glm::dot(rel,f.normal);if(dn>s.radius||dn<-.12)continue;
            const double lu=glm::dot(rel,f.u),lv=glm::dot(rel,f.v),rr=std::sqrt(std::max(0.0,s.radius*s.radius-dn*dn));
            const double ext=rr*(reachK[kind]+1.4*stretchAmt);if(lu< -ext||lu>f.width+ext||lv< -ext-(f.wall?rr*3.9:0.0)||lv>f.height+ext)continue;
            double sdu=0,sdv=0,sa=0;
            if(s.hasStretch){sdu=glm::dot(s.stretch,f.u);sdv=glm::dot(s.stretch,f.v);const double length=std::hypot(sdu,sdv);if(length>.2){sdu/=length;sdv/=length;sa=stretchAmt*length;}else if(s.kind==SplatKind::Roll)sdu=1;}
            entries.push_back({id,lu,lv,dn,sdu,sdv,sa,rr});if(f.wall&&rr>s.radius*.3&&s.kind!=SplatKind::Speck&&s.kind!=SplatKind::Roll)wallDrips=true;
        }
        const bool hasDrips=wallDrips&&g.dripDur>0;
        const int flags=kind*2;
        for(const auto& e:entries){
            const auto& f=stage.faces[e.face];if(dripOnly&&(!f.wall||e.rr<s.radius*.3))continue;
            const auto& a=paint.atlas(e.face);const double padM=(a.pad-.5)/a.ppm;
            double u0,u1,v0,v1;
            if(dripOnly){u0=e.lu-e.rr*.95;u1=e.lu+e.rr*.95;v0=e.lv-e.rr*3.9;v1=e.lv-e.rr*.3;}
            else{const double ext=e.rr*(reachK[kind]+1.4*e.sa),down=f.wall&&hasDrips?e.rr*3.9:0;u0=e.lu-ext;u1=e.lu+ext;v0=e.lv-std::max(ext,down);v1=e.lv+ext;}
            u0=std::max(-padM,u0);u1=std::min(f.width+padM,u1);v0=std::max(-padM,v0);v1=std::min(f.height+padM,v1);if(u1<=u0||v1<=v0)continue;
            const auto push=[&](double u,double v){
                PaintVertex p{};const double x=(a.x+a.pad+u*a.ppm)/atlasSize,y=(a.y+a.pad+v*a.ppm)/atlasSize;
                p.pos[0]=static_cast<float>(x*2-1);p.pos[1]=static_cast<float>(y*2-1);p.local[0]=static_cast<float>(u-e.lu);p.local[1]=static_cast<float>(v-e.lv);p.local[2]=static_cast<float>(e.dn);
                p.splat[0]=static_cast<float>(s.radius);p.splat[1]=static_cast<float>(s.team);p.splat[2]=static_cast<float>(s.seed);p.splat[3]=static_cast<float>(flags+(f.wall?1:0));
                p.stretch[0]=static_cast<float>(e.sdu);p.stretch[1]=static_cast<float>(e.sdv);p.stretch[2]=static_cast<float>(e.sa);
                p.grow[0]=static_cast<float>(tn);p.grow[1]=static_cast<float>(dT);p.grow[2]=dripOnly?1.f:0.f;out.push_back(p);
            };
            push(u0,v0);push(u1,v0);push(u1,v1);push(u0,v0);push(u1,v1);push(u0,v1);
        }
    }

    void queueFlood(const StageData& stage,const FacePaint& paint,VisualFlood request){
        if(request.polygons.size()>6)throw std::runtime_error("Source flood polygon capacity exceeded");
        std::size_t points=0;for(const auto& polygon:request.polygons)points+=polygon.size();
        if(points>64)throw std::runtime_error("Source flood vertex capacity exceeded");
        floods.erase(std::remove_if(floods.begin(),floods.end(),[&](const Flood& f){return f.request.regionId==request.regionId;}),floods.end());
        Flood flood;flood.request=std::move(request);
        std::map<int,std::array<int,4>> bounds;
        for(const int cell:flood.request.cells){
            for(int face=0;face<int(stage.faces.size());++face){const auto& g=paint.faceGrid(face);if(g.offset<0||cell<g.offset||cell>=g.offset+g.columns*g.rows)continue;
                const int local=cell-g.offset,x=local%g.columns,y=local/g.columns;const auto at=bounds.find(face);
                if(at==bounds.end())bounds.emplace(face,std::array<int,4>{x,x,y,y});
                else{auto& b=at->second;b[0]=std::min(b[0],x);b[1]=std::max(b[1],x);b[2]=std::min(b[2],y);b[3]=std::max(b[3],y);}break;
            }
        }
        for(const auto& [id,b]:bounds){const auto& f=stage.faces[id];const auto& g=paint.faceGrid(id);const auto& a=paint.atlas(id);
            const double pad=(a.pad-.5)/a.ppm,u0=std::max(-pad,(b[0]-1)*g.cellWidth),u1=std::min(f.width+pad,(b[1]+2)*g.cellWidth),v0=std::max(-pad,(b[2]-1)*g.cellHeight),v1=std::min(f.height+pad,(b[3]+2)*g.cellHeight);
            for(const auto& uv:std::array<glm::dvec2,6>{{{u0,v0},{u1,v0},{u1,v1},{u0,v0},{u1,v1},{u0,v1}}}){
                const auto world=f.origin+f.u*uv.x+f.v*uv.y;FloodVertex vertex{};
                vertex.pos[0]=float((a.x+a.pad+uv.x*a.ppm)/atlasSize*2-1);vertex.pos[1]=float((a.y+a.pad+uv.y*a.ppm)/atlasSize*2-1);
                for(int c=0;c<3;++c)vertex.world[c]=float(world[c]);flood.vertices.push_back(vertex);
            }
        }
        floods.push_back(std::move(flood));
    }
    void drawFloods(double dt){
        if(floods.empty())return;
        if(!floodShader){
            const auto assets=root.parent_path()/"zones";
            floodShader=std::make_unique<CharacterShader>(readText(assets/"flood.vert"),readText(assets/"flood.frag"),true,false,false);
            glGenVertexArrays(1,&floodVao);glBindVertexArray(floodVao);glGenBuffers(1,&floodVbo);glBindBuffer(GL_ARRAY_BUFFER,floodVbo);
            const auto position=floodShader->attribute("aPos"),world=floodShader->attribute("aW");
            glEnableVertexAttribArray(position);glVertexAttribPointer(position,2,GL_FLOAT,GL_FALSE,sizeof(FloodVertex),nullptr);
            glEnableVertexAttribArray(world);glVertexAttribPointer(world,3,GL_FLOAT,GL_FALSE,sizeof(FloodVertex),reinterpret_cast<void*>(2*sizeof(float)));
        }
        floodShader->use();glBindVertexArray(floodVao);glBindBuffer(GL_ARRAY_BUFFER,floodVbo);
        for(auto& flood:floods){const auto& f=flood.request;flood.age+=dt;const bool done=flood.age>=f.duration;
            const double x=done?1:flood.age/f.duration,rad=done?1e9:f.reach*(f.easeOut?1-(1-x)*(1-x):x);
            std::vector<double> polygons(128),parts(7);int at=0,np=0;
            for(const auto& polygon:f.polygons){parts[np++]=at;for(const auto& p:polygon){polygons[at*2]=p.x;polygons[at*2+1]=p.y;++at;}}parts[np]=at;
            floodShader->uniform("uPoly",polygons);floodShader->uniform("uPart",parts);floodShader->uniform("uParts",np);
            floodShader->uniform("uY",std::vector<double>{f.y0,f.y1});floodShader->uniform("uC",std::vector<double>{f.center.x,f.center.z});
            floodShader->uniform("uR",std::vector<double>{flood.radius,rad});floodShader->uniform("uTeam",f.team);
            if(f.team<0)glDisable(GL_BLEND);else{glEnable(GL_BLEND);glBlendEquationSeparate(GL_FUNC_ADD,GL_MAX);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE);}
            glBufferData(GL_ARRAY_BUFFER,flood.vertices.size()*sizeof(FloodVertex),flood.vertices.data(),GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(flood.vertices.size()));flood.radius=rad;
        }
        floods.erase(std::remove_if(floods.begin(),floods.end(),[](const Flood& f){return f.age>=f.request.duration;}),floods.end());
    }
    void uploadInk(const StageData& stage,FacePaint& paint,double dt){
        if(paint.atlasSize()!=atlasSize)throw std::runtime_error("Source stage renderer requires a 4096×4096 FacePaint atlas");
        usedHeight=paint.usedHeight();ppm=paint.pixelsPerMetre();atlasRects.resize(stage.faces.size());for(std::size_t i=0;i<atlasRects.size();++i)atlasRects[i]=paint.atlas(static_cast<int>(i));
        const bool clear=paint.takeVisualClear();auto incoming=paint.takeVisualSplats();
        if(clear){clearInk();}
        for(auto& flood:paint.takeVisualFloods())queueFlood(stage,paint,std::move(flood));
        for(const auto& item:incoming){const int kind=static_cast<int>(item.kind);if(kind<0||kind>7)continue;const double duration=item.kind==SplatKind::Speck?.05:.085+std::min(.22,item.radius*.075);const bool canDrip=item.kind!=SplatKind::Speck&&item.kind!=SplatKind::Roll;growing.push_back({item,0,duration,canDrip?1.1+std::min(2.2,item.radius*1.5):0});}
        dt=std::clamp(dt,0.0,.25);clock+=dt;dryAcc+=dt;
        for(const auto& ripple:paint.takeVisualRipples())ripples.add(ripple,clock,rippleCamera);
        for(const auto& splat:incoming)if(!splat.cosmetic&&splat.radius>=0.15&&!ripples.near(splat.center,splat.radius,clock)){
            const double r=std::min(splat.radius,3.0);
            ripples.add({splat.center,0.0038+0.0036*r,0.1+0.05*r,0.85+0.35*r,0.55+0.2*r},clock,rippleCamera);
        }
        std::vector<PaintVertex> vertices;vertices.reserve(growing.size()*24*6);std::vector<std::size_t> expired;
        for(std::size_t i=0;i<growing.size();++i){auto& g=growing[i];
            const bool settle=std::any_of(floods.begin(),floods.end(),[&](const Flood& f){const auto& r=f.request;const auto& s=g.splat;const double reach=r.reach+s.radius*(g.dripDur?3.9:2.75)+.5;return std::hypot(s.center.x-r.center.x,s.center.z-r.center.z)<reach&&s.center.y>r.y0-4&&s.center.y<r.y1+4;});
            if(settle){appendSplat(stage,paint,g,false,3,1,vertices);expired.push_back(i);continue;}
            g.age+=dt;const double tn=g.age/g.dur,td=g.dripDur?std::min(1.0,g.age/g.dripDur):1.0,dT=1-std::pow(1-td,2.2);const bool bodyDone=tn>=1.75;
            if(bodyDone&&td>=1){appendSplat(stage,paint,g,g.dripDur>0,3,1,vertices);expired.push_back(i);continue;}
            appendSplat(stage,paint,g,bodyDone,std::min(tn,3.0),dT,vertices);
        }
        for(auto it=expired.rbegin();it!=expired.rend();++it)growing.erase(growing.begin()+static_cast<std::ptrdiff_t>(*it));
        const int n=static_cast<int>(std::floor(dryAcc*40));const int drySteps=n>=2?std::min(n,12):0;if(drySteps)dryAcc-=drySteps/40.0;
        if(vertices.empty()&&!drySteps&&floods.empty())return;
        GLStateGuard state;glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glViewport(0,0,atlasSize,atlasSize);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        if(!vertices.empty()){
            glEnable(GL_BLEND);glBlendEquationSeparate(GL_FUNC_ADD,GL_MAX);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE);
            glUseProgram(paintProgram);glBindVertexArray(paintVao);glBindBuffer(GL_ARRAY_BUFFER,paintVbo);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(PaintVertex),vertices.data(),GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()));
        }
        if(drySteps){
            const float yTop=std::min(1.f,((usedHeight+2.f)/atlasSize)*2.f-1.f);const float quad[]={-1,-1,1,-1,1,yTop,-1,yTop};
            glBindVertexArray(dryVao);glBindBuffer(GL_ARRAY_BUFFER,dryVbo);glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STREAM_DRAW);glEnable(GL_BLEND);
            glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT,GL_FUNC_ADD);glBlendFuncSeparate(GL_ONE,GL_ONE,GL_ZERO,GL_ONE);glUseProgram(dryProgram);glUniform1f(glGetUniformLocation(dryProgram,"uDry"),drySteps/255.f);glDrawArrays(GL_TRIANGLE_FAN,0,4);
        }
        drawFloods(dt);
        glDisable(GL_BLEND);glBindTexture(GL_TEXTURE_2D,atlas);glGenerateMipmap(GL_TEXTURE_2D);
    }
};

StageRenderer::StageRenderer(std::filesystem::path stageAssets,bool environment):stageAssets_(std::move(stageAssets)),environment_(environment) {
        const char* vs = R"GLSL(#version 450 core
layout(location=0) in vec3 p;
layout(location=1) in vec3 n;
layout(location=2) in vec3 c;
layout(location=3) in vec2 inkCoord;
uniform mat4 vp;
out vec3 normal;
out vec3 color;
out vec3 world;
out vec2 inkUV;
void main(){normal=n;color=c;world=p;inkUV=inkCoord;gl_Position=vp*vec4(p,1);}
)GLSL";
        const char* fs = R"GLSL(#version 450 core
in vec3 normal;
in vec3 color;
in vec3 world;
in vec2 inkUV;
out vec4 outColor;
uniform vec3 eye;
uniform usampler2D ownership;
uniform bool linearOutput;
uniform bool useSunShadow;
void main(){
    vec3 n=normalize(normal);
    vec3 l=normalize(vec3(-0.4,0.85,0.35));
    vec3 base=color;
    if(inkUV.x>=0.0){
        uint owner=texture(ownership,inkUV).r;
        if(owner==1u)base=pow(vec3(1.0,0.5412,0.0784),vec3(2.2));
        if(owner==2u)base=pow(vec3(0.1843,0.3569,1.0),vec3(2.2));
    }
    float shadow=useSunShadow?sampleSunShadow(world,n):1.0;
    vec3 linear=base*(0.42+0.58*max(dot(n,l),0.0)*mix(1.0,shadow,0.72));
    float fog=1-exp(-length(world-eye)*0.0015);
    linear=mix(linear,vec3(0.60,0.77,0.84),fog);
    vec3 srgb=mix(linear*12.92,1.055*pow(max(linear,vec3(0)),vec3(1.0/2.4))-0.055,step(vec3(0.0031308),linear));
    outColor=vec4(linearOutput?linear:srgb,1);
}
)GLSL";
        std::string fragmentSource=fs;
        const auto mainAt=fragmentSource.find("void main()");
        if(mainAt==std::string::npos)throw std::runtime_error("Stage fragment shader has no main");
        fragmentSource.insert(mainAt,DirectionalShadow::SamplingGLSL());
        const GLuint vertex = Compile(GL_VERTEX_SHADER, vs);
        GLuint fragment = 0;
        try { fragment = Compile(GL_FRAGMENT_SHADER, fragmentSource.c_str()); }
        catch (...) { glDeleteShader(vertex); throw; }
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[4096]{};
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            glDeleteProgram(program);
            program = 0;
            throw std::runtime_error(std::string("Stage shader link: ") + log);
        }
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        for (GLuint attr = 0; attr < 3; ++attr) {
            glEnableVertexAttribArray(attr);
            glVertexAttribPointer(attr, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex),
                reinterpret_cast<void*>(static_cast<std::size_t>(attr) * sizeof(glm::vec3)));
        }
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,sizeof(RenderVertex),reinterpret_cast<void*>(offsetof(RenderVertex,inkUV)));
        glBindVertexArray(0);
        glGenTextures(1,&ink);
        glBindTexture(GL_TEXTURE_2D,ink);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_R8UI,4096,4096,0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,nullptr);
    }
StageRenderer::~StageRenderer() {
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(program);
        glDeleteTextures(1,&ink);
    }
void StageRenderer::Load(const StageData& stage, int display, const FacePaint& paint) {
        GLStateGuard state;
        display_=display;
        if(display==0){
            if(!source_)source_=std::make_unique<Source>(stageAssets_,environment_);
            source_->load(stage);
            vertices=0;
            uploadedStage=nullptr;uploadedGrid.clear();
            return;
        }
        std::vector<RenderVertex> data;
        data.reserve(stage.faces.size() * 6);
        if(display==3) {
            for(const auto& b:stage.blocks) {
                if(!b.solid) continue;
                const glm::vec3 color=b.rail?glm::vec3(0.55f,0.1f,0.8f):
                    b.hidden?glm::vec3(0.8f,0.18f,0.08f):glm::vec3(0.25f,0.45f,0.5f);
                for(int axis=0;axis<3;++axis) for(double sign:{-1.0,1.0}) {
                    const auto normal=b.axes[axis]*sign;
                    const auto u=b.axes[(axis+1)%3]*b.half[(axis+1)%3];
                    const auto v=b.axes[(axis+2)%3]*b.half[(axis+2)%3];
                    const auto middle=b.center+normal*b.half[axis];
                    const auto a=middle-u-v, c=middle+u+v;
                    for(const auto& p:{a,middle+u-v,c,a,c,middle-u+v})
                        data.push_back({glm::vec3(p),glm::vec3(normal),color,glm::vec2(-1)});
                }
            }
        }
        for (int id=0;id<static_cast<int>(stage.faces.size());++id) {
            if(display==3) break;
            const auto& f=stage.faces[id];
            glm::vec3 color(f.color);
            if (display == 1) color = f.paintable ? glm::vec3(0.05f,0.65f,0.4f) : glm::vec3(0.45f,0.13f,0.14f);
            if (display == 2) color = f.paintable && f.turf ? glm::vec3(0.05f,0.5f,0.95f) : glm::vec3(0.23f);
            const glm::dvec3 a = f.origin, b = a + f.u * f.width;
            const glm::dvec3 d = a + f.v * f.height, c = b + f.v * f.height;
            const auto& rect=paint.atlas(id);
            for (const auto& p : {a,b,c,a,c,d}) {
                glm::vec2 uv(-1.0f);
                if(f.paintable) {
                    const auto local=p-f.origin;
                    uv=glm::vec2(glm::dvec2(rect.x+rect.pad+glm::dot(local,f.u)*rect.ppm,
                                           rect.y+rect.pad+glm::dot(local,f.v)*rect.ppm)/4096.0);
                }
                data.push_back({glm::vec3(p),glm::vec3(f.normal),color,uv});
            }
        }
        vertices = static_cast<GLsizei>(data.size());
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(RenderVertex), data.data(), GL_STATIC_DRAW);
    }
void StageRenderer::UploadInk(const StageData& stage,FacePaint& paint,double dt) {
        // Diagnostic ownership cells, not the final animated GPU ink material.
        GLStateGuard state;
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,ink);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        const auto& grid=paint.grid();
        if(uploadedStage!=&stage || uploadedGrid.size()!=grid.size()) {
            const unsigned char zero=0;
            glClearTexImage(ink,0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,&zero);
            uploadedGrid.assign(grid.size(),0);uploadedStage=&stage;
        }
        for(int id=0;id<static_cast<int>(stage.faces.size());++id) {
            const auto& f=stage.faces[id];
            if(!f.paintable) continue;
            const auto& r=paint.atlas(id);
            const auto& g=paint.faceGrid(id);
            if(g.offset<0) continue;
            int left=g.columns, right=-1, bottom=g.rows, top=-1;
            for(int v=0;v<g.rows;++v) for(int u=0;u<g.columns;++u) {
                const auto cell=g.offset+v*g.columns+u;
                if(grid[cell]==uploadedGrid[cell]) continue;
                left=std::min(left,u);right=std::max(right,u);bottom=std::min(bottom,v);top=std::max(top,v);
            }
            if(right<0) continue;
            // Edge cells also own the atlas gutter; update just the changed cell rectangle.
            const int x0=left==0?r.x:std::max(r.x,static_cast<int>(std::floor(r.x+r.pad+left*g.cellWidth*r.ppm))-1);
            const int y0=bottom==0?r.y:std::max(r.y,static_cast<int>(std::floor(r.y+r.pad+bottom*g.cellHeight*r.ppm))-1);
            const int x1=right==g.columns-1?r.x+r.width:std::min(r.x+r.width,static_cast<int>(std::ceil(r.x+r.pad+(right+1)*g.cellWidth*r.ppm))+1);
            const int y1=top==g.rows-1?r.y+r.height:std::min(r.y+r.height,static_cast<int>(std::ceil(r.y+r.pad+(top+1)*g.cellHeight*r.ppm))+1);
            std::vector<unsigned char> pixels(static_cast<std::size_t>(x1-x0)*(y1-y0));
            for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) {
                const double u=(x+0.5-r.x-r.pad)/r.ppm;
                const double v=(y+0.5-r.y-r.pad)/r.ppm;
                pixels[static_cast<std::size_t>(y-y0)*(x1-x0)+x-x0]=static_cast<unsigned char>(paint.sample(id,u,v));
            }
            glTexSubImage2D(GL_TEXTURE_2D,0,x0,y0,x1-x0,y1-y0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,pixels.data());
        }
        uploadedGrid=grid;
        const std::string requested=std::string(stage.id)+"-"+std::string(stage.mode);
        if(source_&&source_->key==requested)source_->uploadInk(stage,paint,dt);
    }
void StageRenderer::UpdateSwimWake(double dt,const CombatWorld& world,glm::dvec3 camera) {
        if(display_!=0||!source_)return;
        std::vector<SwimWakeActor> actors;actors.reserve(world.actors().size());
        for(const auto* actor:world.actors()) {
            if(!actor)continue;
            const auto& state=actor->state;const auto special=actor->specialPose();
            const bool squidForm=state.squid&&!special.body&&!special.noSquid;
            actors.push_back({actor,state.pos,state.vel,state.alive,actor->isLocal(),
                squidForm&&state.submerged,squidForm&&state.climbing});
        }
        source_->rippleCamera=camera;
        source_->swimWake.update(dt,source_->clock,actors,camera);
    }
glm::vec4 StageRenderer::ReadVisualInk(int faceId,double u,double v) const {
        if(!source_||faceId<0||static_cast<std::size_t>(faceId)>=source_->atlasRects.size())return glm::vec4(0.0f);
        const auto& r=source_->atlasRects[faceId];if(r.x<0||r.width<=0||r.height<=0||r.ppm<=0)return glm::vec4(0.0f);
        const int x=std::clamp(static_cast<int>(std::floor(r.x+r.pad+u*r.ppm)),r.x,r.x+r.width-1);
        const int y=std::clamp(static_cast<int>(std::floor(r.y+r.pad+v*r.ppm)),r.y,r.y+r.height-1);
        unsigned char pixel[4]{};GLStateGuard state;glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glGetTextureSubImage(source_->atlas,0,x,y,0,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,sizeof(pixel),pixel);
        return glm::vec4(pixel[0],pixel[1],pixel[2],pixel[3])/255.0f;
    }
void StageRenderer::VerifyInk(const StageData& stage,const FacePaint& paint) const {
        GLStateGuard state;glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
        std::vector<unsigned char> pixels(4096*4096);
        glBindTexture(GL_TEXTURE_2D,ink);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,pixels.data());
        for(int id=0;id<static_cast<int>(stage.faces.size());++id) {
            if(!stage.faces[id].paintable) continue;
            const auto& r=paint.atlas(id);
            for(int y=r.y;y<r.y+r.height;++y) for(int x=r.x;x<r.x+r.width;++x)
                if(pixels[static_cast<std::size_t>(y)*4096+x]!=paint.sample(id,(x+0.5-r.x-r.pad)/r.ppm,(y+0.5-r.y-r.pad)/r.ppm))
                    throw std::runtime_error("GPU ownership upload differs from CPU grid");
        }
        std::cout<<"INK_UPLOAD verified\n";
    }
int StageRenderer::DrawShadow(const glm::mat4& lightViewProjection) {
        if(display_==0&&source_&&!source_->meshes.empty())return source_->drawShadow(glm::dmat4(lightViewProjection));
        if(vertices<=0)return 0;
        GLboolean previousMask[4]{};glGetBooleanv(GL_COLOR_WRITEMASK,previousMask);
        const GLboolean depthTest=glIsEnabled(GL_DEPTH_TEST);
        GLboolean depthMask=GL_TRUE;glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
        glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,ink);
        glUniform1i(glGetUniformLocation(program,"ownership"),0);
        glUniformMatrix4fv(glGetUniformLocation(program,"vp"),1,GL_FALSE,&lightViewProjection[0][0]);
        const glm::vec3 origin(0.0f);glUniform3fv(glGetUniformLocation(program,"eye"),1,&origin[0]);
        glUniform1i(glGetUniformLocation(program,"linearOutput"),GL_TRUE);
        glUniform1i(glGetUniformLocation(program,"useSunShadow"),GL_FALSE);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,vertices);glBindVertexArray(0);
        glColorMask(previousMask[0],previousMask[1],previousMask[2],previousMask[3]);
        glDepthMask(depthMask);if(!depthTest)glDisable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        return 1;
}
int StageRenderer::Draw(const glm::mat4& vp, const glm::vec3& eye, bool wireframe,bool linearOutput,const DirectionalShadow* shadow) {
        if(display_==0&&source_&&!source_->meshes.empty()){
            CharacterLightState lights;lights.ambient=glm::vec3(0.32f);lights.sunDirection=glm::normalize(glm::vec3(-0.41f,0.83f,-0.38f));
            lights.sunColor=glm::vec3(1.7f);lights.skyDirection=glm::vec3(0,1,0);lights.skyColor=glm::vec3(0.42f,0.55f,0.68f);
            lights.groundColor=glm::vec3(0.22f,0.19f,0.16f);lights.exposure=1.0f;
            // The legacy inspector supplies P*V only. Treat world coordinates as view coordinates so clip space
            // remains identical and the source BRDF still receives a consistent world-space camera and sun.
            return source_->draw(glm::dmat4(1.0),glm::dmat4(vp),glm::dvec3(eye),lights,wireframe,shadow);
        }
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,ink);
        glUniform1i(glGetUniformLocation(program,"ownership"),0);
        glUniformMatrix4fv(glGetUniformLocation(program, "vp"), 1, GL_FALSE, &vp[0][0]);
        glUniform3fv(glGetUniformLocation(program,"eye"),1,&eye[0]);
        glUniform1i(glGetUniformLocation(program,"linearOutput"),linearOutput);
        const bool useShadow=shadow&&shadow->texture()!=0;
        glUniform1i(glGetUniformLocation(program,"useSunShadow"),useShadow?GL_TRUE:GL_FALSE);
        if(useShadow){
            glActiveTexture(GL_TEXTURE8);glBindTexture(GL_TEXTURE_2D,shadow->texture());
            glUniform1i(glGetUniformLocation(program,"sunShadow"),8);
            const glm::mat4 matrix(shadow->matrix());
            glUniformMatrix4fv(glGetUniformLocation(program,"sunShadowMatrix"),1,GL_FALSE,&matrix[0][0]);
            glUniform1f(glGetUniformLocation(program,"sunShadowBias"),static_cast<float>(shadow->bias()));
            glUniform1f(glGetUniformLocation(program,"sunShadowNormalBias"),static_cast<float>(shadow->normalBias()));
            glUniform1f(glGetUniformLocation(program,"sunShadowRadius"),static_cast<float>(shadow->radius()));
        }
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, vertices);
        glBindVertexArray(0);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        return vertices > 0 ? 1 : 0;
    }
int StageRenderer::Draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& lights,
                        bool wireframe,const DirectionalShadow* shadow,bool reflection) {
        if(display_==0&&source_&&!source_->meshes.empty())return source_->draw(view,projection,eye,lights,wireframe,shadow,reflection);
        const glm::mat4 vp=glm::mat4(projection*view);const glm::vec3 camera(eye);
        return Draw(vp,camera,wireframe,true,shadow);
    }
void StageRenderer::LoadActors(const PlayerSimulation& player) {
        std::vector<RenderVertex> data;
        auto capsule=[&](glm::dvec3 feet,double radius,double height,glm::vec3 color) {
            // shortcut: collision silhouettes until the reference skinned character renderer is ported.
            auto point=[&](int ring,int segment) {
                const double lat=-glm::pi<double>()/2+glm::pi<double>()*ring/8;
                const double lon=glm::two_pi<double>()*segment/12;
                const glm::dvec3 n(std::cos(lat)*std::sin(lon),std::sin(lat),std::cos(lat)*std::cos(lon));
                const double mid=lat<0?radius:std::max(radius,height-radius);
                return RenderVertex{glm::vec3(feet+glm::dvec3(0,mid,0)+n*radius),glm::vec3(n),color,glm::vec2(-1)};
            };
            for(int r=0;r<8;++r) for(int s=0;s<12;++s) {
                const auto a=point(r,s), b=point(r,s+1), c=point(r+1,s+1), d=point(r+1,s);
                data.insert(data.end(),{a,b,c,a,c,d});
            }
        };
        if(player.state.alive) capsule(player.visualPos(),0.38,player.state.squid?0.55:1.45,glm::vec3(1,0.26f,0.01f));
        for(const auto& p:player.projectiles()) capsule(p.pos-glm::dvec3(0,0.09,0),0.09,0.18,glm::vec3(1,0.38f,0.01f));
        vertices=static_cast<GLsizei>(data.size());
        glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,data.size()*sizeof(RenderVertex),data.data(),GL_STREAM_DRAW);
    }
} // namespace restoration
