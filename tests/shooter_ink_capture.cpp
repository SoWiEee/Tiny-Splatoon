#include "../src/game/CombatWorld.hpp"
#include "../src/game/PlayerSimulation.hpp"
#include "../src/render/CharacterShader.hpp"
#include "../src/render/ProjectileRenderer.hpp"
#include "../src/render/TeamPalette.hpp"
#include "../src/render/WorldFxParticleRenderer.hpp"
#include "../src/render/WorldFxRecipes.hpp"
#include "../src/world/FacePaint.hpp"
#include "../src/world/StageGeometry.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr int W=1280,H=480,PW=W/2;
constexpr double DT=1.0/60.0;
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}

GLuint shader(GLenum type,const char* source){
    GLuint s=glCreateShader(type);glShaderSource(s,1,&source,nullptr);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(ok)return s;
    char log[2048]{};glGetShaderInfoLog(s,sizeof(log),nullptr,log);glDeleteShader(s);
    throw std::runtime_error(std::string("Floor shader compile failed: ")+log);
}
class MatteFloor {
public:
    MatteFloor(){
        static constexpr char vs[]=R"GLSL(#version 450 core
uniform mat4 uView,uProjection;
const vec3 v[6]=vec3[6](vec3(-8,0,-8),vec3(8,0,-8),vec3(8,0,8),vec3(-8,0,-8),vec3(8,0,8),vec3(-8,0,8));
void main(){gl_Position=uProjection*uView*vec4(v[gl_VertexID],1);})GLSL";
        static constexpr char fs[]=R"GLSL(#version 450 core
layout(location=0) out vec4 color; void main(){color=vec4(.24,.25,.27,1);})GLSL";
        const GLuint a=shader(GL_VERTEX_SHADER,vs),b=shader(GL_FRAGMENT_SHADER,fs);program_=glCreateProgram();
        glAttachShader(program_,a);glAttachShader(program_,b);glLinkProgram(program_);glDeleteShader(a);glDeleteShader(b);
        GLint ok=0;glGetProgramiv(program_,GL_LINK_STATUS,&ok);check(ok,"Floor shader link failed");
        view_=glGetUniformLocation(program_,"uView");projection_=glGetUniformLocation(program_,"uProjection");
        glGenVertexArrays(1,&vao_);
    }
    ~MatteFloor(){if(vao_)glDeleteVertexArrays(1,&vao_);if(program_)glDeleteProgram(program_);}
    MatteFloor(const MatteFloor&)=delete;MatteFloor& operator=(const MatteFloor&)=delete;
    void draw(const glm::mat4& v,const glm::mat4& p)const{
        glUseProgram(program_);glUniformMatrix4fv(view_,1,GL_FALSE,glm::value_ptr(v));
        glUniformMatrix4fv(projection_,1,GL_FALSE,glm::value_ptr(p));glBindVertexArray(vao_);glDrawArrays(GL_TRIANGLES,0,6);
    }
private: GLuint program_=0,vao_=0;GLint view_=-1,projection_=-1;
};

restoration::StageData makeStage(){
    using namespace restoration;StageData s{};s.id="shooter-ink-capture";s.name="Shooter Ink Capture";
    s.mode="turf";s.theme="diagnostic";s.water="none";s.bounds={-8,8,-8,8};
    s.spawnPads={glm::dvec3(-3,0,0),glm::dvec3(3,0,0)};
    BlockData b{};b.center={0,-.15,0};b.half={8,.15,8};
    b.axes={glm::dvec3(1,0,0),glm::dvec3(0,1,0),glm::dvec3(0,0,1)};b.faces.fill(-1);b.faces[2]=0;b.solid=true;s.blocks.push_back(b);
    FaceData f{};f.origin={-8,0,-8};f.u={1,0,0};f.v={0,0,1};f.normal={0,1,0};f.color={.24,.25,.27};
    f.width=f.height=16;f.block=0;f.paintable=f.turf=true;s.faces.push_back(f);return s;
}
struct Lane {
    restoration::CombatWorld* world;restoration::FacePaint* paint;restoration::PlayerSimulation* actor;
    restoration::ProjectileRenderer* renderer;std::vector<restoration::WorldFxParticle> fx;
    std::uint32_t rng;int hits=0;bool drew=false;
};
void launch(Lane& l,int i,const char* id,bool stream,const std::string& preset){
    if(preset=="explosion"){l.world->burst(*l.actor,{0,.15,0},2,50,10,2.2,"bomb");return;}
    const bool wide=preset=="bucket"||preset=="roller";
    const int count=wide?(preset=="bucket"?5:9):1;
    for(int blob=0;blob<count;blob++){
    using namespace restoration;ShooterProjectile p;p.owner=l.actor;p.weaponId=id;p.pos=p.start={0,1,-2};p.vel={0,-1,13};
    if(wide){const double side=double(blob)/(count-1)-.5;p.kind=preset=="bucket"?"slosh":"drop";p.vel={side*(preset=="bucket"?8:12),3.5,13};p.life=2;p.straight=0;}
    p.seed=double((i*37+blob*17+11)%97)/97;p.radius=.3;p.size=.15;p.straight=wide?0:.10;p.gravity=28;p.trailEvery=0;
    p.look={stream?.12:.15,1,1.2,.03,20,.2,3};l.world->launch(std::move(p));
    }
    if(wide){restoration::WorldFxRecipeContext c;c.speed=13;const bool native=std::string(id)!="reference";
        const char* recipe=native?(preset=="bucket"?"inkSlosh":"inkFlick"):"flickCurtain";
        auto fx=restoration::BuildWorldFxRecipe(recipe,{0,1,-2},{0,1,0},restoration::InkColors[0],1,l.rng,-1.6,{0,2,4},{},c,{0,.2,1});l.fx.insert(l.fx.end(),fx.begin(),fx.end());}
}
void impact(Lane& l,const restoration::WorldVisualEvent& e,bool shooter,bool native,glm::dvec3 eye){
    using namespace restoration;if(e.kind!="bulletImpact"&&e.kind!="explosion")return;++l.hits;std::vector<WorldFxParticle> spawned;
    if(e.kind=="explosion")spawned=BuildWorldFxRecipe(native?"inkExplosion":"explosion",e.pos,e.normal,InkColors[0],e.radius,l.rng,-1.6,eye);
    else if(native&&(e.source=="bucket"||e.source=="roller"))spawned=BuildWorldFxRecipe("inkImpact",e.pos,e.normal,InkColors[0],.5,l.rng,-1.6,eye);
    else if(shooter){
        check(e.source=="shooter","Shooter impact lost its visual source classification");
        auto seed=static_cast<std::uint32_t>(std::fmod(std::abs(e.seed),1.0)*4294967295.0)^static_cast<std::uint32_t>(e.id);
        if(!seed)seed=0x6d2b79f5u;
        spawned=BuildWorldFxRecipe("shooterImpact",e.pos,e.normal,InkColors[0],e.radius,seed,-1.6,eye);
    }
    else{WorldFxRecipeContext c;c.count=5;c.speed=3;c.size=.07;c.paint=false;
        spawned=BuildWorldFxRecipe("burst",e.pos,e.normal,InkColors[0],0,l.rng,-1.6,eye,{},c);}
    l.fx.insert(l.fx.end(),std::make_move_iterator(spawned.begin()),std::make_move_iterator(spawned.end()));
}
bool finite(const std::vector<restoration::WorldFxParticle>& fx){
    for(const auto& p:fx)if(!std::isfinite(p.pos.x)||!std::isfinite(p.pos.y)||!std::isfinite(p.pos.z)||
        !std::isfinite(p.vel.x)||!std::isfinite(p.vel.y)||!std::isfinite(p.vel.z)||!std::isfinite(p.alpha)||
        !std::isfinite(p.renderRadius))return false;return true;
}
struct Options {std::filesystem::path assets="assets",output="build/captures/shooter-ink";int frames=180;std::string weapon="shooter";};
Options options(int argc,char** argv){
    Options o;for(int i=1;i<argc;++i){std::string a=argv[i];
        if(a=="--assets"&&i+1<argc)o.assets=argv[++i];else if(a=="--output"&&i+1<argc)o.output=argv[++i];
        else if(a=="--frames"&&i+1<argc)o.frames=std::stoi(argv[++i]);
        else if(a=="--weapon"&&i+1<argc)o.weapon=argv[++i];
        else throw std::invalid_argument("Usage: shooter_ink_capture [--assets DIR] [--output DIR] [--frames N] [--weapon shooter|spinner|bucket|roller|explosion]");}
    check(o.weapon=="shooter"||o.weapon=="spinner"||o.weapon=="bucket"||o.weapon=="roller"||o.weapon=="explosion","Unsupported capture preset");
    check(o.frames>0&&o.frames<=3600,"Frame count must be in 1..3600");return o;
}
}

int main(int argc,char** argv){
    if(!glfwInit())return 1;glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_RESIZABLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(W,H,"Shooter ink comparison",nullptr,nullptr);if(!window){glfwTerminate();return 1;}
    glfwMakeContextCurrent(window);if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))){glfwDestroyWindow(window);glfwTerminate();return 1;}
    int result=0;
    try{
        const auto o=options(argc,argv);std::filesystem::create_directories(o.output);
        {
            using namespace restoration;auto stage=makeStage();StageGeometry geometry(stage);
            FacePaint lp(stage,geometry),sp(stage,geometry);CombatWorld lw(stage,geometry,lp),sw(stage,geometry,sp);
            PlayerSimulation la(stage,geometry,lp,0,&lw),sa(stage,geometry,sp,0,&sw);la.state.alive=sa.state.alive=true;
            la.setLoadout(o.weapon=="explosion"?"shooter":o.weapon);sa.setLoadout(o.weapon=="explosion"?"shooter":o.weapon);const bool stream=o.weapon=="spinner",wide=o.weapon=="bucket"||o.weapon=="roller"||o.weapon=="explosion";
            if(stream){sa.state.pos={0,0,-2};sa.setMuzzleSource([](int){return glm::dvec3(0,1,-2);});}
            ProjectileRenderer lr(o.assets/"models/projectiles",false,o.assets/"shaders"),sr(o.assets/"models/projectiles",false,o.assets/"shaders");
            WorldFxParticleRenderer fx(o.assets/"effects/particles",o.assets/"models/boss/shaders");MatteFloor floor;
            Lane lanes[2]={{&lw,&lp,&la,&lr,{},0x13579BDFu},{&sw,&sp,&sa,&sr,{},0x13579BDFu}};
            CharacterLightState light;light.ambient={.22f,.22f,.22f};light.sunDirection=glm::normalize(glm::vec3(-.45f,.82f,.34f));
            light.sunColor={1,.92f,.82f};light.skyColor={.25f,.31f,.42f};light.groundColor={.24f,.21f,.19f};
            std::vector<unsigned char> pixels(std::size_t(W)*H*3);std::size_t visible=0;stbi_flip_vertically_on_write(1);
            glPixelStorei(GL_PACK_ALIGNMENT,1);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
            for(int frame=0;frame<o.frames;++frame){
                if(stream){CombatPose pose;pose.streaming=frame<90;sa.setNetworkGhostPose(pose,{});}
                const int interval=wide?36:stream?3:6;
                if(frame<90&&frame%interval==0){launch(lanes[0],frame/interval,"reference",stream,o.weapon);launch(lanes[1],frame/interval,o.weapon.c_str(),stream,o.weapon);}
                const double t=frame*DT,angle=.35+.45*double(frame)/std::max(1,o.frames-1);
                const glm::dvec3 eye(std::cos(angle)*4.2,2.7,std::sin(angle)*4.2),target(0,.45,0);
                const auto vd=glm::lookAt(eye,target,glm::dvec3(0,1,0));
                const auto pd=glm::perspective(glm::radians(48.0),double(PW)/H,.08,60.0);const glm::mat4 v(vd),p(pd);
                for(auto& l:lanes){l.world->tick(DT);const auto events=l.world->takeVisualEvents();l.renderer->Update(DT,BuildProjectileFrame(*l.world),l.world->geometry(),events);
                    for(const auto& e:events)impact(l,e,o.weapon=="shooter"&&l.actor==&sa,l.actor==&sa,eye);UpdateWorldFxRecipe(l.fx,DT,l.rng,{},-1.6f,false,false);
                    check(finite(l.fx),"World FX particle contains a non-finite value");}
                glViewport(0,0,W,H);glClearColor(.055f,.062f,.078f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                for(int side=0;side<2;++side){glViewport(side*PW,0,PW,H);floor.draw(v,p);
                    lanes[side].drew|=lanes[side].renderer->Draw(vd,pd,eye,light)>0;
                    lanes[side].drew|=fx.Draw(lanes[side].fx,{},v,p,glm::vec3(eye),&light,t)>0;}
                glReadPixels(0,0,W,H,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
                for(std::size_t i=0;i<pixels.size();i+=3)if(pixels[i]>28||pixels[i+1]>30||pixels[i+2]>34)++visible;
                std::ostringstream name;name<<"frame-"<<std::setw(4)<<std::setfill('0')<<frame<<".png";const auto file=o.output/name.str();
                check(stbi_write_png(file.string().c_str(),W,H,3,pixels.data(),W*3)!=0,"Could not write "+file.string());
                const GLenum err=glGetError();check(err==GL_NO_ERROR,"OpenGL error during capture: "+std::to_string(err));
            }
            check(lanes[0].hits>0&&lanes[1].hits>0,"Both lanes must register a real bulletImpact event");
            check(lanes[0].hits==lanes[1].hits&&lp.version()>0&&lp.version()==sp.version(),"Shooter visuals must preserve collision and paint results");
            check(lanes[0].drew&&lanes[1].drew,"Both projectile lanes must draw");
            check(visible>std::size_t(o.frames)*W*H/12,"Capture produced too few non-background pixels");
            std::cout<<"SHOOTER_INK_CAPTURE renderer="<<reinterpret_cast<const char*>(glGetString(GL_RENDERER))
                <<" weapon="<<o.weapon<<" frames="<<o.frames<<" legacy_hits="<<lanes[0].hits<<" shooter_hits="<<lanes[1].hits
                <<" paint_versions="<<lp.version()<<','<<sp.version()<<" nonbackground_pixels="<<visible
                <<" output="<<o.output.string()<<'\n';
        } // GL resources are released while the context is still current.
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
