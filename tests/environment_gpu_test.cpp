#include "EnvironmentRenderer.hpp"
#include "ReferenceData.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
int main(int argc,char** argv){
    if(!glfwInit())return 1;glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(384,256,"Source environment pixels",nullptr,nullptr);if(!window)return 1;glfwMakeContextCurrent(window);if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))return 1;int result=0;
    try{
        using namespace restoration;const std::filesystem::path assets=argc>1?argv[1]:"assets/reference/environment",output=argc>2?argv[2]:"build/captures/environment";std::filesystem::create_directories(output);
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
