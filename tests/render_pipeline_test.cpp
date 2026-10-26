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
        const char* edgeVertex="#version 450 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}";
        const char* edgeFragment="#version 450 core\nout vec4 color;void main(){float c=gl_FragCoord.x+gl_FragCoord.y<64.0?1.0:0.0;color=vec4(c,c,c,1);}";
        CharacterShader edgeShader(edgeVertex,edgeFragment);GLuint edgeVao=0;glGenVertexArrays(1,&edgeVao);
        ScreenEffects noScreenEffects;noScreenEffects.enabled=false;
        std::array<unsigned char,64*64*4> withoutAa{},withAa{};
        auto renderEdge=[&](bool antialiasing,std::array<unsigned char,64*64*4>& pixels){
            pipeline.Configure(0,false,false,antialiasing);pipeline.Begin(64,64,projection,glm::mat4(1),.1f,100);
            glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);edgeShader.use();glBindVertexArray(edgeVao);glDrawArrays(GL_TRIANGLES,0,3);
            const int draws=pipeline.End(neutral,noScreenEffects);glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Antialiasing generated an OpenGL error");return draws;
        };
        const int withoutAaDraws=renderEdge(false,withoutAa),withAaDraws=renderEdge(true,withAa);glDeleteVertexArrays(1,&edgeVao);
        int changedEdgePixels=0;
        for(std::size_t i=0;i<withoutAa.size();i+=4)if(std::abs(int(withoutAa[i])-int(withAa[i]))>3)++changedEdgePixels;
        if(withAaDraws!=withoutAaDraws+1||changedEdgePixels==0)throw std::runtime_error("FXAA did not smooth the diagonal edge when enabled");
        pipeline.Configure(0,false,false);pipeline.Begin(32,32,projection,glm::mat4(1),.1f,100);
        glClearColor(.18f,.18f,.18f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);pipeline.End(neutral,{},64,64);
        GLint scaledViewport[4]{};glGetIntegerv(GL_VIEWPORT,scaledViewport);
        std::array<unsigned char,4> scaledPixel{};glReadPixels(48,48,1,1,GL_RGBA,GL_UNSIGNED_BYTE,scaledPixel.data());
        if(scaledViewport[2]!=64||scaledViewport[3]!=64||std::abs(int(scaledPixel[0])-gray)>1)throw std::runtime_error("Render scale did not upscale to the full output framebuffer");
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
            const auto path=std::filesystem::path("assets/models/characters/shaders-shadow");
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
        {
            const char* compositeVertex="#version 450 core\nout vec2 vUv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);vUv=p;gl_Position=vec4(p*2.-1.,0,1);}";
            CharacterShader composite(compositeVertex,CharacterShader::readSource("assets/shaders/post/ao-composite.frag"));
            GLuint textures[3]{},compositeVao=0;glGenTextures(3,textures);glGenVertexArrays(1,&compositeVao);
            auto upload=[](GLuint id,GLint internalFormat,GLenum format,GLsizei width,GLsizei height,const float* pixels,GLint filter){
                glBindTexture(GL_TEXTURE_2D,id);glTexImage2D(GL_TEXTURE_2D,0,internalFormat,width,height,0,format,GL_FLOAT,pixels);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            };
            std::array<float,8*2*4> scenePixels{};for(int i=0;i<8*2;++i){scenePixels[i*4]=.8f;scenePixels[i*4+1]=.6f;scenePixels[i*4+2]=.4f;scenePixels[i*4+3]=1.f;}
            std::array<float,8*2> depthPixels{};for(int y=0;y<2;++y)for(int x=0;x<8;++x)depthPixels[y*8+x]=x<4?.2f:(x==7?1.f:.9f);
            const std::array<float,4> aoPixels{{1.f,1.f,0.f,0.f}};
            upload(textures[0],GL_RGBA32F,GL_RGBA,8,2,scenePixels.data(),GL_NEAREST);
            upload(textures[1],GL_R32F,GL_RED,4,1,aoPixels.data(),GL_LINEAR);
            upload(textures[2],GL_R32F,GL_RED,8,2,depthPixels.data(),GL_NEAREST);
            composite.use();composite.uniform("scene",0.);composite.uniform("ao",1.);composite.uniform("depth",2.);
            composite.uniform("cameraNear",.1);composite.uniform("cameraFar",100.);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,textures[0]);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,textures[1]);glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,textures[2]);
            glBindFramebuffer(GL_FRAMEBUFFER,0);glDrawBuffer(GL_BACK);glBindVertexArray(compositeVao);
            glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_DITHER);
            auto drawComposite=[&](int width,int height){glViewport(0,0,width,height);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);};
            auto expectPixel=[&](int x,int y,float multiplier,const char* label){
                std::array<unsigned char,4> pixel{};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
                const float expected[4]={.8f*multiplier,.6f*multiplier,.4f*multiplier,1.f};
                for(int c=0;c<4;++c)if(std::abs(int(pixel[c])-int(std::lround(expected[c]*255.f)))>3)
                    throw std::runtime_error(std::string("AO composite ")+label+" pixel mismatch at x="+std::to_string(x));
            };
            // Low-resolution AO centers map to full-resolution columns 1, 3, 5, and 7.
            drawComposite(8,2);expectPixel(3,0,1.f,"foreground edge");expectPixel(4,0,.2f,"background edge");expectPixel(7,0,1.f,"sky fallback");
            for(int y=0;y<2;++y)for(int x=0;x<8;++x)depthPixels[y*8+x]=x==2?.2f:.9f;
            const std::array<float,4> darkAo{};
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,textures[1]);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,1,GL_RED,GL_FLOAT,darkAo.data());
            glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,textures[2]);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,8,2,GL_RED,GL_FLOAT,depthPixels.data());
            drawComposite(8,2);expectPixel(2,0,1.f,"unsampled thin foreground fallback");expectPixel(3,0,.2f,"thin foreground background");
            std::array<float,7*3*4> oddScene{};for(int i=0;i<7*3;++i){oddScene[i*4]=.8f;oddScene[i*4+1]=.6f;oddScene[i*4+2]=.4f;oddScene[i*4+3]=1.f;}
            std::array<float,7*3> oddDepth{};oddDepth.fill(.5f);std::array<float,4*2> oddAo{};oddAo.fill(1.f);
            upload(textures[0],GL_RGBA32F,GL_RGBA,7,3,oddScene.data(),GL_NEAREST);upload(textures[1],GL_R32F,GL_RED,4,2,oddAo.data(),GL_LINEAR);upload(textures[2],GL_R32F,GL_RED,7,3,oddDepth.data(),GL_NEAREST);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,textures[0]);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,textures[1]);glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,textures[2]);
            drawComposite(7,3);expectPixel(6,2,1.f,"odd-size edge");
            const std::array<float,4> tinyScene{{.4f,.2f,.7f,1.f}};const float tinyDepth=.5f,tinyAo=0.f;
            upload(textures[0],GL_RGBA32F,GL_RGBA,1,1,tinyScene.data(),GL_NEAREST);upload(textures[1],GL_R32F,GL_RED,1,1,&tinyAo,GL_LINEAR);upload(textures[2],GL_R32F,GL_RED,1,1,&tinyDepth,GL_NEAREST);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,textures[0]);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,textures[1]);glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,textures[2]);
            drawComposite(1,1);std::array<unsigned char,4> tinyPixel{};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,tinyPixel.data());
            for(int c=0;c<4;++c)if(std::abs(int(tinyPixel[c])-int(std::lround(tinyScene[c]*(c==3?1.f:.2f)*255.f)))>3)throw std::runtime_error("AO composite failed for a 1x1 target");
            if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("AO composite generated an OpenGL error");
            glDeleteTextures(3,textures);glDeleteVertexArrays(1,&compositeVao);
        }
        std::cout<<"CHARACTER_SHADOW shaders="<<characterShaders.size()<<" lit="<<int(unshadowed[0])<<" shadow="<<int(shadowed[0])<<'\n';
        std::cout<<"POST_PIPELINE neutral="<<gray<<" msaa="<<msaa<<" hdr="<<hdr<<" bloom="<<bloom<<" resize=79x53 GPU="<<glGetString(GL_RENDERER)<<'\n';
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
