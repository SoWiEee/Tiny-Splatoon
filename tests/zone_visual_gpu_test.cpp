#include "ZoneControl.hpp"
#include "ZoneRenderer.hpp"
#include "StageRenderer.hpp"
#include "StageGeometry.hpp"
#include "FacePaint.hpp"
#include "CharacterShader.hpp"
#include "PlayerSimulation.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){
    if(!glfwInit())return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(512,512,"Zone visuals",nullptr,nullptr);if(!window){glfwTerminate();return 1;}
    glfwMakeContextCurrent(window);if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))){glfwDestroyWindow(window);glfwTerminate();return 1;}
    int status=0;
    try{using namespace restoration;
        const std::filesystem::path assets=argc>1?argv[1]:"assets/reference",output=argc>2?argv[2]:"build/captures/zones";
        std::filesystem::create_directories(output);
        const auto found=std::find_if(ReferenceStages().begin(),ReferenceStages().end(),[](const auto& s){return s.id=="tidewater"&&s.mode=="zones";});check(found!=ReferenceStages().end(),"Missing Zones stage");
        const auto& stage=*found;StageGeometry geometry(stage);FacePaint paint(stage,geometry);StageRenderer renderer(assets/"stages");renderer.Load(stage,0,paint);renderer.UploadInk(stage,paint);
        ZoneControl zones(stage,geometry,paint,300,[]{return .5;});ZoneRenderer marks(assets/"zones",std::string(stage.id));
        std::vector<std::unique_ptr<PlayerSimulation>> actors;
        const auto& region=zones.zones().front().region;check(!region.cells.empty(),"Zone has no live paint cells");
        const auto nearest=std::min_element(region.cellCenters.begin(),region.cellCenters.end(),[&](const auto& a,const auto& b){return glm::length(a-zones.zones().front().center)<glm::length(b-zones.zones().front().center);});
        const auto cell=region.cells[nearest-region.cellCenters.begin()];int face=-1;double u=0,v=0;
        for(int id=0;id<int(stage.faces.size());++id){const auto& g=paint.faceGrid(id);if(g.offset>=0&&cell>=g.offset&&cell<g.offset+g.columns*g.rows){face=id;const int local=cell-g.offset;u=(local%g.columns+.5)*g.cellWidth;v=(local/g.columns+.5)*g.cellHeight;break;}}
        check(face>=0,"Zone cell lost its paint face");
        auto pixel=renderer.ReadVisualInk(face,u,v);check(pixel.a<.01f,"New stage must start bare");
        const glm::dvec3 eye{25,27,30};const auto view=glm::lookAt(eye,glm::dvec3(0,1,0),glm::dvec3(0,1,0));const auto projection=glm::perspective(glm::radians(55.),1.,.1,180.);
        CharacterLightState light;light.ambient={.4,.4,.4};light.sunDirection={-.4,.85,.35};light.sunColor={2.5,2.4,2.3};light.skyColor={.65,.75,1};light.groundColor={.2,.15,.1};
        auto draw=[&](const char* name){
            glViewport(0,0,512,512);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glClearColor(.1f,.1f,.1f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            renderer.Draw(view,projection,eye,light);std::vector<unsigned char> before(512*512*4),after(before.size());glReadPixels(0,0,512,512,GL_RGBA,GL_UNSIGNED_BYTE,before.data());
            check(marks.Draw(zones,view,projection,eye,0)==1,"Zone marks must use one source draw");glReadPixels(0,0,512,512,GL_RGBA,GL_UNSIGNED_BYTE,after.data());
            std::size_t changed=0;for(std::size_t i=0;i<before.size();i+=4)if(std::abs(int(before[i])-after[i])+std::abs(int(before[i+1])-after[i+1])+std::abs(int(before[i+2])-after[i+2])>8)++changed;
            check(changed>100,"Floor marks and light curtains must change visible pixels");
            stbi_flip_vertically_on_write(1);check(stbi_write_png((output/name).string().c_str(),512,512,4,after.data(),512*4)!=0,"Could not save zone visual evidence");
            check(glGetError()==GL_NO_ERROR,"Zone drawing must not generate GL errors");
        };
        for(int i=0;i<5;++i){zones.update(.1,actors);marks.Draw(zones,view,projection,eye,0);}
        draw("neutral.png");paint.setCells(region.cells,0);
        for(int i=0;i<9;++i){zones.update(.1,actors);renderer.UploadInk(stage,paint,.1);marks.Draw(zones,view,projection,eye,0);}
        check(zones.owner()==0,"Test capture did not become controlled");pixel=renderer.ReadVisualInk(face,u,v);check(pixel.a>.98f&&pixel.r<.01f&&pixel.g>.7f,"Capture flood must paint continuous fresh team ink in the production atlas");draw("captured.png");
        VisualFlood wipe;wipe.regionId=0;wipe.team=-1;wipe.cells=region.cells;wipe.polygons=region.polygons;wipe.y0=region.y0;wipe.y1=region.y1;wipe.center=zones.zones().front().center;wipe.reach=zones.zones().front().reach;wipe.duration=.7;
        paint.requestVisualFlood(std::move(wipe));for(int i=0;i<8;++i)renderer.UploadInk(stage,paint,.1);
        pixel=renderer.ReadVisualInk(face,u,v);check(pixel.a<.01f,"Objective wipe must erase the production ink atlas");
        paint.clear();renderer.UploadInk(stage,paint,.1);check(renderer.ReadVisualInk(face,u,v).a<.01f,"Restart must clear flood state");check(glGetError()==GL_NO_ERROR,"Flood upload must preserve valid GL state");
        std::cout<<"Source ZoneMarks geometry, shader, flood and wipe passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';status=1;}
    glfwDestroyWindow(window);glfwTerminate();return status;
}
