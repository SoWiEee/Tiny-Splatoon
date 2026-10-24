#include "../src/game/ArsenalData.hpp"
#include "../src/game/CharacterData.hpp"
#include "../src/game/CombatWorld.hpp"
#include "../src/game/PlayerSimulation.hpp"
#include "../src/render/WorldEffects.hpp"
#include "../src/render/WorldFxParticleRenderer.hpp"
#include "../src/render/RenderPipeline.hpp"
#include "../src/render/EnvironmentRenderer.hpp"
#include "../src/render/DeviceRenderer.hpp"
#include "../src/render/TeamPalette.hpp"
#include "../src/world/FacePaint.hpp"
#include "../src/world/StageGeometry.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void charge(restoration::PlayerSimulation& actor) {
    actor.state.invuln = 0.0;
    actor.state.specialCharge = actor.weapon().number("specialCost", 190.0);
}

std::size_t countShape(const std::vector<restoration::WorldFxParticle>& items,
                       restoration::WorldFxShape shape) {
    return static_cast<std::size_t>(std::count_if(items.begin(), items.end(),
        [shape](const auto& item) { return item.shape == shape && item.alpha > 0.0f; }));
}
}

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(128, 128, "World FX source geometry smoke", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    int result = 0;
    try {
        using namespace restoration;
        const StageData* stage = nullptr;
        for (const auto& candidate : ReferenceStages())
            if (candidate.id == "tidewater" && candidate.mode == "turf") stage = &candidate;
        check(stage != nullptr, "Missing source Turf stage for World FX GPU test");

        StageGeometry geometry(*stage);
        FacePaint paint(*stage, geometry);
        CombatWorld world(*stage, geometry, paint);
        const glm::dvec3 anchor = stage->spawnPads[0];

        auto tornadoActor = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 0, &world);
        tornadoActor->spawnAt(anchor, 0.0);
        tornadoActor->setLoadout("shooter", "bomb", "strike");
        charge(*tornadoActor);
        tornadoActor->input.aimYaw = 0.0;
        tornadoActor->input.special = true;
        tornadoActor->tick(1.0 / 60.0);
        check(tornadoActor->specialPose().active && tornadoActor->specialPose().kind == "strike",
              "Strike did not activate through PlayerSimulation input");
        tornadoActor->input.special = false;
        tornadoActor->input.hasMapTarget = true;
        tornadoActor->input.mapTarget = anchor + glm::dvec3(0.0, 0.0, 1.2);
        tornadoActor->input.specialConfirm = true;
        for (int frame = 0; frame < 24 && world.visualObjects().empty(); ++frame)
            tornadoActor->tick(1.0 / 60.0);
        const double strikeFlight = GetEquipment(EquipmentCategory::Special, "strike").number("flight");
        world.tick(strikeFlight + 1.0 / 60.0);
        world.tick(0.1);
        const auto tornadoObjects = world.visualObjects();
        check(std::any_of(tornadoObjects.begin(), tornadoObjects.end(),
                          [](const auto& object) { return object.kind == "tornado"; }),
              "Strike source path did not produce a persistent tornado object");

        auto bubbleActor = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 0, &world);
        bubbleActor->spawnAt(anchor, 0.0);
        bubbleActor->setLoadout("shooter", "bomb", "blower");
        charge(*bubbleActor);
        bubbleActor->input.special = true;
        bubbleActor->tick(1.0 / 60.0);
        check(bubbleActor->specialPose().active && bubbleActor->specialPose().kind == "blower",
              "Blower did not activate through PlayerSimulation input");
        bubbleActor->input.special = false;
        bubbleActor->input.fire = true;
        bubbleActor->tick(1.0 / 60.0);
        const auto bubbleObjects = world.visualObjects();
        check(std::any_of(bubbleObjects.begin(), bubbleObjects.end(),
                          [](const auto& object) { return object.kind == "bubble" && object.held; }),
              "Blower firing did not create its persistent held bubble");

        auto speakerActor = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 0, &world);
        speakerActor->spawnAt(anchor, 0.0);
        speakerActor->setLoadout("shooter", "bomb", "wail");
        charge(*speakerActor);
        speakerActor->input.special = true;
        speakerActor->tick(1.0 / 60.0);
        check(speakerActor->specialPose().active && speakerActor->specialPose().kind == "wail",
              "Wail did not activate through PlayerSimulation input");
        speakerActor->input.special = false;
        for (int frame = 0; frame < 18; ++frame) speakerActor->tick(1.0 / 60.0);
        speakerActor->input.fire = true;
        speakerActor->tick(1.0 / 60.0);
        const auto speakerObjects = world.visualObjects();
        check(std::any_of(speakerObjects.begin(), speakerObjects.end(),
                          [](const auto& object) { return object.kind == "speaker"; }),
              "Wail firing did not place its source speaker prop");

        CharacterData characterData;
        {
            FacePaint popPaint(*stage,geometry);
            CombatWorld popWorld(*stage,geometry,popPaint);
            WorldEffects popEffects(characterData);
            popEffects.BossCrabletPop(anchor,1);
            const auto before=countShape(popEffects.liveItems(),WorldFxShape::BossBlob);
            check(before==10,"A low-quality source crablet pop must create round(26*0.4) ink blobs");
            check(countShape(popEffects.liveItems(),WorldFxShape::Drop)==0,"BossFX blobs must use their own source mesh/material pool, not the fx.js Drop pool");
            const auto sourceInk=glm::vec3(InkColors[1]);
            for(const auto& item:popEffects.liveItems())if(item.shape==WorldFxShape::BossBlob){
                check(glm::distance(item.color,sourceInk)<1e-6f,"Boss pop blobs must use the live team palette");
                const float speed=glm::length(item.vel);
                check(speed>=4.2f*0.55f-1e-5f&&speed<=4.2f*1.15f+1e-5f,"Boss pop blob velocities must follow source speed range");
                check(item.life>=0.84f&&item.life<=1.44f,"Boss pop blob lifetimes must follow source life range");
                check(std::abs(item.pos.y-(anchor.y+0.3))<0.11,"Boss pop blobs must spawn at the source root offset");
                check(std::abs(item.sourceGroundY-float(anchor.y))<1e-5f,"Boss pop blobs must retain their source ground plane");
                const float stretch=1.0f+std::min(2.6f,speed*0.16f);
                check(std::abs(item.renderScale.x-item.radius/std::sqrt(stretch))<1e-6f&&
                      std::abs(item.renderScale.z-item.radius*stretch)<1e-6f,
                      "Airborne BossFX blobs must keep the source velocity-aligned stretch");
            }
            {
                std::ifstream defaults("assets/reference/boss/shaders/crablet-pop-blob.uniforms");
                check(static_cast<bool>(defaults),"Missing exported source BossFX physical-material fixture");
                std::map<std::string,std::vector<double>> values;std::string name;std::size_t count=0;
                while(defaults>>name>>count){std::vector<double> v(count);for(auto& component:v)defaults>>component;values.emplace(name,std::move(v));}
                const auto scalar=[&](const char* key){const auto it=values.find(key);check(it!=values.end()&&it->second.size()==1,"Missing BossFX material fixture value");return it->second[0];};
                check(std::abs(scalar("roughness")-0.12)<1e-8&&std::abs(scalar("metalness"))<1e-8&&
                      std::abs(scalar("clearcoat")-1.0)<1e-8&&std::abs(scalar("clearcoatRoughness")-0.04)<1e-8,
                      "Exported BossFX material must preserve source roughness, metalness, and clearcoat");
            }
            {
                std::ifstream mesh("assets/reference/boss/shaders/crablet-pop-blob.mesh",std::ios::binary);
                char magic[8]{};std::array<unsigned char,8> sizes{};mesh.read(magic,8);mesh.read(reinterpret_cast<char*>(sizes.data()),8);
                const auto readU32=[&](std::size_t offset){return std::uint32_t(sizes[offset])|(std::uint32_t(sizes[offset+1])<<8)|(std::uint32_t(sizes[offset+2])<<16)|(std::uint32_t(sizes[offset+3])<<24);};
                check(mesh&&std::string(magic,8)=="TSBOSSFX"&&readU32(0)==60&&readU32(4)==60,
                      "BossFX low-quality source IcosahedronGeometry must retain its 20 triangular faces");
                std::array<char,60*32+60*4> payload{};mesh.read(payload.data(),static_cast<std::streamsize>(payload.size()));
                check(static_cast<bool>(mesh),"Truncated exported source BossFX mesh fixture");
                const auto readFloat=[&](std::size_t offset){std::uint32_t bits=0;for(int b=0;b<4;++b)bits|=std::uint32_t(static_cast<unsigned char>(payload[offset+b]))<<(8*b);float value=0;std::memcpy(&value,&bits,sizeof(value));return value;};
                std::map<std::array<int,3>,bool> uniqueVertices;
                std::array<glm::vec3,60> positions{},normals{};
                for(std::size_t vertex=0;vertex<60;++vertex){
                    const auto offset=vertex*32;const glm::vec3 p(readFloat(offset),readFloat(offset+4),readFloat(offset+8));
                    check(std::abs(glm::length(p)-1.0f)<1e-5f,"BossFX source icosahedron vertices must lie on its unit sphere");
                    const glm::vec3 normal(readFloat(offset+12),readFloat(offset+16),readFloat(offset+20));
                    const glm::vec2 uv(readFloat(offset+24),readFloat(offset+28));
                    positions[vertex]=p;normals[vertex]=normal;
                    check(std::abs(glm::length(normal)-1.0f)<1e-5f,
                          "BossFX source icosahedron must retain unit source face normals");
                    check(std::isfinite(uv.x)&&std::isfinite(uv.y)&&uv.x>=0&&uv.x<=1.1f&&uv.y>=0&&uv.y<=1,
                          "BossFX source icosahedron must retain finite source UV coordinates");
                    uniqueVertices[{int(std::lround(p.x*10000)),int(std::lround(p.y*10000)),int(std::lround(p.z*10000))}]=true;
                }
                check(uniqueVertices.size()==12,"BossFX detail-zero icosahedron must have the source 12 unique vertices");
                for(std::size_t triangle=0;triangle<60;triangle+=3){
                    const auto& normal=normals[triangle];
                    const glm::vec3 winding=glm::normalize(glm::cross(positions[triangle+1]-positions[triangle],positions[triangle+2]-positions[triangle]));
                    check(glm::distance(normal,normals[triangle+1])<1e-5f&&glm::distance(normal,normals[triangle+2])<1e-5f&&glm::dot(normal,winding)>0.999f,
                          "BossFX source icosahedron must preserve flat per-face normals and outward triangle winding");
                }
                for(std::size_t index=0;index<60;++index){
                    const auto offset=60*32+index*4;
                    const std::uint32_t value=std::uint32_t(static_cast<unsigned char>(payload[offset]))|
                        (std::uint32_t(static_cast<unsigned char>(payload[offset+1]))<<8)|
                        (std::uint32_t(static_cast<unsigned char>(payload[offset+2]))<<16)|
                        (std::uint32_t(static_cast<unsigned char>(payload[offset+3]))<<24);
                    check(value==index,"Non-indexed source BossFX icosahedron must retain source triangle order");
                }
            }
            const auto beforePositions=popEffects.liveItems();
            const glm::dvec3 popCamera=anchor+glm::dvec3(0,1.5,3.0);
            popEffects.Update(1.0/60.0,popWorld,popCamera);
            const auto updatedPopItems=popEffects.liveItems();
            check(countShape(updatedPopItems,WorldFxShape::BossBlob)==10,"The source-mesh pop burst must remain live after its first simulation frame");
            check(std::any_of(updatedPopItems.begin(),updatedPopItems.end(),[&](const auto& item){
                return item.shape==WorldFxShape::BossBlob&&glm::distance(item.pos,beforePositions.front().pos)>1e-5f;
            }),"Boss pop blobs must advance through the source physical pool");
            check(std::all_of(updatedPopItems.begin(),updatedPopItems.end(),[](const auto& item){
                return item.shape!=WorldFxShape::BossBlob||item.renderScale.z>item.renderScale.x;
            }),"Airborne source BossFX blobs must stretch along their velocity axis");
            const glm::vec3 popEye(popCamera),popTarget(anchor+glm::dvec3(0,0.3,0));
            const auto popView=glm::lookAt(popEye,popTarget,glm::vec3(0,1,0));
            const auto popProjection=glm::perspective(glm::radians(48.f),1.f,.1f,20.f);
            EnvironmentRenderer environment;environment.Load(*stage);const auto popLighting=environment.Lights();
            RenderPipeline pipeline;pipeline.Configure(0,false,false,false);
            pipeline.Begin(128,128,popProjection,glm::inverse(popView),.1f,20.f);
            GLboolean colorMaskBeforeClear[4]{},depthMaskBeforeClear=GL_FALSE;
            glGetBooleanv(GL_COLOR_WRITEMASK,colorMaskBeforeClear);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMaskBeforeClear);
            glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);
            glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            GLint sceneFramebufferBefore=0,sceneFramebufferAfter=0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&sceneFramebufferBefore);
            const int popDraws=popEffects.Draw(popView,popProjection,popEye,&popLighting);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&sceneFramebufferAfter);
            check(popDraws>0,"Boss pop blobs must draw through the native WorldFX pool");
            std::vector<float> scenePixels(128*128*4),sceneDepth(128*128);
            glReadPixels(0,0,128,128,GL_RGBA,GL_FLOAT,scenePixels.data());
            glReadPixels(0,0,128,128,GL_DEPTH_COMPONENT,GL_FLOAT,sceneDepth.data());
            float sceneMaxRgb=0,sceneMinAlpha=1,sceneMaxAlpha=0;int sceneLit=0,sceneNonFiniteRgb=0,sceneNonFiniteAtDepth=0;
            const int sceneDepthWritten=static_cast<int>(std::count_if(sceneDepth.begin(),sceneDepth.end(),[](float d){return d<.99999f;}));
            for(std::size_t i=0;i<scenePixels.size();i+=4){
                const bool finiteRgb=std::isfinite(scenePixels[i])&&std::isfinite(scenePixels[i+1])&&std::isfinite(scenePixels[i+2]);
                if(!finiteRgb){++sceneNonFiniteRgb;if(sceneDepth[i/4]<.99999f)++sceneNonFiniteAtDepth;}
                else sceneMaxRgb=std::max(sceneMaxRgb,std::max(scenePixels[i],std::max(scenePixels[i+1],scenePixels[i+2])));
                sceneMinAlpha=std::min(sceneMinAlpha,scenePixels[i+3]);sceneMaxAlpha=std::max(sceneMaxAlpha,scenePixels[i+3]);
                if(finiteRgb&&(scenePixels[i]>1e-4f||scenePixels[i+1]>1e-4f||scenePixels[i+2]>1e-4f))++sceneLit;
            }
            ScreenEffects noScreenEffects;noScreenEffects.enabled=false;pipeline.End(GradeSettings{},noScreenEffects,128,128);
            std::vector<unsigned char> popPixels(128*128*4);glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,popPixels.data());
            int popVisible=0;
            for(std::size_t i=0;i<popPixels.size();i+=4)
                if(popPixels[i]>20||popPixels[i+1]>20||popPixels[i+2]>20)++popVisible;
            const GLenum popGlError=glGetError();
            check(popVisible>5&&popGlError==GL_NO_ERROR&&sceneNonFiniteAtDepth==0,
                  "Boss crablet pop must produce visible native GPU pixels (visible="+std::to_string(popVisible)+", GL error="+std::to_string(popGlError)+", sceneFbo="+std::to_string(sceneFramebufferBefore)+"->"+std::to_string(sceneFramebufferAfter)+", sceneLit="+std::to_string(sceneLit)+", sceneMaxRgb="+std::to_string(sceneMaxRgb)+", nonFiniteRgb="+std::to_string(sceneNonFiniteRgb)+", nonFiniteAtDepth="+std::to_string(sceneNonFiniteAtDepth)+", sceneAlpha="+std::to_string(sceneMinAlpha)+".."+std::to_string(sceneMaxAlpha)+", depthPixels="+std::to_string(sceneDepthWritten)+", masksBeforeClear="+std::to_string(colorMaskBeforeClear[0])+std::to_string(colorMaskBeforeClear[1])+std::to_string(colorMaskBeforeClear[2])+std::to_string(colorMaskBeforeClear[3])+"/"+std::to_string(depthMaskBeforeClear)+")");
        }
        {
            WorldFxParticleRenderer pools;
            const glm::vec3 eye(0,3,5);const auto view=glm::lookAt(eye,glm::vec3(0),glm::vec3(0,1,0));
            const auto projection=glm::perspective(glm::radians(48.f),1.f,.1f,30.f);
            std::filesystem::create_directories("build/captures/worldfx");
            const GLenum prePoolGlError=glGetError();
            for(int i=0;i<6;++i){
                WorldFxParticle item;item.shape=static_cast<WorldFxShape>(i);item.radius=item.renderRadius=.65f;item.radius1=1;
                item.life=1;item.age=.2f;item.renderLength=i==5?2.f:1.2f;item.alpha=item.renderAlpha=1;
                item.color={1,.25415f,.007f};item.normal={0,1,0};item.vel={1,2,0};item.crown=1;item.seed=.37f;
                glBindFramebuffer(GL_FRAMEBUFFER,0);glReadBuffer(GL_BACK);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glViewport(0,0,128,128);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);glPolygonOffset(2,3);
                const int poolDraws=pools.Draw({item},{},view,projection,eye,nullptr,.4);const GLenum drawGlError=glGetError();
                check(poolDraws==1,"Every source particle pool must submit its actual shader and attributes");
                GLint equation=0;glGetIntegerv(GL_BLEND_EQUATION_RGB,&equation);check(equation==GL_FUNC_REVERSE_SUBTRACT&&glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE),"Source particle draws must restore caller GL state");
                std::vector<unsigned char> pixels(128*128*4);glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                int visible=0,maxChannel=0;for(std::size_t p=0;p<pixels.size();p+=4){if(pixels[p]>20||pixels[p+1]>20||pixels[p+2]>20)++visible;maxChannel=std::max(maxChannel,std::max(int(pixels[p]),std::max(int(pixels[p+1]),int(pixels[p+2]))));}
                GLint drawFbo=0,readFbo=0,readBuffer=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);glGetIntegerv(GL_READ_BUFFER,&readBuffer);const GLenum poolGlError=glGetError();
                check(visible>5&&poolGlError==GL_NO_ERROR&&drawGlError==GL_NO_ERROR&&prePoolGlError==GL_NO_ERROR,"A source particle pool must render visible native pixels without GL errors: shape="+std::to_string(i)+", visible="+std::to_string(visible)+", max="+std::to_string(maxChannel)+", GL error="+std::to_string(poolGlError)+", draw error="+std::to_string(drawGlError)+", pre error="+std::to_string(prePoolGlError)+", fbo="+std::to_string(drawFbo)+"/"+std::to_string(readFbo)+", readBuffer="+std::to_string(readBuffer));
                stbi_flip_vertically_on_write(1);stbi_write_png(("build/captures/worldfx/pool-"+std::to_string(i)+".png").c_str(),128,128,4,pixels.data(),128*4);
            }
            glBlendEquation(GL_FUNC_ADD);glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);glPolygonOffset(0,0);
            CharacterPose steady(characterData,characterData.rig(0,0,0),"shaker-grip","shooter"),shaking(characterData,characterData.rig(0,0,0),"shaker-grip","shooter");
            steady.setSub("shaker");shaking.setSub("shaker");CharacterAnimState input;input.subAim=true;CharacterSceneInput scene;
            for(int frame=0;frame<60;++frame){steady.update(1./60,input,scene);shaking.update(1./60,input,scene);}
            input.shakerCharging=true;input.shakerCharge=1;
            const auto shaken=shaking.update(1./60,input,scene);input.shakerCharging=false;const auto held=steady.update(1./60,input,scene);
            check(shaken.shakerKick>0&&held.shakerKick==0,"Shaker lamps must share the charging grip kick on a level transition");
            const glm::dvec4 grip(0,.1915,0,1);
            check(glm::distance(glm::dvec3(shaken.subPropWorld*grip),glm::dvec3(held.subPropWorld*grip))<1e-5,"A charging Shaker must rattle around the held grip without detaching from the hand");
            check(glm::distance(shaken.subNozzle,held.subNozzle)>1e-5,"Shaker vent FX must follow the animated can's nozzle");
        }
        {
            FacePaint bombPaint(*stage,geometry);CombatWorld bombWorld(*stage,geometry,bombPaint);
            PlayerSimulation bomber(*stage,geometry,bombPaint,0,&bombWorld);bomber.spawnAt(anchor,0);
            bombWorld.deploy(bomber,"bomb");
            auto& bomb=const_cast<std::vector<CombatDevice>&>(bombWorld.devices()).back();
            bomb.pos=anchor+glm::dvec3(0,.2,0);bomb.vel={0,0,0};
            const double fuse=GetEquipment(EquipmentCategory::Sub,"bomb").number("fuse");bomb.fuse=fuse*.5;
            DeviceRenderer sourceModels;WorldEffects danger(characterData);danger.SetDeviceRenderer(&sourceModels);
            danger.Update(1./60,bombWorld,anchor+glm::dvec3(0,12,0));
            auto ring=std::find_if(danger.worldItems().begin(),danger.worldItems().end(),[](const auto& p){return p.shape==WorldFxShape::Ring&&p.style==3;});
            check(ring!=danger.worldItems().end(),"Source device models must not suppress the armed bomb danger ring");
            check(std::abs(ring->age-.5)<1e-6&&std::abs(ring->radius-GetEquipment(EquipmentCategory::Sub,"bomb").number("radius"))<1e-6,"Bomb warning must use source fuse progress and actual blast radius");
            glViewport(0,0,128,128);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            danger.Draw(glm::lookAt(glm::vec3(anchor)+glm::vec3(0,12,0),glm::vec3(anchor),glm::vec3(0,0,-1)),glm::ortho(-5.f,5.f,-5.f,5.f,.1f,30.f),glm::vec3(anchor)+glm::vec3(0,12,0));
            std::vector<unsigned char> pixels(128*128*4);glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            int visible=0;for(std::size_t i=0;i<pixels.size();i+=4)if(pixels[i]>20||pixels[i+1]>20||pixels[i+2]>20)++visible;
            check(visible>100,"The fuse danger region must produce visible native GPU pixels");
            bomb.live=false;danger.Update(1./60,bombWorld,anchor);
            check(danger.worldItems().empty(),"An exploded or removed bomb must not leave a stale danger region");
        }
        {
            FacePaint walkPaint(*stage,geometry);
            CombatWorld walkWorld(*stage,geometry,walkPaint);
            PlayerSimulation walker(*stage,geometry,walkPaint,0,&walkWorld);
            walker.spawnAt(anchor,0);
            walker.state.grounded=true;walker.state.squid=false;walker.state.submerged=false;
            walker.state.groundTeam=1;walker.state.vel={5,0,0};
            WorldEffects steps(characterData);steps.SetPaintSurface(&walkPaint);
            steps.Update(1.0/60.0,walkWorld,anchor+glm::dvec3(0,4,7));
            walker.state.pos.x+=0.8;
            steps.Update(1.0/60.0,walkWorld,anchor+glm::dvec3(0,4,7));
            check(steps.counts().shells==0,"Wet footsteps must not emit a generic explosion shell on the floor");
            check(!walkPaint.takeVisualRipples().empty(),"Wet foot plants must ripple the ink surface");
            const auto gridBefore=walkPaint.grid();const auto versionBefore=walkPaint.version();
            for(int i=0;i<90;++i)steps.Update(1.0/60.0,walkWorld,anchor+glm::dvec3(0,4,7));
            const auto specks=walkPaint.takeVisualSplats();
            check(!specks.empty(),"Landing walking droplets must enter the cosmetic paint atlas path");
            check(std::all_of(specks.begin(),specks.end(),[](const auto& item){return item.cosmetic&&item.kind==SplatKind::Speck;}),"Walking droplets must remain cosmetic specks");
            check(walkPaint.grid()==gridBefore&&walkPaint.version()==versionBefore,"Walking spray must not claim scoring ink cells");
            check(steps.counts().rings==0,"Landed droplets must not leave circular fallback decals when paint is connected");
            walker.state.groundTeam=2;steps.Footstep(walker,walker.state.pos,5);
            walker.state.groundTeam=0;
            const glm::vec3 closeEye=glm::vec3(walker.state.pos)+glm::vec3(0,0.7f,1.5f);
            steps.Update(1.0/60.0,walkWorld,closeEye);
            check(steps.counts().glows>0,"Enemy-ink footsteps must exercise the source bubble-glow draw path");
            const auto closeView=glm::lookAt(closeEye,glm::vec3(walker.state.pos)+glm::vec3(0,0.05f,0),glm::vec3(0,1,0));
            const auto closeProjection=glm::perspective(glm::radians(45.f),1.f,0.01f,20.f);
            glViewport(0,0,128,128);glEnable(GL_DEPTH_TEST);glClearColor(.2f,.24f,.3f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            steps.Draw(closeView,closeProjection,closeEye);
            std::vector<unsigned char> glowPixels(128*128*4);glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,glowPixels.data());
            std::filesystem::create_directories("build/captures/worldfx");
            stbi_flip_vertically_on_write(1);
            check(stbi_write_png("build/captures/worldfx/footstep-glow.png",128,128,4,glowPixels.data(),128*4)!=0,"Could not save footstep glow evidence");
            std::vector<glm::vec2> dropCenters;
            for(const auto& particle:steps.liveItems())if(particle.shape==WorldFxShape::Drop){const auto clip=closeProjection*closeView*glm::vec4(particle.pos,1);if(clip.w>0)dropCenters.push_back((glm::vec2(clip)/clip.w*.5f+.5f)*128.f);}
            int litPixels=0;
            for(int y=0;y<128;++y)for(int x=0;x<128;++x){
                const bool drop=std::any_of(dropCenters.begin(),dropCenters.end(),[&](auto c){return std::abs(c.x-(x+.5f))<7&&std::abs(c.y-(y+.5f))<7;});
                if(drop)continue;
                const auto offset=(y*128+x)*4;
                check(glowPixels[offset+2]>=75,"Bubble glow quads must add light without dark rectangular backgrounds");
                if(glowPixels[offset]>54||glowPixels[offset+1]>64||glowPixels[offset+2]>80)++litPixels;
            }
            check(litPixels>0,"Source bubble glows must remain visible outside the tiny droplet silhouettes");
        }
        WorldEffects effects(characterData);
        const glm::dvec3 camera(anchor.x, anchor.y + 5.2, anchor.z - 11.5);
        effects.Update(1.0 / 60.0, world, camera);
        const auto& items = effects.worldItems();
        check(countShape(items, WorldFxShape::Swirl) >= 2,
              "Persistent tornado did not emit its two source funnel shapes");
        check(countShape(items, WorldFxShape::Speaker) >= 1,
              "Persistent wail did not emit the source speaker shape");
        check(countShape(items, WorldFxShape::SourceBeam) >= 1,
              "Persistent wail did not emit the source speaker beam shape");
        check(countShape(items, WorldFxShape::Bubble) >= 1,
              "Persistent blower did not emit the source bubble shape");

        glViewport(0, 0, 128, 128);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.025f, 0.035f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const glm::vec3 eye(camera);
        const glm::vec3 target(anchor.x, anchor.y + 1.3, anchor.z + 1.0);
        const glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
        const glm::mat4 projection = glm::perspective(glm::radians(58.0f), 1.0f, 0.1f, 80.0f);
        glBlendFuncSeparate(GL_ONE,GL_ZERO,GL_ZERO,GL_ONE);
        glBlendEquationSeparate(GL_FUNC_ADD,GL_MAX);
        const int drawCalls = effects.Draw(view, projection, eye);
        GLint restoredAlphaSrc=0,restoredAlphaDst=0,restoredAlphaEquation=0;
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&restoredAlphaSrc);glGetIntegerv(GL_BLEND_DST_ALPHA,&restoredAlphaDst);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&restoredAlphaEquation);
        check(restoredAlphaSrc==GL_ZERO&&restoredAlphaDst==GL_ONE&&restoredAlphaEquation==GL_MAX,"FX normal/additive passes must restore the caller's independent alpha blend state");
        check(drawCalls > 0, "WorldEffects submitted no real source mesh draws");
        std::vector<unsigned char> pixels(128 * 128 * 4);
        glReadPixels(0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        std::size_t painted = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 4)
            if (std::abs(int(pixels[i]) - 6) > 2 || std::abs(int(pixels[i + 1]) - 9) > 2 ||
                std::abs(int(pixels[i + 2]) - 15) > 2) ++painted;
        check(painted > 8, "Source world meshes did not change visible framebuffer pixels");
        const GLenum error = glGetError();
        check(error == GL_NO_ERROR, "WorldEffects source shader/mesh draw returned GL error "
              + std::to_string(static_cast<unsigned int>(error)));

        std::cout << "WORLD_FX_GPU sourceShapes=tornado,speaker,beam,bubble draws=" << drawCalls
                  << " renderer=" << reinterpret_cast<const char*>(glGetString(GL_RENDERER)) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
