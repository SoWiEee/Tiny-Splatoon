#include "../src/render/PlayerShowcase.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}

struct Target {
    GLuint framebuffer=0,color=0,depth=0;
    Target(int width,int height,bool withAttachments) {
        glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        if(!withAttachments)return;
        glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
        glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Showcase test framebuffer incomplete");
    }
    ~Target(){if(depth)glDeleteRenderbuffers(1,&depth);if(color)glDeleteTextures(1,&color);if(framebuffer)glDeleteFramebuffers(1,&framebuffer);}
};

std::vector<unsigned char> capture(restoration::PlayerShowcase& showcase,const restoration::PlayerProfile& profile,
                                   const std::string& main,const std::string& sub,GLuint framebuffer) {
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glViewport(0,0,512,512);glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glClearDepth(1.0);
    glClearColor(0.12f,0.15f,0.18f,1.0f);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    showcase.drawPlayer(profile,main,sub,{40,40,432,432},0.0,true);
    if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Showcase character draw failed");
    std::vector<unsigned char> pixels(512*512*4);glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,512,512,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    return pixels;
}

std::size_t differences(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b) {
    std::size_t count=0;
    for(std::size_t i=0;i<a.size();i+=4)
        if(std::abs(int(a[i])-int(b[i]))+std::abs(int(a[i+1])-int(b[i+1]))+std::abs(int(a[i+2])-int(b[i+2]))>24)++count;
    return count;
}

void testPodiumMotion() {
    using restoration::PlayerShowcase;
    constexpr double tolerance=1e-8;
    const auto starting=PlayerShowcase::podiumMotion(0.0,0,true);
    require(std::abs(starting.drumOffset+2.4)<tolerance,"Podium drum did not start below the stage");
    require(starting.visible&&starting.airborne&&!starting.landed,"First winner did not enter in the air");
    require(std::abs(starting.playerOffset-3.08016)<tolerance,"Winner entrance height did not match the source drop");

    require(PlayerShowcase::podiumMotion(.2,0,true).squash>1.0,"Falling winner must stretch before landing");
    const auto risen=PlayerShowcase::podiumMotion(.72,0,true);
    require(std::abs(risen.drumOffset)<tolerance,"Podium drum did not finish rising on source timing");
    const auto staggered=PlayerShowcase::podiumMotion(.46,1,true);
    require(staggered.airborne&&staggered.playerOffset>0.0,"Later winner did not keep the staggered drop");

    const auto landed=PlayerShowcase::podiumMotion(.46,0,true);
    require(landed.landed&&!landed.airborne&&std::abs(landed.playerOffset)<tolerance,"Winner did not land at the source stagger");
    const auto wobble=PlayerShowcase::podiumMotion(.51,0,true);
    require(wobble.squash<1.0,"Landing did not produce a squash response");

    const auto defeat=PlayerShowcase::podiumMotion(.5,2,false);
    require(defeat.visible&&!defeat.airborne&&!defeat.landed&&std::abs(defeat.playerOffset)<tolerance,
            "Defeat lineup should remain grounded");
    const auto reduced=PlayerShowcase::podiumMotion(0.0,3,true,true);
    require(reduced.visible&&reduced.landed&&!reduced.airborne&&std::abs(reduced.drumOffset)<tolerance&&
            std::abs(reduced.playerOffset)<tolerance,"Reduced motion should keep a static result pose");
}
}

int main(int argc,char** argv) {
    try {testPodiumMotion();}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    if(!glfwInit())return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(512,512,"Player showcase acceptance",nullptr,nullptr);
    if(!window){glfwTerminate();return 1;}glfwMakeContextCurrent(window);
    if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))){glfwDestroyWindow(window);glfwTerminate();return 1;}
    int result=0;
    try {
        using namespace restoration;
        Target output(512,512,true),other(1,1,false);
        PlayerShowcase showcase(argc>1?argv[1]:"assets/models/characters");
        PlayerProfile profile;profile.name="showcase-test";

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,output.framebuffer);glBindFramebuffer(GL_READ_FRAMEBUFFER,other.framebuffer);
        glViewport(7,9,301,277);glEnable(GL_SCISSOR_TEST);glScissor(11,13,71,73);
        glDisable(GL_DEPTH_TEST);glDepthFunc(GL_GREATER);glDepthMask(GL_FALSE);glClearDepth(0.37);
        GLdouble expectedDepthClear=0;glGetDoublev(GL_DEPTH_CLEAR_VALUE,&expectedDepthClear);
        glActiveTexture(GL_TEXTURE4);
        showcase.drawPlayer(profile,"shooter","bomb",{40,40,432,432},0.0,true);
        GLint viewport[4],scissor[4],drawFbo=0,readFbo=0,depthFunc=0,activeTexture=0;
        GLboolean depthMask=GL_TRUE;
        glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_SCISSOR_BOX,scissor);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);
        glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);
        require(viewport[0]==7&&viewport[1]==9&&viewport[2]==301&&viewport[3]==277,"Showcase did not restore the viewport");
        require(scissor[0]==11&&scissor[1]==13&&scissor[2]==71&&scissor[3]==73&&glIsEnabled(GL_SCISSOR_TEST),"Showcase did not restore scissor state");
        require(drawFbo==static_cast<GLint>(output.framebuffer)&&readFbo==static_cast<GLint>(other.framebuffer),"Showcase did not restore draw/read framebuffers");
        require(!glIsEnabled(GL_DEPTH_TEST)&&depthFunc==GL_GREATER&&depthMask==GL_FALSE,"Showcase did not restore depth state");
        require(activeTexture==GL_TEXTURE4,"Showcase did not restore the active texture unit");
        GLdouble depthClear=0.0;glGetDoublev(GL_DEPTH_CLEAR_VALUE,&depthClear);
        require(depthClear==expectedDepthClear,"Showcase did not restore the depth clear value");

        auto base=capture(showcase,profile,"shooter","bomb",output.framebuffer);
        int covered=0;
        for(int y=0;y<512;++y)for(int x=0;x<512;++x){
            const auto i=static_cast<std::size_t>((y*512+x)*4);
            if(x<40||x>=472||y<40||y>=472)
                require(base[i]==31&&base[i+1]==38&&base[i+2]==46,"Showcase drew outside its viewport");
            else if(std::abs(int(base[i])-31)>3||std::abs(int(base[i+1])-38)>3||std::abs(int(base[i+2])-46)>3)++covered;
        }
        require(covered>500,"Showcase did not render a visible player");
        profile.style.hair=1;profile.style.skin=5;profile.style.outfit=5;
        auto styled=capture(showcase,profile,"shooter","bomb",output.framebuffer);
        require(differences(base,styled)>200,"Showcase style change did not affect rendered pixels");
        auto armed=capture(showcase,profile,"charger","bomb",output.framebuffer);
        require(differences(styled,armed)>80,"Showcase weapon change did not affect rendered pixels");

        std::vector<PlayerShowcase::PodiumPlayer> roster={
            {"one",{},"shooter","bomb"},{"two",{},"charger","mine"},
            {"three",{},"roller","sticky"},{"four",{},"blaster","burst"}};
        glBindFramebuffer(GL_FRAMEBUFFER,output.framebuffer);glViewport(0,0,512,512);glDisable(GL_SCISSOR_TEST);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);
        glClearColor(0.12f,0.15f,0.18f,1.0f);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        showcase.drawPodium(roster,true,{0,120,512,300},0.0,true);
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Showcase podium draw failed");
        std::cout<<"Showcase player style, held weapon, podium, and GL state acceptance passed on "<<glGetString(GL_RENDERER)<<'\n';
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
