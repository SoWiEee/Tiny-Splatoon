#include "StageRenderer.hpp"
#include "FacePaint.hpp"
#include "PlayerSimulation.hpp"
#include "StageGeometry.hpp"
#include "DirectionalShadow.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/constants.hpp>

namespace restoration {
namespace {
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

}

StageRenderer::StageRenderer() {
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
uniform bool linearOutput;
uniform bool useSunShadow;
void main(){
    vec3 n=normalize(normal);
    vec3 l=normalize(vec3(-0.4,0.85,0.35));
    vec3 base=color;
    if(inkUV.x>=0.0){
        uint owner=texture(ownership,inkUV).r;
        if(owner==1u)base=pow(vec3(1.0,0.5412,0.0784),vec3(2.2));
        if(owner==2u)base=pow(vec3(0.1843,0.3569,1.0),vec3(2.2));
    }
    float shadow=useSunShadow?sampleSunShadow(world,n):1.0;
    vec3 linear=base*(0.42+0.58*max(dot(n,l),0.0)*mix(1.0,shadow,0.72));
    float fog=1-exp(-length(world-eye)*0.0015);
    linear=mix(linear,vec3(0.60,0.77,0.84),fog);
    vec3 srgb=mix(linear*12.92,1.055*pow(max(linear,vec3(0)),vec3(1.0/2.4))-0.055,step(vec3(0.0031308),linear));
    outColor=vec4(linearOutput?linear:srgb,1);
}
)GLSL";
        std::string fragmentSource=fs;
        const auto mainAt=fragmentSource.find("void main()");
        if(mainAt==std::string::npos)throw std::runtime_error("Stage fragment shader has no main");
        fragmentSource.insert(mainAt,DirectionalShadow::SamplingGLSL());
        const GLuint vertex = Compile(GL_VERTEX_SHADER, vs);
        GLuint fragment = 0;
        try { fragment = Compile(GL_FRAGMENT_SHADER, fragmentSource.c_str()); }
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
StageRenderer::~StageRenderer() {
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(program);
        glDeleteTextures(1,&ink);
    }
void StageRenderer::Load(const StageData& stage, int display, const FacePaint& paint) {
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
void StageRenderer::UploadInk(const StageData& stage,const FacePaint& paint) {
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
void StageRenderer::VerifyInk(const StageData& stage,const FacePaint& paint) const {
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
int StageRenderer::DrawShadow(const glm::mat4& lightViewProjection) {
        if(vertices<=0)return 0;
        GLboolean previousMask[4]{};glGetBooleanv(GL_COLOR_WRITEMASK,previousMask);
        const GLboolean depthTest=glIsEnabled(GL_DEPTH_TEST);
        GLboolean depthMask=GL_TRUE;glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
        glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,ink);
        glUniform1i(glGetUniformLocation(program,"ownership"),0);
        glUniformMatrix4fv(glGetUniformLocation(program,"vp"),1,GL_FALSE,&lightViewProjection[0][0]);
        const glm::vec3 origin(0.0f);glUniform3fv(glGetUniformLocation(program,"eye"),1,&origin[0]);
        glUniform1i(glGetUniformLocation(program,"linearOutput"),GL_TRUE);
        glUniform1i(glGetUniformLocation(program,"useSunShadow"),GL_FALSE);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,vertices);glBindVertexArray(0);
        glColorMask(previousMask[0],previousMask[1],previousMask[2],previousMask[3]);
        glDepthMask(depthMask);if(!depthTest)glDisable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        return 1;
}
int StageRenderer::Draw(const glm::mat4& vp, const glm::vec3& eye, bool wireframe,bool linearOutput,const DirectionalShadow* shadow) {
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,ink);
        glUniform1i(glGetUniformLocation(program,"ownership"),0);
        glUniformMatrix4fv(glGetUniformLocation(program, "vp"), 1, GL_FALSE, &vp[0][0]);
        glUniform3fv(glGetUniformLocation(program,"eye"),1,&eye[0]);
        glUniform1i(glGetUniformLocation(program,"linearOutput"),linearOutput);
        const bool useShadow=shadow&&shadow->texture()!=0;
        glUniform1i(glGetUniformLocation(program,"useSunShadow"),useShadow?GL_TRUE:GL_FALSE);
        if(useShadow){
            glActiveTexture(GL_TEXTURE8);glBindTexture(GL_TEXTURE_2D,shadow->texture());
            glUniform1i(glGetUniformLocation(program,"sunShadow"),8);
            const glm::mat4 matrix(shadow->matrix());
            glUniformMatrix4fv(glGetUniformLocation(program,"sunShadowMatrix"),1,GL_FALSE,&matrix[0][0]);
            glUniform1f(glGetUniformLocation(program,"sunShadowBias"),static_cast<float>(shadow->bias()));
            glUniform1f(glGetUniformLocation(program,"sunShadowNormalBias"),static_cast<float>(shadow->normalBias()));
            glUniform1f(glGetUniformLocation(program,"sunShadowRadius"),static_cast<float>(shadow->radius()));
        }
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, vertices);
        glBindVertexArray(0);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        return vertices > 0 ? 1 : 0;
    }
void StageRenderer::LoadActors(const PlayerSimulation& player) {
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
} // namespace restoration
