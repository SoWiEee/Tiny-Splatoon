#include "EnvironmentRenderer.hpp"
#include "EnvironmentTextureCatalog.hpp"
#include "ReferenceData.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <zlib.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {
using restoration::environment_detail::TextureDescriptor;
using restoration::environment_detail::decodeTextureBlob;
using restoration::environment_detail::parseTextureCatalog;
using restoration::environment_detail::readTextureBlob;
using restoration::environment_detail::readTextureCatalog;

void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void requireFailure(F&& action,const char* message){try{action();}catch(const std::exception&){return;}throw std::runtime_error(message);}

std::string catalogFixture(std::string_view waveEntry=R"({"width":1,"height":1,"bits":8})",std::string_view omit={}){
    constexpr std::array<std::string_view,7> files{{"wave.rgba8.z","cloud-day.rgba16f.z","cloud-sunset.rgba16f.z","cloud-golden.rgba16f.z","pmrem-day.rgba16f.z","pmrem-sunset.rgba16f.z","pmrem-golden.rgba16f.z"}};
    std::string json="{";bool first=true;
    for(const auto file:files){if(file==omit)continue;if(!first)json+=',';first=false;json+='\"';json+=file;json+="\":";json+=file=="wave.rgba8.z"?waveEntry:(file.find("rgba16f")!=std::string_view::npos?R"({"width":1,"height":1,"bits":16})":R"({"width":1,"height":1,"bits":8})");}
    return json+"}";
}

void writeWord(std::vector<unsigned char>& bytes,std::size_t offset,std::uint32_t value){for(unsigned i=0;i<4;i++)bytes[offset+i]=static_cast<unsigned char>(value>>(i*8));}
std::vector<unsigned char> compressedTexture(unsigned width,unsigned height,unsigned bits,std::size_t payloadBytes,std::string_view magic="TSENV001"){
    std::vector<unsigned char> raw(20+payloadBytes,0x5a);std::copy_n(magic.data(),8,raw.begin());
    writeWord(raw,8,width);writeWord(raw,12,height);writeWord(raw,16,bits);
    uLongf size=compressBound(static_cast<uLong>(raw.size()));std::vector<unsigned char> result(size);
    if(compress2(result.data(),&size,raw.data(),static_cast<uLong>(raw.size()),Z_BEST_SPEED)!=Z_OK)throw std::runtime_error("Could not build test environment texture");
    result.resize(size);return result;
}

void testTextureCatalog(const std::filesystem::path& assets){
    const auto catalog=readTextureCatalog(assets);const auto wave=catalog.at("wave.rgba8.z");
    require(wave.width==256&&wave.height==256&&wave.bits==8,"Environment texture catalog dimensions changed");
    const auto pixels=readTextureBlob(assets/"wave.rgba8.z",wave);
    require(pixels.size()==256u*256u*4u,"Environment texture payload size changed");

    requireFailure([]{parseTextureCatalog(catalogFixture(R"({"width":1,"height":1,"bits":8})","cloud-golden.rgba16f.z"));},"Missing environment texture descriptor was accepted");
    requireFailure([]{parseTextureCatalog("{");},"Malformed environment texture JSON was accepted");
    for(const auto invalid:{
        std::string_view(R"({"width":"1","height":1,"bits":8})"),
        std::string_view(R"({"width":1.5,"height":1,"bits":8})"),
        std::string_view(R"({"width":0,"height":1,"bits":8})"),
        std::string_view(R"({"width":1,"height":1,"bits":"8"})"),
        std::string_view(R"({"width":1,"height":1,"bits":32})")})
        requireFailure([&]{parseTextureCatalog(catalogFixture(invalid));},"Malformed environment texture descriptor was accepted");

    const TextureDescriptor tiny{3,2,8};
    const auto blob=compressedTexture(3,2,8,24);
    require(decodeTextureBlob(blob,tiny,"fixture").size()==24,"Valid environment texture blob was rejected");
    requireFailure([&]{decodeTextureBlob(compressedTexture(3,2,8,23),tiny,"short fixture");},"Short environment texture payload was accepted");
    requireFailure([&]{decodeTextureBlob(compressedTexture(3,2,8,25),tiny,"long fixture");},"Long environment texture payload was accepted");
    requireFailure([&]{decodeTextureBlob(compressedTexture(2,3,8,24),tiny,"mismatched fixture");},"Mismatched environment texture header was accepted");
    requireFailure([&]{decodeTextureBlob(compressedTexture(3,2,8,24,"BADMAGIC"),tiny,"invalid magic fixture");},"Invalid environment texture magic was accepted");
    requireFailure([&]{decodeTextureBlob(compressedTexture(3,2,16,24),tiny,"mismatched bit depth fixture");},"Mismatched environment texture bit depth was accepted");
    requireFailure([&]{readTextureBlob(assets/"missing-texture-for-test.z",wave);},"Missing environment texture blob was accepted");
}
}

int main(int argc,char** argv){
    const std::filesystem::path assets=argc>1?argv[1]:"assets/world/environment";
    try{testTextureCatalog(assets);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    if(!glfwInit())return 1;glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(384,256,"Source environment pixels",nullptr,nullptr);if(!window)return 1;glfwMakeContextCurrent(window);if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))return 1;int result=0;
    try{
        using namespace restoration;const std::filesystem::path output=argc>2?argv[2]:"build/captures/environment";std::filesystem::create_directories(output);
        EnvironmentRenderer env(assets);std::set<std::string> stages;std::size_t motionPixels=0;
        GLuint sentinel=0;glGenRenderbuffers(1,&sentinel);glBindRenderbuffer(GL_RENDERBUFFER,sentinel);
        for(const auto& stage:ReferenceStages()){
            if(stage.mode!="turf"||!stages.insert(std::string(stage.id)).second)continue;env.Load(stage);
            const auto lights=env.Lights();if(!lights.environmentTexture||lights.environmentIntensity<=0||glm::length(lights.sunDirection)<.99||lights.fogFar<=lights.fogNear)throw std::runtime_error("Invalid source environment light state");
            const glm::vec3 eye(0,5,90);const auto view=glm::lookAt(eye,glm::vec3(0,2,0),glm::vec3(0,1,0)),projection=glm::perspective(glm::radians(55.f),1.5f,.15f,6500.f);
            std::vector<unsigned char> before,after;int reflected=0;
            for(int frame=0;frame<2;frame++){
                if(frame)for(int i=0;i<25;i++)env.Update(.1);
                glViewport(0,0,384,256);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                reflected=env.RenderReflection(view,projection,eye,384,256,[&](const auto&,const auto&,const auto&){return 0;});
                if(env.DrawSea(view,projection,eye)<1||env.DrawScenery(view,projection,eye)<1||env.DrawSky(view,projection,eye)<1)throw std::runtime_error("Source environment draw path empty");
                std::vector<unsigned char> pixels(384*256*4);glReadPixels(0,0,384,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());if(frame)after=pixels;else before=pixels;
                if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Environment GL error in "+std::string(stage.id));
                if(frame){stbi_flip_vertically_on_write(1);const auto file=output/(std::string(stage.id)+".png");if(!stbi_write_png(file.string().c_str(),384,256,4,pixels.data(),384*4))throw std::runtime_error("Cannot save environment evidence");}
            }
            std::size_t changed=0,colored=0;for(std::size_t i=0;i<before.size();i+=4){if(std::abs(int(before[i])-int(after[i]))+std::abs(int(before[i+1])-int(after[i+1]))+std::abs(int(before[i+2])-int(after[i+2]))>8)changed++;if(after[i]+after[i+1]+after[i+2]>40)colored++;}
            if(changed<50||colored<20000)throw std::runtime_error("Static/blank environment "+std::string(stage.id)+" changed="+std::to_string(changed));
            if(stage.water=="marina"&&reflected<=0)throw std::runtime_error("Missing marina reflection path");motionPixels+=changed;
            GLint restored=0;glGetIntegerv(GL_RENDERBUFFER_BINDING,&restored);if(restored!=GLint(sentinel))throw std::runtime_error("Environment reflection leaked renderbuffer binding");
            std::cout<<"ENVIRONMENT_MAP "<<stage.id<<" changed="<<changed<<" reflections="<<reflected<<'\n';
        }
        glDeleteRenderbuffers(1,&sentinel);
        if(stages.size()!=8)throw std::runtime_error("Missing source environment map variant");std::cout<<"ENVIRONMENT_GPU maps="<<stages.size()<<" motion_pixels="<<motionPixels<<" GPU="<<glGetString(GL_RENDERER)<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
