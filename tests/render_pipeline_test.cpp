#include "../src/render/RenderPipeline.hpp"
#include "../src/render/DirectionalShadow.hpp"
#include "../src/render/CharacterShader.hpp"
#include "../src/render/LensInk.hpp"
#include "../src/render/LensField.hpp"
#include "../src/render/CharacterRenderData.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <array>
#include <string>

int main(){
    if(!glfwInit())return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(96,96,"HDR pipeline acceptance",nullptr,nullptr);
    if(!window){glfwTerminate();return 1;}glfwMakeContextCurrent(window);
    if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))){glfwDestroyWindow(window);glfwTerminate();return 1;}
    int result=0;
    try{
        using namespace restoration;
        RenderPipeline pipeline;
        const auto projection=glm::perspective(glm::radians(60.f),1.f,.1f,100.f);
        GradeSettings neutral;neutral.saturation=1;neutral.vibrance=0;neutral.contrast=1;neutral.vignette=0;neutral.shadowTint=neutral.highlightTint=glm::vec3(1);
        auto render=[&](float input,int samples,bool ao,bool bloom,const GradeSettings& grade,const ScreenEffects& fx,int w=64,int h=64){
            pipeline.Configure(samples,ao,bloom);pipeline.Begin(w,h,projection,glm::mat4(1),.1f,100);
            glClearColor(input,input,input,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);pipeline.End(grade,fx);
            std::array<unsigned char,4> pixel{};glReadPixels(w/2,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
            if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Postprocessing generated an OpenGL error");
            return int(pixel[0]);
        };
        const int gray=render(.18f,0,false,false,neutral,{});
        // Source Neutral subtracts .04 above .08; sRGB(.18-.04) is 104.56/255.
        if(std::abs(gray-105)>2)throw std::runtime_error("Neutral/sRGB color path differs from source: "+std::to_string(gray));
        const int msaa=render(.18f,4,true,false,neutral,{},79,53);
        if(std::abs(msaa-gray)>1)throw std::runtime_error("MSAA/depth resolve or GTAO background changed neutral color");
        const int hdr=render(3.f,4,true,false,neutral,{}),bloom=render(3.f,4,true,true,neutral,{});
        if(bloom<=hdr)throw std::runtime_error("HDR values did not reach the bloom threshold");
        GradeSettings dark=neutral;dark.exposure=.5f;
        if(render(.18f,0,false,false,dark,{})>=gray-15)throw std::runtime_error("Grade exposure is not applied in HDR");
        ScreenEffects flash;flash.flash={1,1,1,.5f};
        if(render(.18f,0,false,false,neutral,flash)<=gray+20)throw std::runtime_error("Source screen-FX composite did not apply flash");
        LensInk ink;ink.droplet(0,.5,.5,.12);ink.update(.1,1);LensField field;
        if(field.Render(ink,64,64)!=1)throw std::runtime_error("Lens field did not submit its instances");
        ScreenEffects stained;stained.lensTexture=field.texture();stained.lensTexel=field.texel();stained.lensColorA={.01f,.04f,1};
        if(std::abs(render(.18f,0,false,false,neutral,stained)-gray)<5)throw std::runtime_error("Source metaball lens field did not reach the HDR composite");
        for(int i=0;i<20;i++)ink.splat(i%3,.5,.5,.08,1);
        if(ink.particles().size()!=LensInk::Capacity)throw std::runtime_error("Lens field pool exceeded or failed its source capacity");
        for(int i=0;i<1200;i++)ink.update(1./60,1);
        if(!ink.particles().empty())throw std::runtime_error("Lens field particles did not expire");
        bool rejected=false;try{pipeline.Begin(0,64,projection,glm::mat4(1),.1f,100);}catch(const std::invalid_argument&){rejected=true;}
        if(!rejected)throw std::runtime_error("Zero-size render target was accepted");
        DirectionalShadow shadow;
        shadow.Begin({-30,30,-40,40},glm::normalize(glm::dvec3(-.4,.85,.35)),1024);
        glClearDepth(.25);glClear(GL_DEPTH_BUFFER_BIT);shadow.End();
        GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
        if(viewport[2]!=64||viewport[3]!=64)throw std::runtime_error("Shadow pass failed to restore viewport");
        if(shadow.bias()>=0||shadow.normalBias()<.012||std::abs(shadow.radius()-1.35)>1e-8)throw std::runtime_error("Shadow fit tuning differs from source");
        const char* vertex="#version 450 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}";
        const auto fragment=std::string("#version 450 core\nout vec4 color;uniform vec3 point;\n")+DirectionalShadow::SamplingGLSL()+"\nvoid main(){color=vec4(vec3(sampleSunShadow(point,vec3(0,1,0))),1);}";
        CharacterShader receiver(vertex,fragment);receiver.use();
        const auto m=shadow.matrix();const auto point=glm::inverse(m)*glm::dvec4(0,0,0,1);
        receiver.uniform("point",{point.x/point.w,point.y/point.w,point.z/point.w});
        receiver.uniform("sunShadowMatrix",std::vector<double>(glm::value_ptr(m),glm::value_ptr(m)+16));receiver.uniform("sunShadowBias",shadow.bias());receiver.uniform("sunShadowNormalBias",shadow.normalBias());receiver.uniform("sunShadowRadius",shadow.radius());receiver.uniform("sunShadow",0.);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,shadow.texture());GLuint shadowVao=0;glGenVertexArrays(1,&shadowVao);glBindVertexArray(shadowVao);glDisable(GL_DEPTH_TEST);glDrawArrays(GL_TRIANGLES,0,3);
        std::array<unsigned char,4> shaded{};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,shaded.data());
        if(shaded[0]>2)throw std::runtime_error("Shadow receiver did not compare occluded depth");
        shadow.Begin({-30,30,-40,40},glm::normalize(glm::dvec3(-.4,.85,.35)),1024);glClearDepth(.75);glClear(GL_DEPTH_BUFFER_BIT);shadow.End();receiver.use();glBindTexture(GL_TEXTURE_2D,shadow.texture());glBindVertexArray(shadowVao);glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,shaded.data());glDeleteVertexArrays(1,&shadowVao);glClearDepth(1);
        if(shaded[0]<253||glGetError()!=GL_NO_ERROR)throw std::runtime_error("Shadow receiver did not preserve lit depth");
        std::vector<std::unique_ptr<CharacterShader>> characterShaders;
        CharacterShader* physical=nullptr;
        for(const auto& definition:CharacterMaterials()){
            const auto path=std::filesystem::path("assets/reference/characters/shaders-shadow");
            auto shader=std::make_unique<CharacterShader>(CharacterShader::readSource(path/(definition.key+".vert")),CharacterShader::readSource(path/(definition.key+".frag")));
            shader->use();for(const auto& [name,values]:definition.uniforms)shader->uniform(name,values);
            if(definition.key=="dark_plain")physical=shader.get();characterShaders.push_back(std::move(shader));
        }
        if(!physical)throw std::runtime_error("Missing plain physical source receiver");
        shadow.Begin({-30,30,-40,40},glm::normalize(glm::dvec3(-.4,.85,.35)),1024);glClearDepth(.25);glClear(GL_DEPTH_BUFFER_BIT);shadow.End();glClearDepth(1);
        GLuint materialVao=0,materialVbo=0,dfg=0;glGenVertexArrays(1,&materialVao);glBindVertexArray(materialVao);glGenBuffers(1,&materialVbo);glBindBuffer(GL_ARRAY_BUFFER,materialVbo);
        const float triangle[]={-1,-1,0,0,0,1,1,-1,0,0,0,1,0,1,0,0,0,1};glBufferData(GL_ARRAY_BUFFER,sizeof(triangle),triangle,GL_STATIC_DRAW);
        for(int i=0;i<2;i++){const auto location=physical->attribute(i?"normal":"position");if(location>=0){glEnableVertexAttribArray(location);glVertexAttribPointer(location,3,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<void*>(i*3*sizeof(float)));}}
        glGenTextures(1,&dfg);glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,dfg);glTexImage2D(GL_TEXTURE_2D,0,GL_RG16F,16,16,0,GL_RG,GL_HALF_FLOAT,CharacterDfg.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        physical->use();physical->uniform("dfgLUT",6.);physical->uniform("diffuse",{.6,.6,.6});physical->uniform("uTeam",{1.,1.,1.});
        const auto view=glm::lookAt(glm::dvec3(0,0,3),glm::dvec3(0),glm::dvec3(0,1,0)),projectionD=glm::perspective(glm::radians(60.),1.,.1,10.);
        physical->matrices(glm::dmat4(1),view,projectionD,{0,0,3});CharacterLightState light;light.sunDirection={0,0,1};light.sunColor={1,1,1};physical->lights(light,view);
        glm::dmat4 fixedShadow(0);fixedShadow[3]={.5,.5,.5,1};physical->uniform("directionalShadowMatrix",std::vector<double>(glm::value_ptr(fixedShadow),glm::value_ptr(fixedShadow)+16));
        physical->uniform("directionalShadowMap",8.);physical->uniform("directionalLightShadows[0].shadowIntensity",1.);physical->uniform("directionalLightShadows[0].shadowBias",0.);physical->uniform("directionalLightShadows[0].shadowNormalBias",0.);physical->uniform("directionalLightShadows[0].shadowRadius",shadow.radius());physical->uniform("directionalLightShadows[0].shadowMapSize",{1024.,1024.});
        glActiveTexture(GL_TEXTURE8);glBindTexture(GL_TEXTURE_2D,shadow.texture());glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);glViewport(0,0,64,64);
        physical->uniform("receiveShadow",0.);glDrawArrays(GL_TRIANGLES,0,3);std::array<unsigned char,4> unshadowed{};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,unshadowed.data());
        physical->uniform("receiveShadow",1.);glDrawArrays(GL_TRIANGLES,0,3);std::array<unsigned char,4> shadowed{};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,shadowed.data());
        glDeleteTextures(1,&dfg);glDeleteBuffers(1,&materialVbo);glDeleteVertexArrays(1,&materialVao);glActiveTexture(GL_TEXTURE0);
        if(unshadowed[0]<shadowed[0]+10||glGetError()!=GL_NO_ERROR)throw std::runtime_error("Source physical character material did not receive the directional shadow");
        std::cout<<"CHARACTER_SHADOW shaders="<<characterShaders.size()<<" lit="<<int(unshadowed[0])<<" shadow="<<int(shadowed[0])<<'\n';
        std::cout<<"POST_PIPELINE neutral="<<gray<<" msaa="<<msaa<<" hdr="<<hdr<<" bloom="<<bloom<<" resize=79x53 GPU="<<glGetString(GL_RENDERER)<<'\n';
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
