#include "../src/render/CharacterArt.hpp"
#include "../src/game/ArsenalData.hpp"
#include "fixtures/character_render_fixtures.hpp"
#include "fixtures/character_tank_fixtures.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv){
    if(!glfwInit())return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(1024,1200,"Source character render acceptance",nullptr,nullptr);
    if(!window){glfwTerminate();return 1;}glfwMakeContextCurrent(window);
    if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))){glfwDestroyWindow(window);glfwTerminate();return 1;}
    int result=0;
    try {
        using namespace restoration;
        const std::filesystem::path assets=argc>1?argv[1]:"assets/reference/characters",captures=argc>2?argv[2]:"build/captures/character";
        std::filesystem::create_directories(captures);
        CharacterArt art(assets);
        GLuint fbo=0,color=0,depth=0;
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1024,1200,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
        glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,1024,1200);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Character capture framebuffer incomplete");
        const glm::dvec3 eye(2.7,1.6,3.8);
        const auto view=glm::lookAt(eye,glm::dvec3(0,0.85,0),glm::dvec3(0,1,0));
        const auto projection=glm::perspective(glm::radians(40.0),256.0/300.0,0.05,50.0);
        CharacterLightState light;light.ambient={0.16,0.16,0.16};light.sunDirection={-0.4,0.85,0.35};light.sunColor={2.5,2.4,2.3};light.skyColor={0.65,0.75,1.0};light.groundColor={0.2,0.15,0.1};
        glClearColor(0.12f,0.15f,0.18f,1);
        std::size_t totalDraws=0;
        auto panel=[&](int index,CharacterPoseFrame frame,const CharacterStyle& style,const std::string& main,const std::string& sub){
            const int x=(index%4)*256,y=(3-index/4)*300;
            glViewport(x,y,256,300);glEnable(GL_SCISSOR_TEST);glScissor(x,y,256,300);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
            totalDraws+=art.draw(frame,style,main,sub,"hero",view,projection,eye,light);
            if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Source character draw failed for "+main+"/"+sub);
            std::vector<unsigned char> pixels(256*300*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(x,y,256,300,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
            int covered=0,colored=0;for(std::size_t p=0;p<pixels.size();p+=3){if(std::abs(int(pixels[p])-31)>3||std::abs(int(pixels[p+1])-38)>3||std::abs(int(pixels[p+2])-46)>3)++covered;if(std::max({pixels[p],pixels[p+1],pixels[p+2]})>80&&std::max({pixels[p],pixels[p+1],pixels[p+2]})-std::min({pixels[p],pixels[p+1],pixels[p+2]})>20)++colored;}
            if(covered<200)throw std::runtime_error("Source character is missing from framebuffer: "+main+"/"+sub);
            if(colored<100)throw std::runtime_error("Source character lost material color: "+main+"/"+sub);
        };
        auto capture=[&](const char* name){
            std::vector<unsigned char> pixels(1024*1200*3);glReadPixels(0,0,1024,1200,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());stbi_flip_vertically_on_write(1);
            if(!stbi_write_png((captures/name).string().c_str(),1024,1200,3,pixels.data(),1024*3))throw std::runtime_error("Cannot save character capture");
        };
        glViewport(0,0,1024,1200);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        int i=0;for(const auto& weapon:MainEquipment())panel(i++,test::RenderFixture(std::string(weapon.id)),{},std::string(weapon.id),std::string(weapon.sub));
        capture("main-models.png");
        glViewport(0,0,1024,1200);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        i=0;for(const auto& sub:SubEquipment()){
            auto frame=test::RenderFixture("shooter");CharacterPartPose held;held.name="sub/group";
            held.local=art.data().model("sub/"+std::string(sub.id)).nodes.front().local;held.local.visible=true;frame.parts.push_back(held);
            panel(i++,frame,{},"shooter",std::string(sub.id));
        }
        capture("sub-models.png");
        glViewport(0,0,1024,1200);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        i=0;for(const auto& preset:CharacterPresets)panel(i++,test::RenderFixture(std::string("preset/")+preset.id),preset.style,"shooter","bomb");
        capture("preset-models.png");
        // Exercise live state transitions through the same pose solver used by matches.
        CharacterPose live(art.data(),art.data().rig(0,0,0),"live-forms","shooter");
        live.setColor({1.0,.2541520943,.0069954102});
        CharacterAnimState liveState;CharacterSceneInput liveScene;liveScene.inWorld=true;
        glViewport(0,0,1024,1200);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        i=0;
        for(const auto form:{CharacterForm::Kid,CharacterForm::Squid,CharacterForm::Swim,CharacterForm::Climb,CharacterForm::Kid}){
            liveState.form=form;liveState.speed=form==CharacterForm::Swim?7:2;liveState.localMove={0,1};
            for(int f=0;f<36;f++)live.update(1.0/60,liveState,liveScene);
            const auto frame=live.frame();
            if(form==CharacterForm::Kid?(!frame.kid.visible||frame.squid.visible):(frame.kid.visible||!frame.squid.visible))
                throw std::runtime_error("Live dive/return selected the wrong character model");
            panel(i++,frame,{},"shooter","bomb");
        }
        capture("live-dive-return.png");
        for(const auto& expected:test::TankFixtures){
            auto frame=test::RenderFixture("preset/rookie");frame.root.matrixOverride=true;frame.root.authoredMatrix=expected.root;
            const auto style=CharacterPresets.front().style;
            for(const auto& id:{"body/"+std::to_string(style.hair)+"/"+std::to_string(style.hat)+"/"+std::to_string(style.brows)+"/hero",std::string("main/shooter"),std::string("sub/bomb")})
                for(const auto& node:art.data().model(id).nodes)if(!node.mesh.empty()&&node.material!="fill"){
                    CharacterPartPose hidden;hidden.name=node.binding;hidden.local=node.local;hidden.local.visible=false;frame.parts.push_back(std::move(hidden));
                }
            if(art.draw(frame,style,"shooter","bomb","hero",view,projection,eye,light)!=1)throw std::runtime_error("Tank uniform fixture must isolate the real fill draw");
            GLint program=0;glGetIntegerv(GL_CURRENT_PROGRAM,&program);
            const GLint upLocation=glGetUniformLocation(program,"uUpL"),heightLocation=glGetUniformLocation(program,"uSY");
            if(upLocation<0||heightLocation<0)throw std::runtime_error("Source tank surface uniforms missing");
            float up[3],height;glGetUniformfv(program,upLocation,up);glGetUniformfv(program,heightLocation,&height);
            for(int axis=0;axis<3;++axis)if(!std::isfinite(up[axis])||std::abs(up[axis]-expected.up[axis])>2e-5)throw std::runtime_error("Tank world-up differs from source onBeforeRender");
            if(!std::isfinite(height)||std::abs(height-expected.sy)>2e-5)throw std::runtime_error("Tank surface height differs from source onBeforeRender");
        }
        for(const auto& [name,fade]:{std::pair{"lod-start.png",1e-4},
                                     std::pair{"lod-middle.png",0.5},
                                     std::pair{"lod-end.png",0.9999}}){
            glViewport(0,0,256,300);glEnable(GL_SCISSOR_TEST);glScissor(0,0,256,300);
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
            const auto frame=test::RenderFixture("preset/rookie");
            const auto style=CharacterPresets.front().style;
            art.draw(frame,style,"shooter","bomb","hero",view,projection,eye,light,{fade,1.0,true});
            art.draw(frame,style,"shooter","bomb","game",view,projection,eye,light,{fade,-1.0,false});
            if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Character LOD dither draw failed");
            std::vector<unsigned char> pixels(256*300*3);glPixelStorei(GL_PACK_ALIGNMENT,1);
            glReadPixels(0,0,256,300,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
            int covered=0;for(std::size_t p=0;p<pixels.size();p+=3)
                if(std::abs(int(pixels[p])-31)>3||std::abs(int(pixels[p+1])-38)>3||std::abs(int(pixels[p+2])-46)>3)++covered;
            if(covered<200)throw std::runtime_error("Character LOD transition dropped the body");
            stbi_flip_vertically_on_write(1);
            if(!stbi_write_png((captures/name).string().c_str(),256,300,3,pixels.data(),256*3))
                throw std::runtime_error("Cannot save character LOD capture");
        }
        std::cout<<"12 main, 15 sub and 10 preset source models drawn; "<<totalDraws<<" draws on "<<glGetString(GL_RENDERER)<<" / "<<glGetString(GL_VERSION)<<'\n';
        glDeleteRenderbuffers(1,&depth);glDeleteTextures(1,&color);glDeleteFramebuffers(1,&fbo);
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
