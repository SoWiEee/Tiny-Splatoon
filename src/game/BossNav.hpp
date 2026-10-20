#pragma once

#include "StageGeometry.hpp"
#include <glm/vec2.hpp>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace restoration {

class BossNav {
public:
    BossNav(const StageData& stage,const StageGeometry& geometry);
    std::pair<double,double> xz(int cell) const;
    int cell(double x,double z) const;
    double floorAt(double x,double z) const;
    double wallClear(double x,double z) const;
    double floorClear(double x,double z) const;
    int kindAt(double x,double z) const;
    bool isPlan(double x,double z) const;
    bool inPad(double x,double z,double margin=0.0) const;
    bool poseOk(double x,double z,double yaw) const;
    bool turnOk(double x,double z,double yaw) const;
    struct Cast { double dist=0.0; bool wall=false; };
    Cast cast(double x,double z,double yaw,double maximum,double step=0.25) const;
    std::vector<glm::dvec2> path(double x0,double z0,double x1,double z1) const;
    glm::dvec2 randomPoint(double value) const;
    const std::vector<int>& planIds() const { return planIds_; }
    double wallDistance(int i) const { return wallDistance_.at(i); }
    double dropDistance(int i) const { return dropDistance_.at(i); }
    double floorY() const { return floorY_; }
private:
    double fieldAt(const std::vector<float>& field,double x,double z) const;
    std::vector<float> field(bool drop) const;
    bool cellOk(int cell,double slack) const;
    bool lineOk(double x0,double z0,double x1,double z1,double slack=0.3) const;
    int nearestPlan(double x,double z) const;
    const StageData& stage_;
    const StageGeometry& geometry_;
    double x0_=0.0,z0_=0.0,floorY_=0.0;
    int nx_=0,nz_=0;
    std::array<glm::dvec3,2> pads_{};
    std::vector<float> floor_,wallDistance_,dropDistance_;
    std::vector<std::uint8_t> kind_,plan_;
    std::vector<int> planIds_;
};
} // namespace restoration
