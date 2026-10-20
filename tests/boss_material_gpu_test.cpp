#include "BossRenderer.hpp"
#include "MatchSimulation.hpp"
#include "ReferenceData.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Pixels=std::vector<unsigned char>;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
const restoration::StageData& bossStage(){
    for(const auto& stage:restoration::ReferenceStages())if(stage.id=="tidewater"&&stage.mode=="turf"&&!stage.onlineOnly)return stage;
    throw std::runtime_error("Missing legal offline Boss stage");
}
Pixels render(restoration::BossRenderer& renderer,const restoration::BossSimulation& boss,const glm::mat4& view,const glm::mat4& projection,glm::vec3 camera){
    glViewport(0,0,320,320);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glClearColor(.03f,.04f,.05f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    require(renderer.Draw(boss,view,projection,camera,glm::vec3(.02f,.08f,1))>=5,"Source Boss materials did not draw all hull buckets");
    Pixels pixels(320*320*4);glReadPixels(0,0,320,320,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    require(glGetError()==GL_NO_ERROR,"Source Boss material draw generated an OpenGL error");return pixels;
}
}

int main(int argc,char** argv){
    if(argc!=2||!glfwInit())return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(320,320,"Source Boss material pixels",nullptr,nullptr);
    if(!window){glfwTerminate();return 1;}glfwMakeContextCurrent(window);
    if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))){glfwDestroyWindow(window);glfwTerminate();return 1;}
    int result=0;
    try{
        using namespace restoration;
        const auto& stage=bossStage();StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);NavGraph nav(stage,geometry);
        MatchOptions options;options.mode="boss";options.duration=300;options.seed=7;MatchSimulation match(stage,geometry,paint,nav,options);
        BossRenderer renderer(argv[1]);match.setBossPoseSink([&](BossSimulation& boss){renderer.Update(boss);});match.startPlaying();
        for(int i=0;i<20&&!match.boss()->state().visible;++i)match.tick(.1);
        require(match.boss()->state().visible,"Boss did not reach its renderable intro state");
        const auto state=match.boss()->state();const glm::dvec3 eye=state.pos+glm::dvec3(18,12,24),target=state.pos+glm::dvec3(0,2.5,0);
        const auto view=glm::lookAt(eye,target,glm::dvec3(0,1,0));const auto projection=glm::perspective(glm::radians(45.0),1.0,.1,100.0);
        const auto baseline=render(renderer,*match.boss(),glm::mat4(view),glm::mat4(projection),glm::vec3(eye));
        match.paused=true;BossSegmentHit hit;require(match.boss()->hit(match.local(),1.0,hit,"shooter")>0,"Boss test hit was not accepted");renderer.Update(*match.boss());
        const auto flashed=render(renderer,*match.boss(),glm::mat4(view),glm::mat4(projection),glm::vec3(eye));
        std::size_t visible=0,changed=0;std::uint64_t difference=0;
        for(std::size_t i=0;i<baseline.size();i+=4){if(baseline[i]>24||baseline[i+1]>24||baseline[i+2]>24)++visible;bool pixelChanged=false;for(std::size_t c=0;c<3;++c){const auto delta=std::abs(int(baseline[i+c])-int(flashed[i+c]));difference+=static_cast<std::uint64_t>(delta);pixelChanged|=delta>2;}if(pixelChanged)++changed;}
        require(visible>250,"Source Boss render changed too few pixels");require(changed>80&&difference>4000,"Live Boss material uniforms did not change rendered pixels");
        std::cout<<"BOSS_MATERIAL_GPU source_shaders=6 visible_pixels="<<visible<<" flash_changed_pixels="<<changed<<" rgb_delta="<<difference<<" renderer="<<glGetString(GL_RENDERER)<<'\n';
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
