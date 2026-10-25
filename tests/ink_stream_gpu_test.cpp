#include "InkStream.hpp"
#include "InkStreamRenderer.hpp"
#include "CharacterShader.hpp"
#include "StageGeometry.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <iostream>
#include <stdexcept>

using namespace restoration;
namespace {
constexpr int width=1140,height=360,column=width/3;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
StageData scene(){
    StageData s{};s.bounds={-10,10,-10,10};s.spawnPads={glm::dvec3(0),glm::dvec3(8,0,8)};
    for(bool wall:{false,true}){BlockData b{};b.center=wall?glm::dvec3(0,2,1.9):glm::dvec3(0,-.1,0);b.half=wall?glm::dvec3(5,3,.1):glm::dvec3(10,.1,10);b.axes={glm::dvec3(1,0,0),glm::dvec3(0,1,0),glm::dvec3(0,0,1)};b.faces.fill(-1);b.solid=true;s.blocks.push_back(b);}return s;
}
}
int main(int argc,char** argv){
    if(!glfwInit())return 1;glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(width,height,"Stream Tube / SDF / Screen Space",nullptr,nullptr);if(!window){glfwTerminate();return 1;}glfwMakeContextCurrent(window);
    if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))return 1;
    int result=0;
    try{
        const std::filesystem::path assets=argc>1?argv[1]:"assets/shaders/ink-stream",output=argc>2?argv[2]:"build/captures/ink-stream";
        const int frames=argc>3?std::stoi(argv[3]):180;check(frames>=120&&frames<=600,"Frames must be in 120..600");std::filesystem::create_directories(output);
        const auto stage=scene();StageGeometry geometry(stage);InkStream stream;InkStreamRenderer renderer(assets);
        CharacterLightState lights;lights.ambient={.3,.3,.3};lights.sunDirection=glm::normalize(glm::vec3(-.4,.8,.3));lights.sunColor={1,.92,.82};lights.skyColor={.15,.2,.3};lights.groundColor={.1,.08,.06};
        InkStreamEmitter emitter;emitter.id=1;emitter.position={0,1,-2};emitter.velocity={0,-.6,12};emitter.seed=.37;
        std::array<std::size_t,3> visible{};std::array<double,3> gpu{};std::array<int,3> measured{};GLuint queries[3]{};glGenQueries(3,queries);
        std::vector<unsigned char> pixels(width*height*3);glPixelStorei(GL_PACK_ALIGNMENT,1);stbi_flip_vertically_on_write(1);
        glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);glClearColor(.02f,.03f,.04f,1);
        for(int frame=0;frame<frames;frame++){
            emitter.active=frame<90;
            const double phase=frame/60.0;emitter.velocity={2.2*std::sin(phase*1.4),-.6+std::sin(phase)*.3,frame<55?12.0:17.0};
            stream.Update(1.0/60,{emitter},geometry);renderer.Update(stream.pieces(),stream.drops());
            glDisable(GL_SCISSOR_TEST);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            const double angle=.4+frame*.007;const glm::dvec3 eye(std::cos(angle)*4.5,2.3,std::sin(angle)*4.5);
            const auto view=glm::lookAt(eye,glm::dvec3(0,.65,-.15),glm::dvec3(0,1,0));
            const auto projection=glm::perspective(glm::radians(50.0),double(column)/height,.06,30.0);
            for(int mode=0;mode<3;mode++){
                glViewport(mode*column,0,column,height);GLint beforeViewport[4]{};glGetIntegerv(GL_VIEWPORT,beforeViewport);
                glBeginQuery(GL_TIME_ELAPSED,queries[mode]);renderer.Draw(view,projection,eye,lights,static_cast<InkStreamMode>(mode));glEndQuery(GL_TIME_ELAPSED);
                GLuint64 elapsed=0;glGetQueryObjectui64v(queries[mode],GL_QUERY_RESULT,&elapsed);if(frame>=30&&frame<90){gpu[mode]+=elapsed/1e6;++measured[mode];}
                GLint viewport[4]{},fbo=0;glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
                check(std::equal(std::begin(viewport),std::end(viewport),std::begin(beforeViewport))&&fbo==0,"Stream renderer leaked framebuffer/viewport state");
            }
            glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
            for(int mode=0;mode<3;mode++){std::size_t count=0;for(int y=0;y<height;y++)for(int x=mode*column;x<(mode+1)*column;x++){const auto i=(y*width+x)*3;if(pixels[i]>35||pixels[i+1]>35||pixels[i+2]>35)++count;}if(count>20)++visible[mode];}
            check(glGetError()==GL_NO_ERROR,"Stream draw returned OpenGL error");
            char name[64]{};std::snprintf(name,sizeof(name),"frame-%04d.png",frame);check(stbi_write_png((output/name).string().c_str(),width,height,3,pixels.data(),width*3)!=0,"Could not save stream comparison");
        }
        check(stream.stats().emitted>0&&stream.stats().splits>0&&stream.stats().impacts>0,"Stream lifecycle failed to emit, split or impact");
        for(const auto count:visible)check(count>20,"A stream rendering mode produced no visible ink");
        check(stream.pieces().empty()&&stream.drops().empty(),"Stopped stream did not expire");
        InkStreamPiece sample;sample.id=1;sample.points={{{-.5f,0,0},.12f},{{0,.1f,0},.08f},{{.5f,0,0},.10f}};renderer.Update({sample},{});
        const glm::dvec3 eye(0,.3,2);const auto view=glm::lookAt(eye,glm::dvec3(0),glm::dvec3(0,1,0));
        const auto projection=glm::perspective(glm::radians(50.0),1.0,.06,30.0);glViewport(0,0,360,360);
        std::vector<unsigned char> occluded(360*360*3);
        for(int mode=0;mode<3;mode++){
            glClearDepth(.0001);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);renderer.Draw(view,projection,eye,lights,static_cast<InkStreamMode>(mode));
            glReadPixels(0,0,360,360,GL_RGB,GL_UNSIGNED_BYTE,occluded.data());check(std::none_of(occluded.begin(),occluded.end(),[](auto v){return v>35;}),"Stream surface ignored foreground scene depth");
            glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);renderer.Draw(view,glm::ortho(-1.0,1.0,-1.0,1.0,.06,30.0),eye,lights,static_cast<InkStreamMode>(mode));
            glReadPixels(0,0,360,360,GL_RGB,GL_UNSIGNED_BYTE,occluded.data());check(std::any_of(occluded.begin(),occluded.end(),[](auto v){return v>35;}),"Orthographic stream disappeared");
        }
        check(glGetError()==GL_NO_ERROR,"Stream depth/projection checks returned GL error");glDeleteQueries(3,queries);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
        renderer.Draw(view,glm::ortho(-1.0,1.0,-1.0,1.0,.06,30.0),eye,lights,InkStreamMode::Tube,true);
        std::vector<float> shadow(360*360);glReadPixels(0,0,360,360,GL_DEPTH_COMPONENT,GL_FLOAT,shadow.data());
        GLboolean mask[4]{};glGetBooleanv(GL_COLOR_WRITEMASK,mask);check(!mask[0]&&!mask[3],"Stream shadow pass leaked its color mask");glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        check(std::count_if(shadow.begin(),shadow.end(),[](float d){return d<.9999f;})>20&&glGetError()==GL_NO_ERROR,"Stream tube shadow wrote no depth");
        std::ofstream report(output/"timings.csv");report<<"mode,gpu_ms_mean\n";const char* names[]={"tube","sdf","screen_space"};
        std::cout<<"INK_STREAM_GPU renderer="<<glGetString(GL_RENDERER)<<" samples="<<stream.stats().emitted<<" splits="<<stream.stats().splits<<" impacts="<<stream.stats().impacts<<'\n';
        for(int mode=0;mode<3;mode++){const double mean=gpu[mode]/std::max(1,measured[mode]);report<<names[mode]<<','<<mean<<'\n';std::cout<<names[mode]<<" gpu_ms="<<mean<<" visible_frames="<<visible[mode]<<'\n';}
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
