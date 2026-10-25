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
#ifdef TINY_RENDERDOC_CAPTURE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <renderdoc_app.h>
#endif

namespace {
PFNGLBUFFERDATAPROC originalBufferData=nullptr;
std::size_t instanceUploads=0;
void APIENTRY CountBufferData(GLenum target,GLsizeiptr size,const void* data,GLenum usage){
    if(target==GL_ARRAY_BUFFER&&usage==GL_STREAM_DRAW)++instanceUploads;
    originalBufferData(target,size,data,usage);
}
struct UploadProbe {
    UploadProbe(){originalBufferData=glad_glBufferData;instanceUploads=0;glad_glBufferData=CountBufferData;}
    ~UploadProbe(){glad_glBufferData=originalBufferData;}
};
}

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
        ShooterProjectile shot;shot.owner=&actor;shot.weaponId="twins";shot.pos={0,0,0};shot.start={-3,0,0};shot.vel={28,0,2};shot.age=.12;shot.seed=.31;shot.look={.19,.8,1.3,.035,26,.3,3};world.launch(shot);
        ProjectileRenderer renderer(argc>1?argv[1]:"assets/reference/projectiles");renderer.Update(1.0/60,world,{});
        if(renderer.blobs().size()!=4)throw std::runtime_error("Shot plus satellite draw instances missing");
        CharacterLightState lights;lights.ambient={.3,.3,.3};lights.sunDirection=glm::normalize(glm::vec3(-.4,.85,.35));lights.sunColor={1,.92,.82};
        const glm::dvec3 eye(0,.5,4);const auto view=glm::lookAt(eye,glm::dvec3(0),glm::dvec3(0,1,0));const auto projection=glm::perspective(glm::radians(45.0),1.0,.05,50.0);
#ifdef TINY_RENDERDOC_CAPTURE
        RENDERDOC_API_1_6_0* capture=nullptr;
        if(argc>3){
            const auto module=GetModuleHandleA("renderdoc.dll");
            const auto getApi=module?reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module,"RENDERDOC_GetAPI")):nullptr;
            if(!getApi||getApi(eRENDERDOC_API_Version_1_6_0,reinterpret_cast<void**>(&capture))!=1)
                throw std::runtime_error("Launch this diagnostic through renderdoccmd capture to record a frame");
            capture->SetCaptureFilePathTemplate(argv[3]);capture->StartFrameCapture(nullptr,nullptr);
        }
#else
        if(argc>3)throw std::runtime_error("RenderDoc capture requires the optional RenderDoc header on Windows");
#endif
        glViewport(0,0,256,256);glEnable(GL_DEPTH_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        UploadProbe uploadProbe;
        if(renderer.Draw(view,projection,eye,lights)!=1)throw std::runtime_error("No source instanced projectile draw");
        std::vector<unsigned char> pixels(256*256*4);glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        std::size_t colored=0;for(std::size_t i=0;i<pixels.size();i+=4)if(pixels[i]>20||pixels[i+1]>20||pixels[i+2]>20)++colored;
        const auto drawError=glGetError();
        if(drawError!=GL_NO_ERROR)throw std::runtime_error("Source projectile draw GL error "+std::to_string(drawError));
        if(colored<60)throw std::runtime_error("Source projectile draw changed too few pixels: "+std::to_string(colored));
        if(argc>2){std::filesystem::create_directories(std::filesystem::path(argv[2]).parent_path());stbi_flip_vertically_on_write(1);if(!stbi_write_png(argv[2],256,256,4,pixels.data(),256*4))throw std::runtime_error("Cannot save projectile evidence");}
        renderer.DrawShadow(projection*view,lights);renderer.Draw(view,projection,eye,lights);
        if(instanceUploads!=1)throw std::runtime_error("Shadow and color passes must share one snapshot upload");
#ifdef TINY_RENDERDOC_CAPTURE
        if(capture&&!capture->EndFrameCapture(nullptr,nullptr))throw std::runtime_error("RenderDoc did not save the projectile capture");
#endif
        renderer.Update(0,world,{});renderer.Draw(view,projection,eye,lights);
        if(instanceUploads!=2)throw std::runtime_error("A changed snapshot must invalidate its GPU upload");
        world.clear();auto inkShot=shot;inkShot.weaponId="shooter";inkShot.seed=.83;inkShot.age=.22;world.launch(inkShot);renderer.Update(0,world,{});
        for(int angle=0;angle<2;angle++){
            const glm::dvec3 inkEye=angle?glm::dvec3(4,.5,0):eye;
            const auto inkView=glm::lookAt(inkEye,glm::dvec3(0),glm::dvec3(0,1,0));
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            if(renderer.Draw(inkView,projection,inkEye,lights)!=1)throw std::runtime_error("Shooter ink must submit one instanced batch");
            glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            std::size_t inkPixels=0;for(std::size_t i=0;i<pixels.size();i+=4)if(pixels[i]>20||pixels[i+1]>20||pixels[i+2]>20)++inkPixels;
            if(inkPixels<60||glGetError()!=GL_NO_ERROR)throw std::runtime_error("Shooter volume missing from front or side");
            if(argc>2){const auto path=std::filesystem::path(argv[2]).parent_path()/(angle?"shooter-volume-side.png":"shooter-volume-front.png");stbi_write_png(path.string().c_str(),256,256,4,pixels.data(),256*4);}
        }
        auto mixedShot=shot;mixedShot.pos={-.5,0,0};world.launch(mixedShot);renderer.Update(0,world,{});
        const auto mixedUploads=instanceUploads;
        if(renderer.Draw(view,projection,eye,lights)!=2||instanceUploads!=mixedUploads+1)
            throw std::runtime_error("Legacy and shooter batches must share one upload");
        renderer.DrawShadow(projection*view,lights);
        if(instanceUploads!=mixedUploads+1)throw std::runtime_error("Mixed shader shadow pass uploaded the same snapshot twice");
        {
            auto fused=inkShot;fused.seed=.20;fused.life=.6;fused.age=.08;
            const glm::dvec3 closeEye(0,.15,2.4);
            const auto closeView=glm::lookAt(closeEye,glm::dvec3(0),glm::dvec3(0,1,0));
            const auto readInk=[&](){
                glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                std::size_t count=0;for(std::size_t i=0;i<pixels.size();i+=4)if(pixels[i]>20||pixels[i+1]>20||pixels[i+2]>20)++count;
                return count;
            };
            const auto components=[&](){
                std::vector<unsigned char> visited(256*256);int count=0;
                for(int start=0;start<256*256;start++){
                    const auto lit=[&](int i){return pixels[i*4]>20||pixels[i*4+1]>20||pixels[i*4+2]>20;};
                    if(visited[start]||!lit(start))continue;
                    std::vector<int> queue{start};visited[start]=1;
                    for(std::size_t q=0;q<queue.size();q++){
                        const int i=queue[q],x=i%256,y=i/256;
                        for(const int next:{x>0?i-1:-1,x<255?i+1:-1,y>0?i-256:-1,y<255?i+256:-1})
                            if(next>=0&&!visited[next]&&lit(next)){visited[next]=1;queue.push_back(next);}
                    }
                    if(queue.size()>4)++count;
                }
                return count;
            };
            for(int phase=0;phase<3;phase++){
                fused.age=phase==2?.4:phase==1?.19:.08;world.clear();world.launch(fused);renderer.Update(0,world,{});
                glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);renderer.Draw(closeView,projection,closeEye,lights);
                if(readInk()<100||components()!=(phase==2?2:1))throw std::runtime_error("SDF must join early lobes and detach the tail later");
                if(argc>2){const auto path=std::filesystem::path(argv[2]).parent_path()/(phase==2?"shooter-sdf-breakup.png":phase==1?"shooter-sdf-neck.png":"shooter-sdf-fused.png");stbi_write_png(path.string().c_str(),256,256,4,pixels.data(),256*4);}
            }
            fused.age=.08;world.clear();world.launch(fused);renderer.Update(0,world,{});
            const auto planeDepth=[&](double z){const auto clip=projection*closeView*glm::dvec4(0,0,z,1);return clip.z/clip.w*.5+.5;};
            glClearDepth(planeDepth(0));glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            renderer.Draw(closeView,projection,closeEye,lights);
            if(readInk()<40)throw std::runtime_error("SDF surface depth must pass a plane behind the ink front");
            glClearDepth(planeDepth(.32));glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            renderer.Draw(closeView,projection,closeEye,lights);
            if(readInk()!=0)throw std::runtime_error("An opaque plane in front must fully occlude the SDF");
            glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            const glm::dvec3 insideEye(0,0,.35);const auto insideView=glm::lookAt(insideEye,glm::dvec3(0),glm::dvec3(0,1,0));
            renderer.Draw(insideView,projection,insideEye,lights);
            if(readInk()<100)throw std::runtime_error("Camera inside the proxy lost the ink surface");
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            renderer.Draw(closeView,glm::ortho(-.7,.7,-.7,.7,.05,50.0),closeEye,lights);
            if(readInk()<100||glGetError()!=GL_NO_ERROR)throw std::runtime_error("Orthographic SDF ray/depth reconstruction failed");
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            const glm::dvec3 farEye(0,0,30);
            renderer.Draw(glm::lookAt(farEye,glm::dvec3(0),glm::dvec3(0,1,0)),projection,farEye,lights);
            if(readInk()==0||glGetError()!=GL_NO_ERROR)throw std::runtime_error("Small projected ink must remain visible through the mesh LOD");
            GLint cullMode=0;glGetIntegerv(GL_CULL_FACE_MODE,&cullMode);
            if(cullMode!=GL_BACK)throw std::runtime_error("SDF proxy leaked front-face culling into other renderers");
        }
        {
            world.clear();actor.setLoadout("spinner");actor.setMuzzleSource([](int){return glm::dvec3(0);});
            CombatPose pose;pose.streaming=true;actor.setNetworkGhostPose(pose,{});
            auto streamShot=shot;streamShot.weaponId="spinner";streamShot.pos=glm::dvec3(0);streamShot.vel={0,0,20};world.launch(streamShot);
            for(int i=0;i<20;i++)renderer.Update(1.0/60,world,{});
            const glm::dvec3 streamEye(3,1,3);const auto streamView=glm::lookAt(streamEye,glm::dvec3(0,0,1),glm::dvec3(0,1,0));
            if(!renderer.blobs().empty()||renderer.Draw(streamView,projection,streamEye,lights)<1)throw std::runtime_error("Actual spinner streaming pose failed to reach the jet renderer");
            pose.streaming=false;actor.setNetworkGhostPose(pose,{});
            for(int i=0;i<100;i++)renderer.Update(1.0/60,world,{});
            if(renderer.Draw(streamView,projection,streamEye,lights)!=0)throw std::runtime_error("Stopped spinner retained a stale stream");
            actor.setLoadout("shooter");actor.setMuzzleSource({});actor.setNetworkGhostPose({},{});
        }
        world.clear();world.launch(shot);renderer.Update(0,world,{});
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
        const auto checkShadow=[&](const char* kind,int expectedDraws,bool expectedDepth,bool shooterInk=false){
            world.clear();auto caster=shot;caster.kind=kind;caster.weaponId=kind;caster.state="fly";caster.pos={0,0,0};caster.vel={28,0,2};caster.flightDir=glm::normalize(caster.vel);caster.age=.12;caster.distance=3;caster.tier=2;
            if(shooterInk)caster.weaponId="shooter";
            world.launch(caster);renderer.Update(0,world,{});glClearDepth(1.0);glClear(GL_DEPTH_BUFFER_BIT);
            const auto draws=renderer.DrawShadow(projection*view,lights);
            std::vector<float> depth(256*256);glReadPixels(0,0,256,256,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
            const auto covered=std::count_if(depth.begin(),depth.end(),[](float value){return value<.9999f;});
            if(draws!=expectedDraws||((covered>0)!=expectedDepth)||glGetError()!=GL_NO_ERROR)
                throw std::runtime_error(std::string("Projectile source shadow caster mismatch: ")+kind+" draws="+std::to_string(draws)+" covered="+std::to_string(covered));
        };
        checkShadow("shot",1,true);checkShadow("arrow",2,true);checkShadow("fist",1,true);checkShadow("bladewave",0,false);
        checkShadow("shot",1,true,true);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteTextures(1,&shadowDepth);glDeleteFramebuffers(1,&shadowFramebuffer);
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Source projectile renderer GL error");
        std::cout<<"PROJECTILE_GPU source_teardrop=4 visible_pixels="<<colored<<" snapshot_upload_reuse=pass ribbon_lifecycle=pass renderer="<<glGetString(GL_RENDERER)<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
