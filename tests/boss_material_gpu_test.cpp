#include "BossRenderer.hpp"
#include "MatchSimulation.hpp"
#include "ReferenceData.hpp"
#include "../src/network/AuthorityState.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Pixels=std::vector<unsigned char>;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
const restoration::StageData& bossStage(){
    for(const auto& stage:restoration::ReferenceStages())if(stage.id=="tidewater"&&stage.mode=="turf"&&!stage.onlineOnly)return stage;
    throw std::runtime_error("Missing legal offline Boss stage");
}
Pixels render(restoration::BossRenderer& renderer,const restoration::BossSimulation& boss,const glm::mat4& view,const glm::mat4& projection,glm::vec3 camera,int* drawCount=nullptr){
    glViewport(0,0,320,320);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glClearColor(.03f,.04f,.05f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    const int count=renderer.Draw(boss,view,projection,camera,glm::vec3(.02f,.08f,1));require(count>=5,"Source Boss materials did not draw all hull buckets");if(drawCount)*drawCount=count;
    Pixels pixels(320*320*4);glReadPixels(0,0,320,320,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    require(glGetError()==GL_NO_ERROR,"Source Boss material draw generated an OpenGL error");return pixels;
}
Pixels renderHazards(restoration::BossRenderer& renderer,const restoration::BossSimulation& boss,const restoration::StageGeometry& geometry,const glm::mat4& viewProjection){
    glViewport(0,0,320,320);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glClearColor(.03f,.04f,.05f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    require(renderer.DrawHazards(boss,geometry,viewProjection,glm::vec3(1,0,0))>0,"Source Boss hazard geometry did not draw");
    Pixels pixels(320*320*4);glReadPixels(0,0,320,320,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    require(glGetError()==GL_NO_ERROR,"Source Boss hazard draw generated an OpenGL error");return pixels;
}
std::size_t visiblePixels(const Pixels& pixels){std::size_t count=0;for(std::size_t i=0;i<pixels.size();i+=4)if(pixels[i]>24||pixels[i+1]>24||pixels[i+2]>24)++count;return count;}
std::size_t changedPixels(const Pixels& a,const Pixels& b){std::size_t count=0;for(std::size_t i=0;i<a.size();i+=4)if(std::abs(int(a[i])-int(b[i]))+std::abs(int(a[i+1])-int(b[i+1]))+std::abs(int(a[i+2])-int(b[i+2]))>3)++count;return count;}
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
        int hullDrawCount=0;const auto baseline=render(renderer,*match.boss(),glm::mat4(view),glm::mat4(projection),glm::vec3(eye),&hullDrawCount);
        match.paused=true;BossSegmentHit hit;require(match.boss()->hit(match.local(),1.0,hit,"shooter")>0,"Boss test hit was not accepted");renderer.Update(*match.boss());
        const auto flashed=render(renderer,*match.boss(),glm::mat4(view),glm::mat4(projection),glm::vec3(eye));
        std::size_t visible=0,changed=0;std::uint64_t difference=0;
        for(std::size_t i=0;i<baseline.size();i+=4){if(baseline[i]>24||baseline[i+1]>24||baseline[i+2]>24)++visible;bool pixelChanged=false;for(std::size_t c=0;c<3;++c){const auto delta=std::abs(int(baseline[i+c])-int(flashed[i+c]));difference+=static_cast<std::uint64_t>(delta);pixelChanged|=delta>2;}if(pixelChanged)++changed;}
        require(visible>250,"Source Boss render changed too few pixels");require(changed>80&&difference>4000,"Live Boss material uniforms did not change rendered pixels");
        match.paused=false;match.boss()->forceMove(BossMoveId::Slam);match.tick(.1);
        require(!match.boss()->moves().empty()&&match.boss()->moves().back().id==BossMoveId::Slam,"Boss slam did not enter its hazard timeline");
        const auto& slam=match.boss()->moves().back();const glm::dvec3 hazardTarget=slam.point+glm::dvec3(0,.2,0),hazardEye=slam.point+glm::dvec3(18,15,18);
        const auto hazardView=glm::lookAt(hazardEye,hazardTarget,glm::dvec3(0,1,0)),hazardProjection=glm::perspective(glm::radians(70.0),1.0,.1,100.0);
        const auto markPixels=renderHazards(renderer,*match.boss(),geometry,glm::mat4(hazardProjection*hazardView));
        require(visiblePixels(markPixels)>80,"Source Boss slam telegraph produced no visible floor geometry");
        for(int i=0;i<17;++i)match.tick(.1);
        const auto ringPixels=renderHazards(renderer,*match.boss(),geometry,glm::mat4(hazardProjection*hazardView));
        require(visiblePixels(ringPixels)>80&&changedPixels(markPixels,ringPixels)>0,"Source Boss clipped shockwave ring did not render as a moving hazard area");

        const auto placeTargetOutsidePad=[&](MatchSimulation& candidate){
            auto& boss=*candidate.boss();auto& actor=candidate.local();
            for(const int cell:boss.nav().planIds()){
                const auto [x,z]=boss.nav().xz(cell);if(boss.nav().inPad(x,z,3.4))continue;
                const double ground=geometry.groundHeight(x,z,boss.state().pos.y+10);
                if(!std::isfinite(ground)||geometry.pointInside({x,ground+.3,z}))continue;
                actor.state.pos=glm::dvec3(x,ground,z);actor.state.vel=glm::dvec3(0);return true;
            }
            return false;
        };
        const auto placeTargetForSweep=[&](MatchSimulation& candidate){
            auto& boss=*candidate.boss();auto& actor=candidate.local();const auto origin=boss.state().pos;
            for(const double offset:{0.0,.04,-.04,.08,-.08,.12,-.12})for(const double distance:{6.0,8.0,10.0}){
                const double yaw=boss.state().yaw+offset,x=origin.x+std::sin(yaw)*distance,z=origin.z+std::cos(yaw)*distance;
                if(boss.nav().inPad(x,z,3.4))continue;
                const double ground=geometry.groundHeight(x,z,origin.y+10);
                if(!std::isfinite(ground)||geometry.pointInside({x,ground+.3,z}))continue;
                actor.state.pos=glm::dvec3(x,ground,z);actor.state.vel=glm::dvec3(0);return true;
            }
            return false;
        };
        const auto captureMoveHazardPair=[&](BossMoveId id,const char* name,const auto& placeTarget){
            FacePaint movePaint(stage,geometry,2048);NavGraph moveNav(stage,geometry);
            MatchSimulation moveMatch(stage,geometry,movePaint,moveNav,options);BossRenderer moveRenderer(argv[1]);
            moveMatch.startPlaying();
            for(int step=0;step<20&&!moveMatch.boss()->state().visible;++step)moveMatch.tick(.1);
            require(moveMatch.boss()->state().visible,(std::string("Fresh Boss match did not reach its visible state for ")+name).c_str());
            for(int step=0;step<100&&moveMatch.boss()->state().anim!=0;++step)moveMatch.boss()->tick(.05,true);
            require(moveMatch.boss()->state().anim==0,(std::string("Fresh Boss intro did not finish for ")+name).c_str());
            moveRenderer.Update(*moveMatch.boss());
            require(moveMatch.local().state.alive,(std::string("Fresh Boss owner is not alive for ")+name).c_str());
            auto& boss=*moveMatch.boss();BossMove move;bool found=false;
            const double requestedAt=boss.state().clock;
            if(id==BossMoveId::Charge){
                const auto& nav=boss.nav();double length=0,bossX=0,bossZ=0,bossYaw=0;
                for(const int cell:nav.planIds()){
                    const auto [x,z]=nav.xz(cell);
                    for(int heading=0;heading<360;heading+=5){
                        const double yaw=glm::radians(static_cast<double>(heading));
                        if(!nav.poseOk(x,z,yaw)||!nav.turnOk(x,z,yaw))continue;
                        const auto cast=nav.cast(x,z,yaw,34);
                        if(cast.dist>=6&&cast.dist>length){length=cast.dist;bossX=x;bossZ=z;bossYaw=yaw;}
                    }
                }
                require(length>=6,"Boss stage has no legal full-circle Charge origin");
                auto snapshot=boss.captureAuthorityState();
                snapshot.state.pos={bossX,nav.floorAt(bossX,bossZ),bossZ};snapshot.state.vel=glm::dvec3(0);snapshot.state.yaw=bossYaw;snapshot.state.turn=0;
                snapshot.state.target=-1;snapshot.state.anim=0;snapshot.state.stunned=false;snapshot.state.invulnerable=false;
                snapshot.move.reset();snapshot.moves.clear();snapshot.crablets.clear();
                require(boss.adoptAuthorityState(snapshot),"Boss rejected its validated Charge origin");
                const auto chargeCast=nav.cast(bossX,bossZ,bossYaw,34);
                require(nav.isPlan(bossX,bossZ)&&nav.poseOk(bossX,bossZ,bossYaw)&&nav.turnOk(bossX,bossZ,bossYaw)&&chargeCast.dist>=6,
                        "Adopted Boss Charge origin failed source navigation checks");
                moveRenderer.Update(boss);moveMatch.local().state.invuln=1.0e6;
                bool targetPlaced=false;
                for(double distance=std::min(10.0,chargeCast.dist-.5);distance>=2.5&&!targetPlaced;distance-=.25){
                    const double x=bossX+std::sin(bossYaw)*distance,z=bossZ+std::cos(bossYaw)*distance;
                    if(!nav.isPlan(x,z)||nav.inPad(x,z,3.4))continue;
                    const double ground=geometry.groundHeight(x,z,boss.state().pos.y+10);
                    if(!std::isfinite(ground)||geometry.pointInside({x,ground+.3,z}))continue;
                    moveMatch.local().state.pos={x,ground,z};moveMatch.local().state.vel=glm::dvec3(0);targetPlaced=true;
                }
                require(targetPlaced,"Could not place a living Boss target down the validated Charge lane");
                for(int step=0;step<600&&!found;++step){
                    boss.forceMove(id);boss.tick(.05,true);moveRenderer.Update(boss);
                    for(auto it=boss.moves().rbegin();it!=boss.moves().rend();++it)
                        if(it->id==id&&it->t0>=requestedAt-.001){move=*it;found=true;break;}
                }
            }else{
                require(placeTarget(moveMatch),(std::string("Could not place Boss target for ")+name+" hazard test").c_str());
                boss.forceMove(id);
                for(int step=0;step<600&&!found;++step){
                    boss.tick(.05,true);moveRenderer.Update(boss);
                    for(auto it=boss.moves().rbegin();it!=boss.moves().rend();++it)
                        if(it->id==id&&it->t0>=requestedAt-.001){move=*it;found=true;break;}
                }
            }
            require(found,(std::string("Forced source-valid Boss ")+name+" move did not start (alive="+std::to_string(moveMatch.local().state.alive)+", target="+std::to_string(boss.state().target)+", clock="+std::to_string(boss.state().clock)+", moves="+std::to_string(boss.moves().size())+")").c_str());
            const auto advanceTo=[&](double u){
                int step=0;while(moveMatch.boss()->state().clock-move.t0<u&&step++<200){moveMatch.boss()->tick(.05,true);moveRenderer.Update(*moveMatch.boss());}
                require(moveMatch.boss()->state().clock-move.t0>=u-.001,(std::string("Boss ")+name+" move did not reach requested phase").c_str());
            };
            glm::dvec3 focus=move.point;
            if(id==BossMoveId::Barrage){
                require(!move.barrels.empty(),"Boss Barrage must keep its source barrel targets");
                const auto& barrel=move.barrels.front();focus=glm::dvec3(barrel.x,barrel.y,barrel.z);
            }else if(id==BossMoveId::Sweep){
                const double angle=(move.a0+move.a1)*.5;focus=glm::dvec3(move.origin.x+std::sin(angle)*7,move.point.y,move.origin.z+std::cos(angle)*7);
            }else if(id==BossMoveId::Charge){
                const glm::dvec3 forward(std::sin(move.yaw),0,std::cos(move.yaw));focus=move.point+forward*std::min(8.0,move.length*.4);
            }
            const glm::dvec3 hazardEye=focus+glm::dvec3(0,28,0);
            const auto hazardView=glm::lookAt(hazardEye,focus,glm::dvec3(0,0,-1));
            const auto hazardOrtho=glm::ortho(-12.0,12.0,-12.0,12.0,.1,70.0);
            const glm::mat4 hazardVp=glm::mat4(hazardOrtho*hazardView);
            advanceTo(move.tele*.8);
            const auto telegraph=renderHazards(moveRenderer,*moveMatch.boss(),geometry,hazardVp);
            require(visiblePixels(telegraph)>40,(std::string("Source Boss ")+name+" telegraph area did not produce visible pixels").c_str());
            advanceTo(move.tele+std::min(.45,move.act*.3));
            const auto active=renderHazards(moveRenderer,*moveMatch.boss(),geometry,hazardVp);
            require(visiblePixels(active)>40&&changedPixels(telegraph,active)>15,
                    (std::string("Source Boss ")+name+" active hazard did not change its rendered geometry").c_str());
        };
        captureMoveHazardPair(BossMoveId::Barrage,"Barrage",placeTargetOutsidePad);
        captureMoveHazardPair(BossMoveId::Sweep,"Sweep",placeTargetForSweep);
        captureMoveHazardPair(BossMoveId::Frenzy,"Frenzy",placeTargetOutsidePad);
        captureMoveHazardPair(BossMoveId::Charge,"Charge",placeTargetOutsidePad);

        FacePaint crabPaint(stage,geometry,2048);NavGraph crabNav(stage,geometry);
        MatchSimulation crabMatch(stage,geometry,crabPaint,crabNav,options);BossRenderer crabRenderer(argv[1]);
        crabMatch.startPlaying();
        for(int step=0;step<20&&!crabMatch.boss()->state().visible;++step)crabMatch.tick(.1);
        require(crabMatch.boss()->state().visible,"Fresh Boss crablet match did not reach its visible state");
        for(int step=0;step<100&&crabMatch.boss()->state().anim!=0;++step)crabMatch.boss()->tick(.05,true);
        require(crabMatch.boss()->state().anim==0,"Fresh Boss crablet intro did not finish");
        require(placeTargetOutsidePad(crabMatch),"Could not place live Boss crablet hit owner outside its spawn pad");
        require(crabMatch.local().state.alive,"Fresh Boss crablet hit owner is not alive");
        crabRenderer.Update(*crabMatch.boss());
        const auto advanceCrabletBoss=[&](double dt){crabMatch.boss()->tick(dt,true);crabRenderer.Update(*crabMatch.boss());};
        const double crabMoveRequestedAt=crabMatch.boss()->state().clock;crabMatch.boss()->forceMove(BossMoveId::Crablets);
        BossMove crabMove;bool crabMoveStarted=false;
        for(int step=0;step<600&&!crabMoveStarted;++step){
            advanceCrabletBoss(.05);
            for(auto it=crabMatch.boss()->moves().rbegin();it!=crabMatch.boss()->moves().rend();++it)
                if(it->id==BossMoveId::Crablets&&it->t0>=crabMoveRequestedAt-.001){crabMove=*it;crabMoveStarted=true;break;}
        }
        require(crabMoveStarted,"Boss Crablets move did not start after its telegraph request");
        for(auto& actor:crabMatch.actors())actor->spawnAt(stage.spawnPads[0],0.0);
        while(crabMatch.boss()->state().clock-crabMove.t0<crabMove.tele*.8)advanceCrabletBoss(.05);
        require(crabMove.phase(crabMatch.boss()->state().clock)==BossMovePhase::Telegraph&&crabMatch.boss()->crablets().empty(),
                "Crablet models must stay hidden during the source move telegraph");
        while(crabMatch.boss()->state().clock-crabMove.t0<crabMove.tele+.05)advanceCrabletBoss(.05);
        require(crabMove.phase(crabMatch.boss()->state().clock)==BossMovePhase::Active&&!crabMatch.boss()->crablets().empty(),
                "Source Crablets must enter the active phase before their live models spawn");
        require(!crabMatch.boss()->crablets().empty(),"Source Boss crablet move did not spawn live models");
        const auto crabAt=crabMatch.boss()->crablets().front().pos;const glm::dvec3 crabEye=crabAt+glm::dvec3(2.5,1.7,3.5),crabTarget=crabAt+glm::dvec3(0,.22,0);
        const auto crabView=glm::lookAt(crabEye,crabTarget,glm::dvec3(0,1,0)),crabProjection=glm::perspective(glm::radians(48.0),1.0,.05,30.0);
        int crabDrawCount=0;const auto crabFrame0=render(crabRenderer,*crabMatch.boss(),glm::mat4(crabView),glm::mat4(crabProjection),glm::vec3(crabEye),&crabDrawCount);
        advanceCrabletBoss(.1);const auto crabFrame1=render(crabRenderer,*crabMatch.boss(),glm::mat4(crabView),glm::mat4(crabProjection),glm::vec3(crabEye));
        require(crabDrawCount>hullDrawCount,"Source live crablet meshes were not included in Boss draw calls");
        require(visiblePixels(crabFrame0)>30&&changedPixels(crabFrame0,crabFrame1)>0,"Source live crablet pose did not change rendered frames");
        for(std::size_t i=0;i<crabMatch.boss()->crablets().size();++i){
            BossSegmentHit crabHit;crabHit.crablet=static_cast<int>(i);const auto& crab=crabMatch.boss()->crablets()[i];
            const double dealt=crabMatch.boss()->hit(crabMatch.local(),30,crabHit,"shooter");
            require(dealt==30,(std::string("Crablet pop test hit was not accepted: index=")+std::to_string(i)+
                " dealt="+std::to_string(dealt)+" hp="+std::to_string(crab.hp)+" crabDead="+std::to_string(crab.dead)+
                " actorAlive="+std::to_string(crabMatch.local().state.alive)+" actorHp="+std::to_string(crabMatch.local().state.hp)+
                " matchPhase="+std::to_string(static_cast<int>(crabMatch.phase()))+" remaining="+std::to_string(crabMatch.remaining())+
                " bossDead="+std::to_string(crabMatch.boss()->state().dead)).c_str());
        }crabRenderer.Update(*crabMatch.boss());
        advanceCrabletBoss(.08);int popDrawCount=0;
        require(crabRenderer.TakeCrabletPopEvents().empty(),"Crablet ink burst must wait until the source 0.14 second pop window ends");
        const auto popFrame=render(crabRenderer,*crabMatch.boss(),glm::mat4(crabView),glm::mat4(crabProjection),glm::vec3(crabEye),&popDrawCount);
        require(popDrawCount>hullDrawCount&&visiblePixels(popFrame)>30&&changedPixels(crabFrame1,popFrame)>0,"Source crablet swell and flash were absent inside the 0.14 second pop window");
        advanceCrabletBoss(.07);int hiddenDrawCount=0;
        const auto popEvents=crabRenderer.TakeCrabletPopEvents();
        require(popEvents.size()==crabMatch.boss()->crablets().size(),"Each crablet must emit one BossFX burst when its source pop finishes");
        for(const auto& position:popEvents)
            require(std::any_of(crabMatch.boss()->crablets().begin(),crabMatch.boss()->crablets().end(),[&](const auto& crab){return glm::distance(position,crab.pos)<1e-3;}),"BossFX burst event must use the crablet's actual root position");
        require(crabRenderer.TakeCrabletPopEvents().empty(),"BossFX burst event must be drained exactly once");
        const auto hiddenFrame=render(crabRenderer,*crabMatch.boss(),glm::mat4(crabView),glm::mat4(crabProjection),glm::vec3(crabEye),&hiddenDrawCount);
        require(hiddenDrawCount==hullDrawCount&&visiblePixels(hiddenFrame)<visiblePixels(popFrame),"Popped crablet models stayed visible after the source pop window");
        std::cout<<"BOSS_MATERIAL_GPU source_shaders=6 visible_pixels="<<visible<<" flash_changed_pixels="<<changed<<" slam_mark_pixels="<<visiblePixels(markPixels)<<" ring_pixels="<<visiblePixels(ringPixels)<<" other_move_hazard_pairs=4 crab_draw_calls="<<crabDrawCount<<" pop_pixels="<<visiblePixels(popFrame)<<" renderer="<<glGetString(GL_RENDERER)<<'\n';
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
