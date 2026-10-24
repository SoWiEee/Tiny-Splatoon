#include "ProjectileRenderer.hpp"
#include "CharacterShader.hpp"
#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

int main(int argc,char** argv){
    if(!glfwInit())return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(256,256,"Source projectile pixels",nullptr,nullptr);
    if(!window){glfwTerminate();return 1;}glfwMakeContextCurrent(window);
    if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))return 1;
    int result=0;
    try{
        using namespace restoration;
        StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
        StageGeometry geometry(stage);FacePaint paint(stage,geometry);CombatWorld world(stage,geometry,paint);PlayerSimulation actor(stage,geometry,paint,0,&world);actor.state.alive=true;
        ShooterProjectile shot;shot.owner=&actor;shot.pos={0,0,0};shot.start={-3,0,0};shot.vel={28,0,2};shot.age=.12;shot.seed=.31;shot.look={.19,.8,1.3,.035,26,.3,3};world.launch(shot);
        ProjectileRenderer renderer(argc>1?argv[1]:"assets/reference/projectiles");renderer.Update(1.0/60,world,{});
        if(renderer.blobs().size()!=4)throw std::runtime_error("Shot plus satellite draw instances missing");
        CharacterLightState lights;lights.ambient={.3,.3,.3};lights.sunDirection=glm::normalize(glm::vec3(-.4,.85,.35));lights.sunColor={1,.92,.82};
        const glm::dvec3 eye(0,.5,4);const auto view=glm::lookAt(eye,glm::dvec3(0),glm::dvec3(0,1,0));const auto projection=glm::perspective(glm::radians(45.0),1.0,.05,50.0);
        glViewport(0,0,256,256);glEnable(GL_DEPTH_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        if(renderer.Draw(view,projection,eye,lights)!=1)throw std::runtime_error("No source instanced projectile draw");
        std::vector<unsigned char> pixels(256*256*4);glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        std::size_t colored=0;for(std::size_t i=0;i<pixels.size();i+=4)if(pixels[i]>20||pixels[i+1]>20||pixels[i+2]>20)++colored;
        const auto drawError=glGetError();
        if(drawError!=GL_NO_ERROR)throw std::runtime_error("Source projectile draw GL error "+std::to_string(drawError));
        if(colored<60)throw std::runtime_error("Source projectile draw changed too few pixels: "+std::to_string(colored));
        if(argc>2){std::filesystem::create_directories(std::filesystem::path(argv[2]).parent_path());stbi_flip_vertically_on_write(1);if(!stbi_write_png(argv[2],256,256,4,pixels.data(),256*4))throw std::runtime_error("Cannot save projectile evidence");}
        renderer.Update(.01,world,{{"beam",{-1,0,0},{1,0,0},2,0,.8,.17,.4}});
        if(renderer.ribbons()!=1||renderer.Draw(view,projection,eye,lights)!=2)throw std::runtime_error("Source charger ribbon absent");
        renderer.Update(1,world,{});if(renderer.ribbons()!=0)throw std::runtime_error("Charger ribbon did not expire");
        for(const auto* kind:{"arrow","fist","bladewave"}){
            world.clear();auto custom=shot;custom.kind=kind;custom.weaponId=kind;custom.flightDir=glm::normalize(custom.vel);custom.distance=3;custom.tier=2;
            world.launch(custom);renderer.Update(0,world,{});
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);const auto calls=renderer.Draw(view,projection,eye,lights);
            glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            std::size_t visible=0;for(std::size_t i=0;i<pixels.size();i+=4)if(pixels[i]>20||pixels[i+1]>20||pixels[i+2]>20)++visible;
            if(calls<1||visible<8||glGetError()!=GL_NO_ERROR)throw std::runtime_error(std::string("Source kit projectile invisible or invalid: ")+kind);
            if(argc>2){const auto path=std::filesystem::path(argv[2]).parent_path()/(std::string("projectile-")+kind+".png");stbi_write_png(path.string().c_str(),256,256,4,pixels.data(),256*4);}
        }
        StageData wallStage{};wallStage.bounds={-30,30,-30,30};wallStage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
        BlockData wall{};wall.center={0,0,.5};wall.half={3,3,.1};wall.axes={glm::dvec3(1,0,0),glm::dvec3(0,1,0),glm::dvec3(0,0,1)};wall.faces.fill(-1);wall.solid=true;wallStage.blocks.push_back(wall);
        StageGeometry wallGeometry(wallStage);FacePaint wallPaint(wallStage,wallGeometry);CombatWorld wallWorld(wallStage,wallGeometry,wallPaint);
        {
            PlayerSimulation waveOwner(wallStage,wallGeometry,wallPaint,0,&wallWorld);waveOwner.state.alive=true;
            ShooterProjectile wave;wave.owner=&waveOwner;wave.kind="bladewave";wave.weaponId="blade";wave.pos={0,0,0};wave.vel={0,0,28};wave.range=10;wave.life=1;wave.radius=.35;wave.size=1.7;wallWorld.launch(wave);
            const auto waveId=wallWorld.projectiles().back().id;
            renderer.Update(0,wallWorld,{});
            renderer.Update(0,wallWorld,{{"blade_wave_hit",{0,0,0},{0,0,-1},.09,0,0,.08,0,"blade",waveId}});
            if(renderer.Draw(view,projection,eye,lights)!=1)throw std::runtime_error("Blade wave hit event incorrectly ended the projectile");
            wallWorld.tick(.02);const auto wallEvents=wallWorld.takeVisualEvents();
            if(std::none_of(wallEvents.begin(),wallEvents.end(),[&](const auto& event){return event.kind=="blade_wave_wall"&&event.id==waveId;}))throw std::runtime_error("Blade wave wall impact event missing");
            renderer.Update(.02,wallWorld,wallEvents);
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            if(renderer.Draw(view,projection,eye,lights)!=1)throw std::runtime_error("Blade wave wall fade did not render");
            glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            std::size_t fading=0;for(std::size_t i=0;i<pixels.size();i+=4)if(pixels[i]>20||pixels[i+1]>20||pixels[i+2]>20)++fading;
            if(fading<8)throw std::runtime_error("Blade wave wall fade produced no visible pixels");
        }
        wallWorld.tick(.07);renderer.Update(.07,wallWorld,{});
        if(renderer.Draw(view,projection,eye,lights)!=1)throw std::runtime_error("Blade wave fade expired before 0.14 seconds");
        wallWorld.tick(.08);renderer.Update(.08,wallWorld,{});
        if(renderer.Draw(view,projection,eye,lights)!=0)throw std::runtime_error("Blade wave wall fade outlived 0.14 seconds");

        StageData rangeStage{};rangeStage.bounds={-30,30,-30,30};rangeStage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
        StageGeometry rangeGeometry(rangeStage);FacePaint rangePaint(rangeStage,rangeGeometry);CombatWorld rangeWorld(rangeStage,rangeGeometry,rangePaint);
        {
            PlayerSimulation waveOwner(rangeStage,rangeGeometry,rangePaint,0,&rangeWorld);waveOwner.state.alive=true;
            ShooterProjectile wave;wave.owner=&waveOwner;wave.kind="bladewave";wave.weaponId="blade";wave.pos={0,0,0};wave.vel={0,0,28};wave.range=.1;wave.life=.1;wave.radius=.35;wave.size=1.7;rangeWorld.launch(wave);
            const auto waveId=rangeWorld.projectiles().back().id;
            renderer.Update(0,rangeWorld,{});rangeWorld.tick(.02);const auto rangeEvents=rangeWorld.takeVisualEvents();
            if(std::none_of(rangeEvents.begin(),rangeEvents.end(),[&](const auto& event){return event.kind=="blade_wave_stop"&&event.id==waveId;}))throw std::runtime_error("Blade wave range stop event missing");
            renderer.Update(.02,rangeWorld,rangeEvents);
            if(renderer.Draw(view,projection,eye,lights)!=1)throw std::runtime_error("Blade wave range expiry did not start its fade");
        }
        rangeWorld.tick(.15);renderer.Update(.15,rangeWorld,{});
        if(renderer.Draw(view,projection,eye,lights)!=0)throw std::runtime_error("Blade wave range fade outlived 0.14 seconds");

        GLuint shadowFramebuffer=0,shadowDepth=0;glGenFramebuffers(1,&shadowFramebuffer);glBindFramebuffer(GL_FRAMEBUFFER,shadowFramebuffer);
        glGenTextures(1,&shadowDepth);glBindTexture(GL_TEXTURE_2D,shadowDepth);glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,256,256,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,shadowDepth,0);glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Projectile shadow depth framebuffer is incomplete");
        glViewport(0,0,256,256);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
        const auto checkShadow=[&](const char* kind,int expectedDraws,bool expectedDepth){
            world.clear();auto caster=shot;caster.kind=kind;caster.weaponId=kind;caster.state="fly";caster.pos={0,0,0};caster.vel={28,0,2};caster.flightDir=glm::normalize(caster.vel);caster.age=.12;caster.distance=3;caster.tier=2;
            world.launch(caster);renderer.Update(0,world,{});glClearDepth(1.0);glClear(GL_DEPTH_BUFFER_BIT);
            const auto draws=renderer.DrawShadow(projection*view,lights);
            std::vector<float> depth(256*256);glReadPixels(0,0,256,256,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
            const auto covered=std::count_if(depth.begin(),depth.end(),[](float value){return value<.9999f;});
            if(draws!=expectedDraws||((covered>0)!=expectedDepth)||glGetError()!=GL_NO_ERROR)
                throw std::runtime_error(std::string("Projectile source shadow caster mismatch: ")+kind+" draws="+std::to_string(draws)+" covered="+std::to_string(covered));
        };
        checkShadow("shot",1,true);checkShadow("arrow",2,true);checkShadow("fist",1,true);checkShadow("bladewave",0,false);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteTextures(1,&shadowDepth);glDeleteFramebuffers(1,&shadowFramebuffer);
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Source projectile renderer GL error");
        std::cout<<"PROJECTILE_GPU source_teardrop=4 visible_pixels="<<colored<<" ribbon_lifecycle=pass renderer="<<glGetString(GL_RENDERER)<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
