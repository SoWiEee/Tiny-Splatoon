#include "StageViewer.hpp"
#include "ReferenceData.hpp"
#include "StageGeometry.hpp"
#include "FacePaint.hpp"
#include "PlayerSimulation.hpp"
#include "MatchSimulation.hpp"
#include "NavGraph.hpp"
#include "CharacterArt.hpp"
#include "ArsenalData.hpp"
#include "PerformanceCapture.hpp"
#include "FollowCamera.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace restoration {
namespace {
struct Options {
    std::string stage = "tidewater", mode = "turf", capture;
    std::string benchmarkReport;
    int frames = 0, width = 1600, height = 900;
    int benchmarkWarmup = 120, benchmarkFrames = 600;
    std::uint32_t benchmarkSeed = 1;
    bool paintProbe = false, game = false, playProbe = false, match = false, native = false;
    bool paintProbeSpecified = false, playProbeSpecified = false, captureSpecified = false, benchmarkReportSpecified = false;
    bool benchmarkTuningSpecified = false;
    bool widthSpecified = false, heightSpecified = false;
};

Options ParseOptions(int argc, char** argv) {
    Options result;
    bool stageTool = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--reference-match") {
            result.match = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') result.stage = argv[++i];
            continue;
        }
        if (arg == "--native") { result.native = true; continue; }
        if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
        const std::string value = argv[++i];
        if (arg == "--reference-stage") { result.stage = value; stageTool = true; }
        else if (arg == "--reference-game") { result.stage = value; result.game = true; }
        else if (arg == "--reference-mode") result.mode = value;
        else if (arg == "--capture") { result.capture = value; result.captureSpecified = true; }
        else if (arg == "--benchmark-report") { result.benchmarkReport = value; result.benchmarkReportSpecified = true; }
        else if (arg == "--benchmark-seed") {
            result.benchmarkTuningSpecified = true;
            std::size_t end = 0;
            const auto seed = std::stoull(value, &end);
            if (end != value.size() || seed > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("Invalid unsigned seed for " + arg);
            result.benchmarkSeed = static_cast<std::uint32_t>(seed);
        }
        else if (arg == "--benchmark-warmup" || arg == "--benchmark-frames") {
            result.benchmarkTuningSpecified = true;
            std::size_t end = 0;
            const int number = std::stoi(value, &end);
            if (end != value.size() || number < (arg == "--benchmark-warmup" ? 0 : 1) || number > 100000)
                throw std::runtime_error("Invalid frame count for " + arg);
            if (arg == "--benchmark-warmup") result.benchmarkWarmup = number;
            else result.benchmarkFrames = number;
        }
        else if (arg == "--paint-probe" || arg == "--play-probe") {
            if(value!="0" && value!="1") throw std::runtime_error(arg+" must be 0 or 1");
            if(arg=="--paint-probe") { result.paintProbe=value=="1"; result.paintProbeSpecified=true; }
            else { result.playProbe=value=="1"; result.playProbeSpecified=true; }
        }
        else if (arg == "--frames" || arg == "--width" || arg == "--height") {
            std::size_t end = 0;
            const int number = std::stoi(value, &end);
            if (end != value.size() || number <= 0 || number > 16384)
                throw std::runtime_error("Invalid positive integer for " + arg);
            if (arg == "--frames") result.frames = number;
            else if (arg == "--width") { result.width = number; result.widthSpecified = true; }
            else { result.height = number; result.heightSpecified = true; }
        } else throw std::runtime_error("Unknown stage lab option: " + arg);
    }
    if (!stageTool && !result.game && !result.match) result.native = true;
    if (result.match) {
        if (!result.widthSpecified) result.width = 1920;
        if (!result.heightSpecified) result.height = 1080;
    }
    if (!result.capture.empty() && !result.frames) result.frames = 3;
    if(result.playProbe && !result.game) throw std::runtime_error("--play-probe requires --reference-game");
    if (result.native && (result.match || result.game || result.paintProbeSpecified || result.playProbeSpecified ||
                          result.captureSpecified || result.benchmarkReportSpecified || result.benchmarkTuningSpecified))
        throw std::runtime_error("--native cannot be combined with reference matches, practice probes, captures, or benchmark options");
    if (!result.benchmarkReport.empty() && !result.match)
        throw std::runtime_error("--benchmark-report requires --reference-match");
    if (result.match && (!result.capture.empty() || result.game || result.paintProbe || result.playProbe))
        throw std::runtime_error("--reference-match cannot be combined with practice probes or --capture");
    if (!result.benchmarkReport.empty() && result.frames)
        throw std::runtime_error("--frames cannot shorten a fixed-size benchmark capture");
    return result;
}

GLuint Compile(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Stage shader: ") + log);
    }
    return shader;
}

struct RenderVertex { glm::vec3 position, normal, color; glm::vec2 inkUV; };

class StageRenderer {
    GLuint vao = 0, vbo = 0, program = 0, ink = 0;
    GLsizei vertices = 0;
    const StageData* uploadedStage = nullptr;
    std::vector<std::uint8_t> uploadedGrid;
public:
    StageRenderer() {
        const char* vs = R"GLSL(#version 450 core
layout(location=0) in vec3 p;
layout(location=1) in vec3 n;
layout(location=2) in vec3 c;
layout(location=3) in vec2 inkCoord;
uniform mat4 vp;
out vec3 normal;
out vec3 color;
out vec3 world;
out vec2 inkUV;
void main(){normal=n;color=c;world=p;inkUV=inkCoord;gl_Position=vp*vec4(p,1);}
)GLSL";
        const char* fs = R"GLSL(#version 450 core
in vec3 normal;
in vec3 color;
in vec3 world;
in vec2 inkUV;
out vec4 outColor;
uniform vec3 eye;
uniform usampler2D ownership;
void main(){
    vec3 n=normalize(normal);
    vec3 l=normalize(vec3(-0.4,0.85,0.35));
    vec3 base=color;
    if(inkUV.x>=0.0){
        uint owner=texture(ownership,inkUV).r;
        if(owner==1u)base=pow(vec3(1.0,0.5412,0.0784),vec3(2.2));
        if(owner==2u)base=pow(vec3(0.1843,0.3569,1.0),vec3(2.2));
    }
    vec3 linear=base*(0.42+0.58*max(dot(n,l),0.0));
    float fog=1-exp(-length(world-eye)*0.0015);
    linear=mix(linear,vec3(0.60,0.77,0.84),fog);
    vec3 srgb=mix(linear*12.92,1.055*pow(max(linear,vec3(0)),vec3(1.0/2.4))-0.055,step(vec3(0.0031308),linear));
    outColor=vec4(srgb,1);
}
)GLSL";
        const GLuint vertex = Compile(GL_VERTEX_SHADER, vs);
        GLuint fragment = 0;
        try { fragment = Compile(GL_FRAGMENT_SHADER, fs); }
        catch (...) { glDeleteShader(vertex); throw; }
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[4096]{};
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            glDeleteProgram(program);
            program = 0;
            throw std::runtime_error(std::string("Stage shader link: ") + log);
        }
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        for (GLuint attr = 0; attr < 3; ++attr) {
            glEnableVertexAttribArray(attr);
            glVertexAttribPointer(attr, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex),
                reinterpret_cast<void*>(static_cast<std::size_t>(attr) * sizeof(glm::vec3)));
        }
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,sizeof(RenderVertex),reinterpret_cast<void*>(offsetof(RenderVertex,inkUV)));
        glBindVertexArray(0);
        glGenTextures(1,&ink);
        glBindTexture(GL_TEXTURE_2D,ink);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_R8UI,4096,4096,0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,nullptr);
    }
    ~StageRenderer() {
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(program);
        glDeleteTextures(1,&ink);
    }
    void Load(const StageData& stage, int display, const FacePaint& paint) {
        std::vector<RenderVertex> data;
        data.reserve(stage.faces.size() * 6);
        if(display==3) {
            for(const auto& b:stage.blocks) {
                if(!b.solid) continue;
                const glm::vec3 color=b.rail?glm::vec3(0.55f,0.1f,0.8f):
                    b.hidden?glm::vec3(0.8f,0.18f,0.08f):glm::vec3(0.25f,0.45f,0.5f);
                for(int axis=0;axis<3;++axis) for(double sign:{-1.0,1.0}) {
                    const auto normal=b.axes[axis]*sign;
                    const auto u=b.axes[(axis+1)%3]*b.half[(axis+1)%3];
                    const auto v=b.axes[(axis+2)%3]*b.half[(axis+2)%3];
                    const auto middle=b.center+normal*b.half[axis];
                    const auto a=middle-u-v, c=middle+u+v;
                    for(const auto& p:{a,middle+u-v,c,a,c,middle-u+v})
                        data.push_back({glm::vec3(p),glm::vec3(normal),color,glm::vec2(-1)});
                }
            }
        }
        for (int id=0;id<static_cast<int>(stage.faces.size());++id) {
            if(display==3) break;
            const auto& f=stage.faces[id];
            glm::vec3 color(f.color);
            if (display == 1) color = f.paintable ? glm::vec3(0.05f,0.65f,0.4f) : glm::vec3(0.45f,0.13f,0.14f);
            if (display == 2) color = f.paintable && f.turf ? glm::vec3(0.05f,0.5f,0.95f) : glm::vec3(0.23f);
            const glm::dvec3 a = f.origin, b = a + f.u * f.width;
            const glm::dvec3 d = a + f.v * f.height, c = b + f.v * f.height;
            const auto& rect=paint.atlas(id);
            for (const auto& p : {a,b,c,a,c,d}) {
                glm::vec2 uv(-1.0f);
                if(f.paintable) {
                    const auto local=p-f.origin;
                    uv=glm::vec2(glm::dvec2(rect.x+rect.pad+glm::dot(local,f.u)*rect.ppm,
                                           rect.y+rect.pad+glm::dot(local,f.v)*rect.ppm)/4096.0);
                }
                data.push_back({glm::vec3(p),glm::vec3(f.normal),color,uv});
            }
        }
        vertices = static_cast<GLsizei>(data.size());
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(RenderVertex), data.data(), GL_STATIC_DRAW);
    }
    void UploadInk(const StageData& stage,const FacePaint& paint) {
        // Diagnostic ownership cells, not the final animated GPU ink material.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,ink);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        const auto& grid=paint.grid();
        if(uploadedStage!=&stage || uploadedGrid.size()!=grid.size()) {
            const unsigned char zero=0;
            glClearTexImage(ink,0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,&zero);
            uploadedGrid.assign(grid.size(),0);uploadedStage=&stage;
        }
        for(int id=0;id<static_cast<int>(stage.faces.size());++id) {
            const auto& f=stage.faces[id];
            if(!f.paintable) continue;
            const auto& r=paint.atlas(id);
            const auto& g=paint.faceGrid(id);
            if(g.offset<0) continue;
            int left=g.columns, right=-1, bottom=g.rows, top=-1;
            for(int v=0;v<g.rows;++v) for(int u=0;u<g.columns;++u) {
                const auto cell=g.offset+v*g.columns+u;
                if(grid[cell]==uploadedGrid[cell]) continue;
                left=std::min(left,u);right=std::max(right,u);bottom=std::min(bottom,v);top=std::max(top,v);
            }
            if(right<0) continue;
            // Edge cells also own the atlas gutter; update just the changed cell rectangle.
            const int x0=left==0?r.x:std::max(r.x,static_cast<int>(std::floor(r.x+r.pad+left*g.cellWidth*r.ppm))-1);
            const int y0=bottom==0?r.y:std::max(r.y,static_cast<int>(std::floor(r.y+r.pad+bottom*g.cellHeight*r.ppm))-1);
            const int x1=right==g.columns-1?r.x+r.width:std::min(r.x+r.width,static_cast<int>(std::ceil(r.x+r.pad+(right+1)*g.cellWidth*r.ppm))+1);
            const int y1=top==g.rows-1?r.y+r.height:std::min(r.y+r.height,static_cast<int>(std::ceil(r.y+r.pad+(top+1)*g.cellHeight*r.ppm))+1);
            std::vector<unsigned char> pixels(static_cast<std::size_t>(x1-x0)*(y1-y0));
            for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) {
                const double u=(x+0.5-r.x-r.pad)/r.ppm;
                const double v=(y+0.5-r.y-r.pad)/r.ppm;
                pixels[static_cast<std::size_t>(y-y0)*(x1-x0)+x-x0]=static_cast<unsigned char>(paint.sample(id,u,v));
            }
            glTexSubImage2D(GL_TEXTURE_2D,0,x0,y0,x1-x0,y1-y0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,pixels.data());
        }
        uploadedGrid=grid;
    }
    void VerifyInk(const StageData& stage,const FacePaint& paint) const {
        std::vector<unsigned char> pixels(4096*4096);
        glBindTexture(GL_TEXTURE_2D,ink);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,pixels.data());
        for(int id=0;id<static_cast<int>(stage.faces.size());++id) {
            if(!stage.faces[id].paintable) continue;
            const auto& r=paint.atlas(id);
            for(int y=r.y;y<r.y+r.height;++y) for(int x=r.x;x<r.x+r.width;++x)
                if(pixels[static_cast<std::size_t>(y)*4096+x]!=paint.sample(id,(x+0.5-r.x-r.pad)/r.ppm,(y+0.5-r.y-r.pad)/r.ppm))
                    throw std::runtime_error("GPU ownership upload differs from CPU grid");
        }
        std::cout<<"INK_UPLOAD verified\n";
    }
    int Draw(const glm::mat4& vp, const glm::vec3& eye, bool wireframe) {
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,ink);
        glUniform1i(glGetUniformLocation(program,"ownership"),0);
        glUniformMatrix4fv(glGetUniformLocation(program, "vp"), 1, GL_FALSE, &vp[0][0]);
        glUniform3fv(glGetUniformLocation(program,"eye"),1,&eye[0]);
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, vertices);
        glBindVertexArray(0);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        return vertices > 0 ? 1 : 0;
    }
    void LoadActors(const PlayerSimulation& player) {
        std::vector<RenderVertex> data;
        auto capsule=[&](glm::dvec3 feet,double radius,double height,glm::vec3 color) {
            // shortcut: collision silhouettes until the reference skinned character renderer is ported.
            auto point=[&](int ring,int segment) {
                const double lat=-glm::pi<double>()/2+glm::pi<double>()*ring/8;
                const double lon=glm::two_pi<double>()*segment/12;
                const glm::dvec3 n(std::cos(lat)*std::sin(lon),std::sin(lat),std::cos(lat)*std::cos(lon));
                const double mid=lat<0?radius:std::max(radius,height-radius);
                return RenderVertex{glm::vec3(feet+glm::dvec3(0,mid,0)+n*radius),glm::vec3(n),color,glm::vec2(-1)};
            };
            for(int r=0;r<8;++r) for(int s=0;s<12;++s) {
                const auto a=point(r,s), b=point(r,s+1), c=point(r+1,s+1), d=point(r+1,s);
                data.insert(data.end(),{a,b,c,a,c,d});
            }
        };
        if(player.state.alive) capsule(player.visualPos(),0.38,player.state.squid?0.55:1.45,glm::vec3(1,0.26f,0.01f));
        for(const auto& p:player.projectiles()) capsule(p.pos-glm::dvec3(0,0.09,0),0.09,0.18,glm::vec3(1,0.38f,0.01f));
        vertices=static_cast<GLsizei>(data.size());
        glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,data.size()*sizeof(RenderVertex),data.data(),GL_STREAM_DRAW);
    }
};

void Capture(const std::string& path, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    const std::filesystem::path target(path);
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path());
    stbi_flip_vertically_on_write(1);
    if (!stbi_write_png(path.c_str(), width, height, 3, pixels.data(), width * 3))
        throw std::runtime_error("Cannot write capture: " + path);
    std::cout << "CAPTURE " << path << " " << width << "x" << height << '\n';
}

CameraTarget CameraActor(const PlayerSimulation& player) {
    CameraTarget target;
    const auto& s=player.state;
    target.position=s.pos; target.visualPosition=player.visualPos(); target.velocity=s.vel;
    target.grounded=s.grounded; target.climbing=s.climbing; target.squid=s.squid;
    target.swimming=s.submerged; target.landT=s.landT; target.landSpeed=s.landSpeed;
    return target;
}

void PlayLoop(GLFWwindow* window,const Options& options,const StageData& initial) {
    const StageData* stage=&initial;
    std::unique_ptr<StageGeometry> geometry;
    std::unique_ptr<FacePaint> paint;
    std::unique_ptr<PlayerSimulation> player;
    FollowCamera camera;
    StageRenderer renderer,actors;
    bool playing=!options.capture.empty() || options.playProbe, paused=false;
    double accumulator=0, previous=glfwGetTime(), mx=0,my=0;
    int frame=0, steps=0, uploadedVersion=-1;
    bool escapeWas=false;
    bool fireBlocked=true;
    auto captureMouse=[&](bool capture) {
        fireBlocked=true;
        glfwSetInputMode(window,GLFW_CURSOR,capture?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL);
        if(glfwRawMouseMotionSupported()) glfwSetInputMode(window,GLFW_RAW_MOUSE_MOTION,capture?GLFW_TRUE:GLFW_FALSE);
        glfwGetCursorPos(window,&mx,&my);
    };
    auto reset=[&] {
        player.reset(); paint.reset();
        geometry=std::make_unique<StageGeometry>(*stage);
        paint=std::make_unique<FacePaint>(*stage,*geometry);
        player=std::make_unique<PlayerSimulation>(*stage,*geometry,*paint);
        player->spawnAt(stage->spawnPads[0],0);
        camera.yaw=0; camera.pitch=-0.1; camera.reset(CameraActor(*player));
        renderer.Load(*stage,0,*paint); renderer.UploadInk(*stage,*paint);
        uploadedVersion=paint->version(); accumulator=0; steps=0;
    };
    reset(); captureMouse(playing && options.capture.empty());
    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        int width,height; glfwGetFramebufferSize(window,&width,&height);
        const double now=glfwGetTime(), realDt=std::clamp(now-previous,0.0,0.1);
        previous=now;
        if(width<=0 || height<=0) {glfwWaitEventsTimeout(0.05);continue;}
        const bool escape=glfwGetKey(window,GLFW_KEY_ESCAPE)==GLFW_PRESS;
        if(escape && !escapeWas && playing) { paused=!paused; accumulator=0; captureMouse(!paused); }
        if(playing && !paused && options.capture.empty() && !options.playProbe && !glfwGetWindowAttrib(window,GLFW_FOCUSED)) {
            paused=true;accumulator=0;captureMouse(false);
        }
        escapeWas=escape;
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        if(!playing || paused) {
            ImGui::SetNextWindowPos(ImVec2(width*0.5f,height*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
            ImGui::SetNextWindowSize(ImVec2(430,0));
            ImGui::Begin(paused?"Paused":"INKWAVE / Turf Riot",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
            if(!playing) {
                ImGui::TextUnformatted("Native practice preview");
                if(ImGui::BeginCombo("Stage",std::string(stage->name).c_str())) {
                    for(const auto& candidate:ReferenceStages()) {
                        if(candidate.mode!="turf" || candidate.onlineOnly) continue;
                        if(ImGui::Selectable(std::string(candidate.name).c_str(),stage==&candidate)) {stage=&candidate;reset();}
                    }
                    ImGui::EndCombo();
                }
                ImGui::TextUnformatted("Spritzer / Tangerine");
                if(ImGui::Button("Start practice",ImVec2(-1,38))) {playing=true;paused=false;reset();captureMouse(true);}
            } else {
                if(ImGui::Button("Resume",ImVec2(-1,38))) {paused=false;captureMouse(true);}
                if(ImGui::Button("Restart practice",ImVec2(-1,38))) {reset();paused=false;captureMouse(true);}
                if(ImGui::Button("Return to stage select",ImVec2(-1,38))) {playing=false;paused=false;captureMouse(false);}
            }
            ImGui::Separator();
            ImGui::TextWrapped("WASD move / Mouse look / Left click fire / Shift squid / Space jump / Esc pause");
            if(ImGui::Button("Quit",ImVec2(-1,0))) glfwSetWindowShouldClose(window,1);
            ImGui::End();
        }
        if(playing && !paused) {
            double x,y; glfwGetCursorPos(window,&x,&y);
            if(!options.playProbe) {camera.yaw-=(x-mx)*0.0021;camera.pitch=std::clamp(camera.pitch-(y-my)*0.0021,-1.05,1.15);}
            mx=x;my=y;
            accumulator+=options.playProbe?1.0/60:realDt;
            while(accumulator+1e-12>=1.0/60) {
                auto& input=player->input;
                const glm::dvec3 forward(std::sin(camera.yaw),0,std::cos(camera.yaw));
                const glm::dvec3 right(-std::cos(camera.yaw),0,std::sin(camera.yaw));
                input.move=glm::dvec3(0);
                if(options.playProbe) {
                    camera.pitch=-0.28;
                    input.move=steps<120?forward:glm::dvec3(0);
                    input.jump=steps>=70 && steps<76;
                    input.squid=steps>=170 && steps<220;
                    input.fire=steps<165 || steps>=220;
                } else {
                    if(glfwGetKey(window,GLFW_KEY_W)==GLFW_PRESS) input.move+=forward;
                    if(glfwGetKey(window,GLFW_KEY_S)==GLFW_PRESS) input.move-=forward;
                    if(glfwGetKey(window,GLFW_KEY_D)==GLFW_PRESS) input.move+=right;
                    if(glfwGetKey(window,GLFW_KEY_A)==GLFW_PRESS) input.move-=right;
                    input.jump=glfwGetKey(window,GLFW_KEY_SPACE)==GLFW_PRESS;
                    input.squid=glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_RIGHT_SHIFT)==GLFW_PRESS;
                    const bool left=glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
                    if(!left) fireBlocked=false;
                    input.fire=left && !fireBlocked;
                }
                if(glm::length(input.move)>1) input.move=glm::normalize(input.move);
                const auto& pose=camera.pose();
                const auto hit=geometry->raycast(pose.eye,pose.forward,90,true);
                input.aimPoint=hit.hit?hit.point:pose.eye+pose.forward*90.0;
                input.hasAimPoint=true; input.aimYaw=camera.yaw;input.aimPitch=camera.pitch;
                player->tick(1.0/60);camera.tick(1.0/60,CameraActor(*player),*geometry);
                accumulator-=1.0/60;++steps;
            }
        }
        if(uploadedVersion!=paint->version()) {renderer.UploadInk(*stage,*paint);uploadedVersion=paint->version();}
        const auto& pose=camera.pose();
        const glm::vec3 eye(pose.eye), direction(pose.forward);
        const auto vp=glm::perspective(glm::radians(static_cast<float>(pose.verticalFovDegrees)),static_cast<float>(width)/height,0.15f,6500.0f)*
            glm::lookAt(eye,eye+direction,glm::vec3(0,1,0));
        glViewport(0,0,width,height);glEnable(GL_DEPTH_TEST);
        glClearColor(0.64f,0.81f,0.9f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        renderer.Draw(vp,eye,false);actors.LoadActors(*player);actors.Draw(vp,eye,false);
        if(playing) {
            ImGui::SetNextWindowPos(ImVec2(18,18));ImGui::SetNextWindowBgAlpha(0.75f);
            ImGui::Begin("Practice",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs);
            ImGui::Text("%s / Spritzer",std::string(stage->name).c_str());
            ImGui::Text("Practice %02d:%02d",steps/3600,(steps/60)%60);
            ImGui::Text("HP %.0f   Ink %.0f   Special %.0f / 190",player->state.hp,player->state.ink,player->state.specialCharge);
            ImGui::Text("Turf %.2f%%",paint->coverage()[0]*100);
            if(!player->state.alive) ImGui::TextUnformatted("Splatted - respawning...");
            ImGui::End();
            if(!paused && player->state.alive) {
                auto* draw=ImGui::GetForegroundDrawList();
                const ImVec2 center(width*0.5f,height*0.5f);
                draw->AddCircle(center,7,IM_COL32(255,255,255,230),24,1.5f);
                draw->AddCircleFilled(center,1.5f,IM_COL32(255,255,255,255));
            }
        }
        ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ++frame;
        if(options.frames && frame>=options.frames) {
            if(options.playProbe) renderer.VerifyInk(*stage,*paint);
            if(options.playProbe && options.frames>=240 &&
               (steps!=options.frames || player->shotsFired()<1 || paint->counts()[0]<=0 ||
                !std::isfinite(player->state.pos.x+player->state.pos.y+player->state.pos.z)))
                throw std::runtime_error("Native practice probe did not complete movement/fire/paint");
            if(!options.capture.empty()) Capture(options.capture,width,height);
            std::cout<<"PLAYER steps="<<steps<<" pos="<<player->state.pos.x<<","<<player->state.pos.y<<","<<player->state.pos.z
                     <<" hp="<<player->state.hp<<" ink="<<player->state.ink<<" turf="<<paint->counts()[0]<<" shots="<<player->shotsFired()<<'\n';
            if(options.playProbe) {
                paint->clear();renderer.UploadInk(*stage,*paint);renderer.VerifyInk(*stage,*paint);
                paint->splat(stage->spawnPads[0]+glm::dvec3(0,0.02,0),1.5,1,0.5);
                renderer.UploadInk(*stage,*paint);renderer.VerifyInk(*stage,*paint);
            }
            break;
        }
        glfwSwapBuffers(window);
    }
    captureMouse(false);
}

enum class MatchExit { Close, StageSelect };

MatchExit MatchLoop(GLFWwindow* window,const Options& options,const StageData& stage,
                    const std::filesystem::path& characterAssets,bool nativeApp=false) {
    StageGeometry geometry(stage);
    FacePaint paint(stage,geometry);
    NavGraph nav(stage,geometry);
    FollowCamera camera;
    MatchOptions setup;
    setup.seed=options.benchmarkSeed;
    setup.autopilot=false;
    MatchSimulation match(stage,geometry,paint,nav,setup);
    if(match.actors().size()!=8) throw std::runtime_error("Reference Turf match did not create eight actors");
    if(match.bots().size()!=7) throw std::runtime_error("Reference Turf match requires one local actor and seven bots");
    match.startPlaying();
    camera.yaw=match.local().state.yaw;
    camera.pitch=-0.1;
    camera.reset(CameraActor(match.local()));

    StageRenderer renderer;
    renderer.Load(stage,0,paint);
    renderer.UploadInk(stage,paint);
    CharacterArt art(characterAssets);
    struct LiveCharacter {
        std::unique_ptr<CharacterPose> pose;
        CharacterStyle style;
        CharacterSceneInput scene;
    };
    std::vector<std::unique_ptr<LiveCharacter>> characters;
    characters.reserve(match.actors().size());
    const std::array<glm::dvec3,2> teamColors={
        glm::dvec3(1.0,0.258,0.0037),glm::dvec3(0.0258,0.102,1.0)};
    const double dt=1.0/60.0;
    double accumulator=0.0,previous=glfwGetTime();
    int uploadedVersion=paint.version(),frame=0;
    const glm::dvec3 target((stage.bounds.x+stage.bounds.y)*0.5,
        (stage.spawnPads[0].y+stage.spawnPads[1].y)*0.5,
        (stage.bounds.z+stage.bounds.w)*0.5);
    const double span=std::max(stage.bounds.y-stage.bounds.x,stage.bounds.w-stage.bounds.z);
    const glm::dvec3 overviewEye=target+glm::dvec3(span*0.82,span*1.15,span*0.82);
    const bool benchmark=!options.benchmarkReport.empty();
    bool paused=false,escapeWas=false,cheerWas=false,leftWas=false,fireBlocked=true,resultReported=false;
    MatchExit matchExit=MatchExit::Close;
    bool pauseSubWasHeld=false,pauseSpecialWasHeld=false,subResumeSync=false,specialResumeBlock=false;
    double judgeElapsed=0.0;
    double mouseX=0.0,mouseY=0.0;
    auto captureMouse=[&](bool capture) {
        fireBlocked=true;
        glfwSetInputMode(window,GLFW_CURSOR,capture?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL);
        if(glfwRawMouseMotionSupported()) glfwSetInputMode(window,GLFW_RAW_MOUSE_MOTION,capture?GLFW_TRUE:GLFW_FALSE);
        glfwGetCursorPos(window,&mouseX,&mouseY);
    };
    auto pauseMatch=[&] {
        const auto& input=match.local().input;
        pauseSubWasHeld=input.sub || glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS ||
            glfwGetKey(window,GLFW_KEY_E)==GLFW_PRESS;
        pauseSpecialWasHeld=input.special || glfwGetKey(window,GLFW_KEY_F)==GLFW_PRESS ||
            glfwGetKey(window,GLFW_KEY_Q)==GLFW_PRESS;
        paused=true;match.paused=true;accumulator=0.0;captureMouse(false);
    };
    auto resumeMatch=[&] {
        paused=false;match.paused=false;accumulator=0.0;
        subResumeSync=pauseSubWasHeld;specialResumeBlock=pauseSpecialWasHeld;
        captureMouse(true);
    };
    std::uint32_t simulationStep=0;
    auto tick=[&] {
        auto& local=match.local();
        auto& input=local.input;
        input={};
        if(benchmark) {
            const double phase=(static_cast<double>(simulationStep)+options.benchmarkSeed*31.0)/90.0;
            const glm::dvec3 waypoint=target+glm::dvec3(std::sin(phase)*span*0.12,0.0,
                std::cos(phase*0.73)*span*0.12);
            glm::dvec3 direction=waypoint-local.state.pos;
            direction.y=0.0;
            const double distance=glm::length(direction);
            if(distance>1e-6) input.move=direction/distance;
            glm::dvec3 enemy=stage.spawnPads[1]-local.state.pos;
            enemy.y=0.0;
            input.aimYaw=std::atan2(enemy.x,enemy.z);
            input.aimPitch=0.025*std::sin(phase*0.41);
            input.aimPoint=stage.spawnPads[1]+glm::dvec3(0.0,1.0,0.0);
            input.hasAimPoint=true;
            const auto action=simulationStep%240;
            input.fire=action<150;
            input.squid=action>=160&&action<210;
            input.jump=action>=72&&action<78;
            ++simulationStep;
        } else {
            const auto& pose=camera.pose();
            const glm::dvec3 forward(std::sin(camera.yaw),0,std::cos(camera.yaw));
            const glm::dvec3 right(-std::cos(camera.yaw),0,std::sin(camera.yaw));
            if(glfwGetKey(window,GLFW_KEY_W)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_UP)==GLFW_PRESS) input.move+=forward;
            if(glfwGetKey(window,GLFW_KEY_S)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_DOWN)==GLFW_PRESS) input.move-=forward;
            if(glfwGetKey(window,GLFW_KEY_D)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_RIGHT)==GLFW_PRESS) input.move+=right;
            if(glfwGetKey(window,GLFW_KEY_A)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_LEFT)==GLFW_PRESS) input.move-=right;
            if(glm::length(input.move)>1.0) input.move=glm::normalize(input.move);
            input.jump=glfwGetKey(window,GLFW_KEY_SPACE)==GLFW_PRESS;
            input.squid=glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_RIGHT_SHIFT)==GLFW_PRESS;
            const bool left=glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
            if(!left) fireBlocked=false;
            input.fire=left&&!fireBlocked;
            const bool sub=glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_E)==GLFW_PRESS;
            input.sub=sub;
            if(subResumeSync) {
                if(!sub) {
                    input.sub=false;input.squid=true;input.move=glm::dvec3(0);input.fire=false;
                    subResumeSync=false;
                }
            }
            const bool special=glfwGetKey(window,GLFW_KEY_F)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_Q)==GLFW_PRESS;
            input.special=special&&!specialResumeBlock;
            if(specialResumeBlock&&!special) specialResumeBlock=false;
            const bool cheer=glfwGetKey(window,GLFW_KEY_C)==GLFW_PRESS;
            input.cheer=cheer&&!cheerWas;
            cheerWas=cheer;
            const auto hit=geometry.raycast(pose.eye,pose.forward,90.0,true);
            input.aimPoint=hit.hit?hit.point:pose.eye+pose.forward*90.0;
            input.hasAimPoint=true;
            input.aimYaw=camera.yaw;
            input.aimPitch=camera.pitch;
            const bool mapTarget=local.specialPose().mapTarget;
            input.hasMapTarget=mapTarget;
            if(mapTarget) input.mapTarget=input.aimPoint;
            input.specialConfirm=mapTarget&&left&&!fireBlocked&&!leftWas;
            leftWas=left;
        }
        match.tick(dt);
        if(!benchmark) {
            if(match.phase()==MatchPhase::Judge) {
                judgeElapsed+=dt;
                if(judgeElapsed>=2.6) match.finishJudging();
            } else judgeElapsed=0.0;
            if(match.phase()==MatchPhase::Results&&!resultReported) {
                if(const auto& result=match.result())
                    std::cout<<"MATCH_RESULT winner="<<(result->winner==0?"orange":"blue")
                             <<" orange="<<result->coverage[0]*100.0<<" blue="<<result->coverage[1]*100.0<<'\n';
                captureMouse(false);
                resultReported=true;
            }
        }
        if(!benchmark) camera.tick(dt,CameraActor(local),geometry);
    };
    CharacterLightState lighting;
    lighting.ambient=glm::vec3(0.23f,0.25f,0.29f);
    lighting.sunDirection=glm::normalize(glm::vec3(-0.4f,0.85f,0.35f));
    lighting.sunColor=glm::vec3(1.0f,0.92f,0.82f);
    lighting.skyDirection=glm::vec3(0.0f,1.0f,0.0f);
    lighting.skyColor=glm::vec3(0.42f,0.56f,0.75f);
    lighting.groundColor=glm::vec3(0.27f,0.22f,0.18f);
    for(std::size_t i=0;i<match.actors().size();++i) {
        auto* actor=match.actors()[i].get();
        const auto& roster=match.roster()[i];
        auto live=std::make_unique<LiveCharacter>();
        live->style=roster.style;
        live->pose=std::make_unique<CharacterPose>(art.data(),art.data().rig(
            roster.style.hair,roster.style.hat,roster.style.brows),roster.name,actor->mainId());
        live->pose->setStyle(roster.style);
        live->pose->setSub(actor->subId());
        live->pose->setColor(teamColors[static_cast<std::size_t>(actor->team())]);
        live->pose->trigger(CharacterTrigger{"spawn"});
        live->scene.inWorld=true;
        live->scene.isLocal=roster.local;
        live->scene.team=actor->team();
        live->scene.lifeLevel=2;
        live->scene.cameraPosition=overviewEye;
        live->scene.actors.resize(match.actors().size());
        live->scene.raycast=[&geometry](glm::dvec3 origin,glm::dvec3 direction,double distance) {
            const auto hit=geometry.raycast(origin,direction,distance,true);
            return CharacterGroundHit{hit.hit,hit.dist,hit.point,hit.normal};
        };
        auto* state=live.get();
        auto* pose=live->pose.get();
        characters.push_back(std::move(live));
        actor->setPoseTriggerSink([pose](const CharacterTrigger& event) { pose->trigger(event); });
        actor->setPoseFrameSink([&,actor,state,pose,i](double step) {
            const auto& current=actor->state;
            const auto combat=actor->combatPose();
            const auto special=actor->specialPose();
            CharacterAnimState animation;
            animation.position=actor->visualPos();
            if(special.kind=="crab"&&!special.roll) animation.position.y+=special.seat;
            animation.yaw=current.yaw;
            animation.aimPitch=actor->input.aimPitch;
            animation.speed=glm::length(glm::dvec2(current.vel.x,current.vel.z));
            const double c=std::cos(current.yaw),s=std::sin(current.yaw);
            const double horizontalSpeed=std::hypot(current.vel.x,current.vel.z);
            if(horizontalSpeed>0.1) {
                animation.localMove={(-current.vel.x*c+current.vel.z*s)/horizontalSpeed,
                                     (current.vel.x*s+current.vel.z*c)/horizontalSpeed};
            }
            animation.vy=current.vel.y;
            animation.hp=std::clamp(current.hp/100.0,0.0,1.0);
            animation.form=current.squid?(current.climbing?CharacterForm::Climb:
                current.submerged?CharacterForm::Swim:CharacterForm::Squid):CharacterForm::Kid;
            if(special.body||special.noSquid) animation.form=CharacterForm::Kid;
            animation.grounded=current.grounded;
            animation.firing=combat.firing||special.firing>0.0;
            animation.rolling=combat.rolling;
            animation.subAim=combat.subAim||special.raise;
            animation.subPropHidden=special.kind=="booyah"&&special.active&&!special.thrown;
            animation.lowInk=current.ink<18.0;
            animation.inEnemyInk=current.grounded&&current.groundTeam==2&&!current.submerged&&!special.inkProof;
            animation.invuln=current.invuln>0.0;
            animation.visible=current.alive&&special.kind!="kraken"&&!(special.kind=="crab"&&special.roll);
            animation.charge=combat.charge;
            animation.ink=current.ink/100.0;
            animation.special=special.active?std::clamp(special.remaining,0.0,1.0):
                std::clamp(current.specialCharge/actor->weapon().number("specialCost",190.0),0.0,1.0);
            const double hurt=std::clamp(std::max(current.hurtFlash,1.0-current.hp/100.0)*
                (current.hp<100.0?1.0:0.0),0.0,1.0);
            pose->setHurt(hurt,teamColors[static_cast<std::size_t>(1-actor->team())]);
            CharacterRunnerState runner;
            runner.charging=combat.charging;
            runner.firing=combat.firing;
            runner.streaming=combat.streaming;
            runner.dodge=combat.dodgeTime>0.0;
            runner.lockT=combat.lockTime;
            runner.charge=combat.charge;
            runner.streamT=combat.burstTime;
            runner.hand=combat.hand;
            runner.sinceHand=combat.sinceHand;runner.hasSinceHand=true;
            runner.leaping=combat.leaping;
            runner.open=runner.canopyOpen=combat.canopyOpen;
            runner.canopyGrow=combat.canopyGrow;
            runner.canopyHp=combat.canopyHp;runner.canopyHpMax=combat.canopyHpMax;
            runner.canopyShake=combat.canopyShake;runner.canopyFlash=combat.canopyFlash;
            runner.canopyRegrowK=combat.canopyRegrowK;runner.canopyOpening=combat.canopyOpening;
            runner.canopyState=combat.canopyState;
            runner.bladeFlashTime=combat.bladeFlashTime;
            runner.mittsLandTime=combat.mittsLandTime;
            animation.runner=runner;

            auto& scene=state->scene;
            scene.cameraPosition=options.benchmarkReport.empty() ? camera.pose().eye : overviewEye;
            for(std::size_t j=0;j<match.actors().size();++j) {
                const auto& other=*match.actors()[j];
                scene.actors[j]={j+1,other.team(),j!=i&&other.state.alive,other.visualPos()};
            }
            pose->update(step,animation,scene);
        });
    }

    std::unique_ptr<PerformanceCapture> capture;
    if(!options.benchmarkReport.empty())
        capture=std::make_unique<PerformanceCapture>(options.benchmarkReport,std::string(stage.id),options.benchmarkSeed,
            options.width,options.height,options.benchmarkWarmup,options.benchmarkFrames);
    if(!benchmark) captureMouse(true);
    glEnable(GL_DEPTH_TEST);
    while(!glfwWindowShouldClose(window)) {
        if(capture) capture->beginFrame();
        glfwPollEvents();
        int width=0,height=0;
        glfwGetFramebufferSize(window,&width,&height);
        if(width<=0||height<=0) {if(capture)throw std::runtime_error("Benchmark framebuffer is unavailable");glfwWaitEventsTimeout(0.05);continue;}
        if(capture && (width!=1920 || height!=1080)) throw std::runtime_error("Benchmark framebuffer must be exactly 1920x1080; got "+std::to_string(width)+"x"+std::to_string(height));
        const double now=glfwGetTime();
        if(!benchmark) {
            const bool escape=glfwGetKey(window,GLFW_KEY_ESCAPE)==GLFW_PRESS;
            if(match.phase()==MatchPhase::Playing && escape&&!escapeWas) {
                if(paused) resumeMatch(); else pauseMatch();
            }
            escapeWas=escape;
            if(!paused&&match.phase()==MatchPhase::Playing&&!glfwGetWindowAttrib(window,GLFW_FOCUSED)) pauseMatch();
            if(!paused && match.phase()==MatchPhase::Playing) {
                double x=0.0,y=0.0;
                glfwGetCursorPos(window,&x,&y);
                camera.yaw-=(x-mouseX)*0.0021;
                camera.pitch=std::clamp(camera.pitch-(y-mouseY)*0.0021,-1.05,1.15);
                mouseX=x;mouseY=y;
            }
        }
        if(capture) tick();
        else if(!paused) {
            accumulator+=std::clamp(now-previous,0.0,0.1);
            while(accumulator+1e-12>=dt) {tick();accumulator-=dt;}
        }
        previous=now;
        if(uploadedVersion!=paint.version()) {renderer.UploadInk(stage,paint);uploadedVersion=paint.version();}
        const glm::dvec3 renderEye=benchmark?overviewEye:camera.pose().eye;
        const glm::dvec3 renderForward=benchmark?glm::normalize(target-overviewEye):camera.pose().forward;
        glm::mat4 projection,view;
        if(benchmark) {
            projection=glm::perspective(glm::radians(48.0f),static_cast<float>(width)/height,0.15f,6500.0f);
            view=glm::lookAt(glm::vec3(overviewEye),glm::vec3(target),glm::vec3(0,1,0));
        } else {
            projection=glm::perspective(glm::radians(static_cast<float>(camera.pose().verticalFovDegrees)),static_cast<float>(width)/height,0.15f,6500.0f);
            view=glm::lookAt(glm::vec3(renderEye),glm::vec3(renderEye+renderForward),glm::vec3(0,1,0));
        }
        const auto vp=projection*view;
        glViewport(0,0,width,height);
        glClearColor(0.64f,0.81f,0.9f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        int draws=renderer.Draw(vp,glm::vec3(renderEye),false);
        for(std::size_t i=0;i<characters.size();++i) {
            const auto& actor=*match.actors()[i];
            draws+=static_cast<int>(art.draw(characters[i]->pose->frame(),characters[i]->style,
                actor.mainId(),actor.subId(),"game",glm::dmat4(view),glm::dmat4(projection),renderEye,lighting));
        }
        if(!capture) {
            ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
            if(match.phase()==MatchPhase::Results) {
                ImGui::SetNextWindowPos(ImVec2(width*0.5f,height*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
                ImGui::SetNextWindowSize(ImVec2(440,0));
                ImGui::Begin("Turf results",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
                if(const auto& result=match.result()) {
                    ImGui::Text("Team %s wins",result->winner==0?"Orange":"Blue");
                    ImGui::Text("Final turf  Orange %4.1f%%  /  Blue %4.1f%%",
                        result->coverage[0]*100.0,result->coverage[1]*100.0);
                }
                if(nativeApp && ImGui::Button("Replay / choose stage",ImVec2(-1,36))) matchExit=MatchExit::StageSelect;
                if(ImGui::Button(nativeApp?"Close game":"Close match",ImVec2(-1,36))) glfwSetWindowShouldClose(window,1);
                ImGui::End();
            } else if(paused) {
                ImGui::SetNextWindowPos(ImVec2(width*0.5f,height*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
                ImGui::SetNextWindowSize(ImVec2(420,0));
                ImGui::Begin("Paused",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
                ImGui::TextUnformatted("The match clock and simulation are paused.");
                if(ImGui::Button("Resume",ImVec2(-1,36))) resumeMatch();
                if(nativeApp && ImGui::Button("Return to stage select",ImVec2(-1,36))) matchExit=MatchExit::StageSelect;
                if(ImGui::Button(nativeApp?"Close game":"Close match",ImVec2(-1,36))) glfwSetWindowShouldClose(window,1);
                ImGui::End();
            } else if(match.phase()==MatchPhase::Finish || match.phase()==MatchPhase::Judge) {
                ImGui::SetNextWindowPos(ImVec2(width*0.5f,height*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
                ImGui::SetNextWindowSize(ImVec2(440,0));
                ImGui::Begin("Match tally",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoInputs);
                const auto coverage=paint.coverage();
                if(match.phase()==MatchPhase::Finish) ImGui::TextUnformatted("Time is up. Counting final turf...");
                else if(const auto& result=match.result()) {
                    ImGui::Text("Team %s wins",result->winner==0?"Orange":"Blue");
                    ImGui::TextUnformatted("Final turf tally");
                }
                ImGui::Text("Orange %4.1f%%  /  Blue %4.1f%%",coverage[0]*100.0,coverage[1]*100.0);
                ImGui::End();
            } else {
                ImGui::SetNextWindowPos(ImVec2(18,18));
                ImGui::Begin("Turf Riot",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs);
                const auto coverage=paint.coverage();
                const auto& local=match.local();
                const int seconds=static_cast<int>(std::ceil(std::max(0.0,match.remaining())));
                ImGui::Text("%s",std::string(stage.name).c_str());
                ImGui::Text("%02d:%02d",seconds/60,seconds%60);
                ImGui::Text("Turf  %4.1f%%  /  %4.1f%%",coverage[0]*100.0,coverage[1]*100.0);
                ImGui::Text("HP %3.0f   Ink %3.0f   Special %3.0f / %.0f",local.state.hp,local.state.ink,
                    local.state.specialCharge,local.weapon().number("specialCost",190.0));
                if(!local.state.alive) ImGui::Text("Splatted - respawning in %.1f",local.state.respawnTimer);
                ImGui::Text("WASD / Mouse   Fire LMB   Sub RMB or E   Special F or Q");
                ImGui::Text("Shift squid   Space jump   C cheer   Esc pause");
                if(local.specialPose().mapTarget)
                    ImGui::TextUnformatted("Aim at a strike target; click to confirm after the ready delay.");
                ImGui::End();
                auto* draw=ImGui::GetForegroundDrawList();
                const ImVec2 center(width*0.5f,height*0.5f);
                draw->AddCircle(center,7,IM_COL32(255,255,255,230),24,1.5f);
                draw->AddCircleFilled(center,1.5f,IM_COL32(255,255,255,255));
            }
            ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if(matchExit==MatchExit::StageSelect) break;
        }
        if(options.frames&&++frame>=options.frames) break;
        if(capture) capture->endGpu();
        glfwSwapBuffers(window);
        if(capture) {
            const PerformanceCapture::FrameCounts counts{
                static_cast<int>(match.actors().size()),draws,0,
                static_cast<int>(match.world().projectiles().size())};
            if(capture->endFrame(counts)) {capture->write();break;}
        }
    }
    if(!benchmark) captureMouse(false);
    return matchExit;
}

void NativeMenu(GLFWwindow* window,const Options& options,const StageData& initial,
                const std::filesystem::path& characterAssets) {
    const StageData* selected=&initial;
    int frame=0;
    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        int width=0,height=0;
        glfwGetFramebufferSize(window,&width,&height);
        if(width<=0||height<=0) {glfwWaitEventsTimeout(0.05);continue;}
        ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(width*0.5f,height*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
        ImGui::SetNextWindowSize(ImVec2(460,0));
        ImGui::Begin("INKWAVE / Turf Riot",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
        ImGui::TextUnformatted("Select an offline Turf stage.");
        if(ImGui::BeginCombo("Stage",std::string(selected->name).c_str())) {
            for(const auto& candidate:ReferenceStages()) {
                if(candidate.mode!="turf"||candidate.onlineOnly) continue;
                if(ImGui::Selectable(std::string(candidate.name).c_str(),selected==&candidate)) selected=&candidate;
            }
            ImGui::EndCombo();
        }
        ImGui::TextUnformatted("WASD / Mouse to play, Shift squid, Space jump.");
        const bool start=ImGui::Button("Start Turf match",ImVec2(-1,38));
        const bool close=ImGui::Button("Close",ImVec2(-1,32));
        ImGui::End();
        glViewport(0,0,width,height);
        glClearColor(0.06f,0.11f,0.15f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        if(close) {glfwSetWindowShouldClose(window,1);break;}
        if(options.frames&&++frame>=options.frames) {
            std::cout<<"NATIVE_MENU state=stage-select stage="<<selected->id<<" frames="<<frame<<'\n';
            break;
        }
        if(start) {
            Options game=options;
            game.stage=std::string(selected->id);
            game.match=true;
            game.native=false;
            if(MatchLoop(window,game,*selected,characterAssets,true)==MatchExit::Close) break;
            frame=0;
        }
    }
}

void Loop(GLFWwindow* window, const Options& options, const StageData& initial) {
    StageRenderer renderer;
    const StageData* stage = &initial;
    auto geometry=std::make_unique<StageGeometry>(*stage);
    auto paint=std::make_unique<FacePaint>(*stage,*geometry);
    if(options.paintProbe) {
        for(int t=0;t<2;++t) {
            const auto spawn=stage->spawnPads[t];
            paint->splat(spawn+glm::dvec3(0,0.02,0),4.0,t,0.2+t*0.3);
        }
        for(int i=0;i<8;++i) {
            const float x=-10.5f+i*3.0f;
            const double h=geometry->groundHeight(x,0);
            if(std::isfinite(h)) paint->splat(glm::dvec3(x,h+0.02,0),2.5,i%2,0.125*i);
        }
    }
    int display = 0;
    bool wire = false;
    renderer.Load(*stage, display,*paint);
    renderer.UploadInk(*stage,*paint);
    glm::vec3 target;
    float distance = 1, yaw = 0.55f, pitch = 0.85f;
    auto reset = [&] {
        target = glm::vec3((stage->bounds.x + stage->bounds.y)*0.5f,0,
                              (stage->bounds.z + stage->bounds.w)*0.5f);
        const glm::dvec2 extent(stage->bounds.y-stage->bounds.x,stage->bounds.w-stage->bounds.z);
        distance = static_cast<float>(glm::length(extent)*1.4);
        yaw=0.55f; pitch=0.85f;
    };
    reset();
    double lastX=0,lastY=0;
    bool dragging=false;
    bool painting=false;
    int team=0;
    float brushRadius=1.5f;
    unsigned int splatIndex=0;
    int frame=0;
    double previous=glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        int width=0,height=0;
        glfwGetFramebufferSize(window,&width,&height);
        if (width<=0 || height<=0) { glfwWaitEventsTimeout(0.05); continue; }
        const double now=glfwGetTime();
        const float dt=static_cast<float>(std::min(now-previous,0.05));
        previous=now;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(18,18),ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(365,0),ImGuiCond_FirstUseEver);
        ImGui::Begin("OpenGL migration / stage lab");
        ImGui::TextWrapped("Layout and prop collision data. Prop art, actors and full ink rendering are pending.");
        const auto& stages=ReferenceStages();
        if (ImGui::BeginCombo("Stage / mode",(std::string(stage->id)+" / "+std::string(stage->mode)).c_str())) {
            for (const auto& candidate:stages) {
                const std::string label=std::string(candidate.id)+" / "+std::string(candidate.mode);
                if (ImGui::Selectable(label.c_str(),stage==&candidate)) {
                    stage=&candidate;
                    paint.reset();
                    geometry=std::make_unique<StageGeometry>(*stage);
                    paint=std::make_unique<FacePaint>(*stage,*geometry);
                    renderer.Load(*stage,display,*paint);renderer.UploadInk(*stage,*paint);reset();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Text("%s",std::string(stage->name).c_str());
        ImGui::Text("Blocks: %zu   Faces: %zu",stage->blocks.size(),stage->faces.size());
        ImGui::Text("Layout: %d   Prop colliders: %d",stage->layoutBlockCount,stage->propColliderCount);
        if (stage->onlineOnly) ImGui::TextWrapped("Online-only stage; inspection does not enable offline play.");
        const char* views[]={"Base colors","Paintable faces","Scoring faces","Collision volumes"};
        if (ImGui::Combo("Inspect",&display,views,4)) renderer.Load(*stage,display,*paint);
        if(display==3) ImGui::TextWrapped("Red: prop collision / Purple: rails / Teal: layout. These boxes are diagnostics, not prop art.");
        ImGui::Checkbox("Wireframe",&wire);
        if (ImGui::Button("Reset camera")) reset();
        ImGui::RadioButton("Tangerine",&team,0);ImGui::SameLine();ImGui::RadioButton("Cobalt",&team,1);
        ImGui::SliderFloat("Splat radius (m)",&brushRadius,0.2f,5.0f);
        if(ImGui::Button("Clear logic ink")){paint->clear();renderer.UploadInk(*stage,*paint);}
        const auto coverage=paint->coverage();
        ImGui::Text("Coverage %.2f%% / %.2f%%",coverage[0]*100,coverage[1]*100);
        ImGui::Text("Live turf cells: %d",paint->turfTotal());
        ImGui::TextWrapped("Left-click: seeded logic splat / Right-drag: orbit / W,S: zoom / A,D: pan / Esc: close");
        ImGui::TextWrapped("Ink view displays CPU ownership cells, not final ink shading.");
        ImGui::TextWrapped("Diagnostic renderer; visual parity is not certified.");
        ImGui::End();
        double mouseX,mouseY;
        glfwGetCursorPos(window,&mouseX,&mouseY);
        const bool right=glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS;
        if (!ImGui::GetIO().WantCaptureMouse && right && dragging) {
            yaw-=static_cast<float>(mouseX-lastX)*0.005f;
            pitch=std::clamp(pitch+static_cast<float>(mouseY-lastY)*0.005f,0.08f,1.54f);
        }
        dragging=right;lastX=mouseX;lastY=mouseY;
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            if (glfwGetKey(window,GLFW_KEY_ESCAPE)==GLFW_PRESS) glfwSetWindowShouldClose(window,1);
            if (glfwGetKey(window,GLFW_KEY_W)==GLFW_PRESS) distance*=std::exp(-dt);
            if (glfwGetKey(window,GLFW_KEY_S)==GLFW_PRESS) distance*=std::exp(dt);
            const glm::vec3 side(std::cos(yaw),0,-std::sin(yaw));
            if (glfwGetKey(window,GLFW_KEY_A)==GLFW_PRESS) target-=side*distance*dt*0.4f;
            if (glfwGetKey(window,GLFW_KEY_D)==GLFW_PRESS) target+=side*distance*dt*0.4f;
        }
        distance=std::clamp(distance,3.0f,500.0f);
        const glm::vec3 eye=target+glm::vec3(std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch))*distance;
        const glm::mat4 projection=glm::perspective(glm::radians(48.0f),static_cast<float>(width)/height,0.1f,1500.0f);
        const glm::mat4 view=glm::lookAt(eye,target,glm::vec3(0,1,0));
        const bool left=glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
        if(left && !painting && !ImGui::GetIO().WantCaptureMouse) {
            int windowWidth,windowHeight;
            glfwGetWindowSize(window,&windowWidth,&windowHeight);
            if(windowWidth>0 && windowHeight>0) {
                glm::vec4 ray=glm::inverse(projection*view)*glm::vec4(
                    static_cast<float>(mouseX/windowWidth)*2-1,1-static_cast<float>(mouseY/windowHeight)*2,1,1);
                const auto direction=glm::normalize(glm::vec3(ray)/ray.w-eye);
                const auto hit=geometry->raycast(eye,direction,1500,true);
                if(hit.hit) {
                    const float seed=static_cast<float>((++splatIndex*2654435761u)&65535u)/65536.0f;
                    paint->splat(hit.point+hit.normal*0.02,brushRadius,team,seed);
                    renderer.UploadInk(*stage,*paint);
                }
            }
        }
        painting=left;
        glViewport(0,0,width,height);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.64f,0.81f,0.9f,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        renderer.Draw(projection*view,eye,wire);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ++frame;
        if (options.frames && frame>=options.frames) {
            if (!options.capture.empty()) Capture(options.capture,width,height);
            break;
        }
        glfwSwapBuffers(window);
    }
}
}

int RunStageViewer(int argc, char** argv) {
    GLFWwindow* window=nullptr;
    bool gui=false,glfw=false;
    try {
        const auto options=ParseOptions(argc,argv);
        const auto& stages=ReferenceStages();
        const auto found=std::find_if(stages.begin(),stages.end(),[&](const StageData& stage){
            return stage.id==options.stage && stage.mode==options.mode;
        });
        if (found==stages.end()) throw std::runtime_error("Unknown or unsupported stage/mode: "+options.stage+"/"+options.mode);
        if((options.game || options.match || options.native) && (found->onlineOnly || found->mode!="turf"))
            throw std::runtime_error("Native practice currently requires an offline Turf stage");
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfw=true;
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        const bool hidden=!options.capture.empty()||!options.benchmarkReport.empty();
        glfwWindowHint(GLFW_VISIBLE,hidden?GLFW_FALSE:GLFW_TRUE);
        if(!options.benchmarkReport.empty()) glfwWindowHint(GLFW_DECORATED,GLFW_FALSE);
        window=glfwCreateWindow(options.width,options.height,"Tiny-Splatoon",nullptr,nullptr);
        if (!window) throw std::runtime_error("OpenGL 4.5 window creation failed");
        glfwMakeContextCurrent(window);
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) throw std::runtime_error("GLAD initialization failed");
        std::cout<<"GPU "<<glGetString(GL_RENDERER)<<" / "<<glGetString(GL_VERSION)<<'\n';
        std::cout<<"STAGE "<<found->id<<" MODE "<<found->mode<<" BLOCKS "<<found->blocks.size()<<" FACES "<<found->faces.size()
                 <<" PROP_COLLIDERS "<<found->propColliderCount<<'\n';
        glfwSwapInterval(hidden?0:1);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        gui=true;
        ImGui::GetIO().IniFilename=nullptr;
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOpenGL(window,true);
        ImGui_ImplOpenGL3_Init("#version 450 core");
        if(options.native) {
            auto assets=std::filesystem::absolute(argv[0]).parent_path()/"assets"/"reference"/"characters";
            if(!std::filesystem::exists(assets)) assets=std::filesystem::current_path()/"assets"/"reference"/"characters";
            NativeMenu(window,options,*found,assets);
        }
        else if(options.match) {
            auto assets=std::filesystem::absolute(argv[0]).parent_path()/"assets"/"reference"/"characters";
            if(!std::filesystem::exists(assets)) assets=std::filesystem::current_path()/"assets"/"reference"/"characters";
            MatchLoop(window,options,*found,assets);
        }
        else if(options.game) PlayLoop(window,options,*found);
        else Loop(window,options,*found);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();gui=false;
        glfwDestroyWindow(window);window=nullptr;
        glfwTerminate();glfw=false;
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"Stage lab: "<<error.what()<<'\n';
        if (gui) {ImGui_ImplOpenGL3_Shutdown();ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();}
        if (window) glfwDestroyWindow(window);
        if (glfw) glfwTerminate();
        return 1;
    }
}
}
