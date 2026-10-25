#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <vector>

namespace restoration {
class StageGeometry;
struct InkStreamEmitter {
    std::uint64_t id=0;bool active=false;int team=0;
    glm::dvec3 position{0},velocity{0,0,25};
    double radius=.10,straight=.12,gravity=28,drag=.8,life=.65,seed=.2;
};
struct InkStreamPoint { glm::vec3 position{0};float radius=.1f; };
struct InkStreamPiece { std::uint64_t id=0;int team=0;std::vector<InkStreamPoint> points; };
struct InkStreamDrop { glm::vec3 position{0},velocity{0};float radius=.05f,age=0,life=.25f;int team=0; };
struct InkStreamSplash { glm::dvec3 position{0},normal{0,1,0};double radius=.15,seed=.2;int team=0; };
struct InkStreamStats { std::uint64_t emitted=0,splits=0,impacts=0; };

// Visual-only fixed-step emitter; it never writes paint, health or gameplay RNG.
class InkStream {
public:
    void Update(double dt,const std::vector<InkStreamEmitter>& emitters,const StageGeometry& geometry);
    void Clear();
    const std::vector<InkStreamPiece>& pieces() const{return pieces_;}
    const std::vector<InkStreamDrop>& drops() const{return drops_;}
    const std::vector<InkStreamSplash>& splashes() const{return splashes_;}
    const InkStreamStats& stats() const{return stats_;}
    double time() const{return time_;}
private:
    struct Node {glm::dvec3 pos,vel;double born=0,radius=.1,straight=.12,gravity=28,drag=.8,life=.65;std::uint64_t sequence=0;bool hit=false;double hitAt=0;int period=12,seam=8;double closeAge=.16;};
    struct Chain {std::uint64_t id=0,emitter=0;int team=0;std::vector<Node> nodes;};
    struct Source {std::uint64_t chain=0,sequence=0;bool active=false;double splashAt=-1;InkStreamEmitter previous;};
    void Step(const std::vector<InkStreamEmitter>& emitters,const StageGeometry& geometry);
    void Rebuild();
    double time_=0,accumulator_=0;std::uint64_t nextChain_=1;
    std::map<std::uint64_t,Source> sources_;
    std::vector<Chain> chains_;
    std::vector<InkStreamPiece> pieces_;
    std::vector<InkStreamDrop> drops_;
    std::vector<InkStreamSplash> splashes_;
    InkStreamStats stats_;
};
}
