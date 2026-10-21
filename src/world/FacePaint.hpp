#pragma once

#include "ReferenceData.hpp"

#include <array>
#include <cstdint>
#include <glm/vec2.hpp>
#include <vector>

namespace restoration {

class StageGeometry;

enum class SplatKind { Shot, Line, Blast, Bomb, Trail, Drop, Roll, Speck };

struct SplatOptions {
    glm::dvec3 stretch{0.0};
    double stretchAmt = 1.0;
    bool hasStretch = false;
    bool cosmetic = false;
    SplatKind kind = SplatKind::Shot;
};

struct VisualSplat {
    glm::dvec3 center, stretch;
    double radius = 0.0, seed = 0.0, stretchAmt = 1.0;
    int team = 0;
    SplatKind kind = SplatKind::Shot;
    bool hasStretch = false, cosmetic = false;
};

struct VisualFlood {
    int regionId = -1; // a new request replaces the in-progress flood with the same ID
    int team = -1; // -1 wipes to bare floor
    std::vector<int> cells; // live turf cell indices from the prepared region
    std::vector<std::vector<glm::dvec2>> polygons;
    double y0 = -2.0, y1 = 6.0;
    glm::dvec3 center{0.0};
    double reach = 0.0, start = 0.0, duration = 0.0;
    bool easeOut = false;
};

struct AtlasRect {
    int x = -1, y = -1, width = 0, height = 0, pad = 8;
    double ppm = 0.0;
};

struct FaceGrid {
    int offset = -1, columns = 0, rows = 0;
    double cellWidth = 0.0, cellHeight = 0.0;
};

// CPU gameplay ink and the paintable-face atlas layout from the browser PaintSystem.
class FacePaint {
public:
    FacePaint(const StageData& stage, const StageGeometry& geometry, int atlasSize = 4096);

    double splat(const glm::dvec3& center, double radius, int team, double seed, const SplatOptions& options = {});
    bool setCells(const std::vector<int>& cells, int team); // team 0/1, or -1 for bare floor; updates turf counts/version
    std::vector<VisualSplat> takeVisualSplats();
    void requestVisualFlood(VisualFlood flood);
    std::vector<VisualFlood> takeVisualFloods();
    bool takeVisualClear();
    int sample(int faceId, double u, double v) const;
    std::array<double, 2> coverage() const;
    void clear();

    const AtlasRect& atlas(int faceId) const { return atlas_.at(faceId); }
    const FaceGrid& faceGrid(int faceId) const { return faces_.at(faceId); }
    const std::vector<std::uint8_t>& grid() const { return grid_; }
    const std::vector<std::uint8_t>& dead() const { return dead_; }
    const std::array<int, 2>& counts() const { return counts_; }
    int turfTotal() const { return turfTotal_; }
    double turfArea() const { return turfArea_; }
    double pixelsPerMetre() const { return ppm_; }
    int atlasSize() const { return atlasSize_; }
    int usedHeight() const { return usedHeight_; }
    int version() const { return version_; }

private:
    bool tryPack(double ppm);
    double splatFace(int faceId, double lu, double lv, double radius, int team, double seed,
                     double sdu, double sdv, double stretchAmt, SplatKind kind);

    const StageData& stage_;
    const StageGeometry& geometry_;
    int atlasSize_;
    std::vector<AtlasRect> atlas_;
    std::vector<FaceGrid> faces_;
    std::vector<std::uint8_t> grid_, dead_, turfCell_;
    std::vector<VisualSplat> visualSplats_;
    std::vector<VisualFlood> visualFloods_;
    bool visualClear_ = false;
    std::array<int, 2> counts_{0, 0};
    int turfTotal_ = 0, usedHeight_ = 0, version_ = 0;
    double turfArea_ = 0.0, ppm_ = 0.0;
};

double blobWobble(double angle, double seed);

} // namespace restoration
