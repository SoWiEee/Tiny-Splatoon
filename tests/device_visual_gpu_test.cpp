#include "DeviceRenderer.hpp"
#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"
#include "ArsenalData.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <set>
#include <map>
#include <sstream>
#include <stdexcept>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
int main(int argc,char** argv){
    if(!glfwInit())return 1;glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(256,256,"Source device pixels",nullptr,nullptr);if(!window)return 1;glfwMakeContextCurrent(window);if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))return 1;int result=0;
    try{
        using namespace restoration;const std::filesystem::path assets=argc>1?argv[1]:"assets/reference/devices";
        const std::filesystem::path output=argc>2?argv[2]:"build/captures/devices";std::filesystem::create_directories(output);
        DeviceRenderer renderer(assets);CharacterLightState lights;lights.ambient={.3,.3,.3};lights.sunDirection=glm::normalize(glm::vec3(-.4,.8,.3));lights.sunColor={2,1.8,1.6};lights.skyColor={.4,.5,.65};lights.groundColor={.25,.2,.16};
        const glm::dvec3 eye(3,2.6,4);const auto view=glm::lookAt(eye,glm::dvec3(0,.5,0),glm::dvec3(0,1,0)),projection=glm::perspective(glm::radians(48.),1.,.05,100.);
        std::ifstream catalog(assets/"models.txt");std::string line;std::vector<std::pair<std::string,int>> models;
        while(std::getline(catalog,line)){std::istringstream row(line);std::string kind;int team;unsigned count;if(!(row>>kind>>team>>count))throw std::runtime_error("Invalid test catalog");models.emplace_back(kind,team);for(unsigned i=0;i<count;i++)if(!std::getline(catalog,line))throw std::runtime_error("Truncated test catalog");}
        std::size_t pixelsTotal=0;std::set<std::string> kinds,small;std::map<std::string,std::vector<unsigned char>> orange;
        for(const auto& sub:SubEquipment())small.insert(std::string(sub.id));
        for(const auto& [kind,team]:models){
            glViewport(0,0,256,256);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            const auto model=small.count(kind)?glm::scale(glm::dmat4(1),glm::dvec3(8)):glm::dmat4(1);
            const int draws=renderer.DrawModel(kind,team,model,.75,view,projection,eye,lights,false,1,1);
            std::vector<unsigned char> pixels(256*256*4);glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());std::size_t colored=0;for(std::size_t i=0;i<pixels.size();i+=4)if(pixels[i]>12||pixels[i+1]>12||pixels[i+2]>12)colored++;
            const auto error=glGetError();if(draws<1||colored<(small.count(kind)?40:8)||error!=GL_NO_ERROR)throw std::runtime_error("Invisible/invalid source device "+kind+" team="+std::to_string(team)+" pixels="+std::to_string(colored)+" GL="+std::to_string(error));
            if(kind=="bomb"||kind=="sticky"||kind=="waddle"||kind=="torpedo"||kind=="boomerang"||kind=="stormBase"){
                if(team==0)orange[kind]=pixels;
                else {std::size_t different=0;const auto& first=orange.at(kind);for(std::size_t p=0;p<pixels.size();p+=4)if(std::abs(int(first[p])-pixels[p])+std::abs(int(first[p+2])-pixels[p+2])>12)different++;if(different<8)throw std::runtime_error("Source team material parameters collapsed: "+kind);}
            }
            if(kind=="bomb"){std::size_t pigment=0;for(std::size_t p=0;p<pixels.size();p+=4)if(team==0?pixels[p]>pixels[p+2]+20:pixels[p+2]>pixels[p]+20)pigment++;if(pigment<8)throw std::runtime_error("Source bomb team pigment is missing or reversed");}
            pixelsTotal+=colored;kinds.insert(kind);stbi_flip_vertically_on_write(1);const auto path=output/(kind+"-"+std::to_string(team)+".png");if(!stbi_write_png(path.string().c_str(),256,256,4,pixels.data(),256*4))throw std::runtime_error("Cannot save device evidence");
        }
        StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};StageGeometry geometry(stage);FacePaint paint(stage,geometry);CombatWorld world(stage,geometry,paint);PlayerSimulation actor(stage,geometry,paint,0,&world);
        for(const auto& sub:SubEquipment()){const std::string id(sub.id);actor.setLoadout("shooter",id);actor.state.ink=100;world.deploy(actor,id);world.tick(1./60);renderer.Update(1./60,world,eye);renderer.Draw(view,projection,eye,lights,{});world.clear();renderer.Update(1./60,world,eye);if(renderer.Count()!=0||renderer.Draw(view,projection,eye,lights,{})!=0)throw std::runtime_error("Device survives world clear: "+id);}
        actor.setLoadout("shooter","tracer");actor.state.ink=100;world.deploy(actor,"tracer");
        for(int frame=0;frame<8&&renderer.TracerRibbonSegmentCount()==0;frame++){world.tick(1./60);renderer.Update(1./60,world,eye);}
        if(renderer.TracerRibbonSegmentCount()==0)throw std::runtime_error("Tracer source trail did not create ribbon segments while the bolt was alive");
        glViewport(0,0,256,256);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        if(renderer.Draw(view,projection,eye,lights,{})<1||glGetError()!=GL_NO_ERROR)throw std::runtime_error("Tracer source ribbon did not submit cleanly");
        std::vector<unsigned char> ribbonPixels(256*256*4);glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,ribbonPixels.data());std::size_t ribbonVisible=0;
        for(std::size_t i=0;i<ribbonPixels.size();i+=4)if(ribbonPixels[i]>12||ribbonPixels[i+1]>12||ribbonPixels[i+2]>12)ribbonVisible++;
        if(ribbonVisible<3)throw std::runtime_error("Tracer source ribbon produced no visible camera-facing pixels");
        const double trailLife=GetEquipment(EquipmentCategory::Sub,"tracer").number("trailLife",1.0);
        for(int frame=0;frame<180&&!world.devices().empty();frame++){world.tick(1./60);renderer.Update(1./60,world,eye);}
        if(!world.devices().empty()||renderer.Count()!=0||renderer.TracerTailCount()==0)throw std::runtime_error("Tracer tail did not outlive its source bolt");
        for(int frame=0;frame<static_cast<int>(std::ceil(trailLife*60))+4;frame++){world.tick(1./60);renderer.Update(1./60,world,eye);}
        if(renderer.TracerTailCount()!=0||renderer.TracerRibbonSegmentCount()!=0)throw std::runtime_error("Tracer ribbon tail was not cleaned up after its source lifetime");
        if(kinds.size()<29||glGetError()!=GL_NO_ERROR)throw std::runtime_error("Incomplete source device matrix or GL error");
        std::cout<<"DEVICE_GPU models="<<models.size()<<" kinds="<<kinds.size()<<" visible_pixels="<<pixelsTotal<<" tracer_ribbon_pixels="<<ribbonVisible<<" tail=created,expired GPU="<<glGetString(GL_RENDERER)<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
