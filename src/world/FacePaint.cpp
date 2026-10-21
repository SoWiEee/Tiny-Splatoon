#include "FacePaint.hpp"
#include "StageGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

#include <glm/geometric.hpp>

namespace restoration {
namespace {
constexpr double kCell = 0.25;
constexpr double kWobMax = 1.5;
constexpr double kBandL = 0.55, kBandW = 0.62, kBandR = 0.1;
constexpr double kReach[] = {2.45, 2.1, 2.7, 2.75, 2.3, 1.9, 1.25, 1.35};

bool finite(const glm::dvec3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
} // namespace

double blobWobble(double a, double s) {
    const auto finger = [](double x, double power) { return std::pow(std::max(std::cos(x), 0.0), power); };
    return 1.0 + 0.12 * std::sin(3 * a + s * 6.2831) + 0.08 * std::sin(5 * a + s * 17.0)
           + 0.05 * std::sin(7 * a + s * 41.0) + 0.03 * std::sin(11 * a + s * 73.0)
           + 0.018 * std::sin(17 * a + s * 29.0)
           + 0.17 * finger(a - s * 37.7, 28.0)
           + 0.12 * finger(a - s * 53.3 - 2.1, 36.0);
}

FacePaint::FacePaint(const StageData& stage, const StageGeometry& geometry, int atlasSize)
    : stage_(stage), geometry_(geometry), atlasSize_(atlasSize), atlas_(stage.faces.size()), faces_(stage.faces.size()) {
    if (atlasSize != 2048 && atlasSize != 4096) throw std::invalid_argument("paint atlas must be 2048 or 4096 pixels");
    double ppm = 30.0;
    bool packed = false;
    for (int attempt = 0; attempt < 30; ++attempt, ppm *= 0.92) {
        if (tryPack(ppm)) { packed = true; ppm_ = ppm; break; }
    }
    if (!packed) throw std::runtime_error("paintable faces do not fit paint atlas");

    int total = 0;
    for (int id = 0; id < static_cast<int>(stage.faces.size()); ++id) {
        const auto& face = stage.faces[id];
        if (!face.paintable) continue;
        auto& grid = faces_[id];
        grid.columns = std::max(1, static_cast<int>(std::floor(face.width / kCell + 0.5)));
        grid.rows = std::max(1, static_cast<int>(std::floor(face.height / kCell + 0.5)));
        grid.cellWidth = face.width / grid.columns;
        grid.cellHeight = face.height / grid.rows;
        grid.offset = total;
        total += grid.columns * grid.rows;
    }
    grid_.resize(total);
    dead_.resize(total);
    turfCell_.resize(total);
    for (int id = 0; id < static_cast<int>(stage.faces.size()); ++id) {
        const auto& face = stage.faces[id];
        const auto& g = faces_[id];
        if (g.offset < 0) continue;
        for (int j = 0; j < g.rows; ++j) for (int i = 0; i < g.columns; ++i) {
            glm::dvec3 p = face.origin;
            p += face.u * ((i + 0.5) * g.cellWidth);
            p += face.v * ((j + 0.5) * g.cellHeight);
            p += face.normal * 0.06;
            const int k = g.offset + j * g.columns + i;
            if (geometry.pointInside(p, 0.0, face.block)) dead_[k] = 1;
            else if (face.turf) { turfCell_[k] = 1; ++turfTotal_; turfArea_ += g.cellWidth * g.cellHeight; }
        }
    }
}

bool FacePaint::tryPack(double ppm) {
    std::vector<int> ids;
    ids.reserve(stage_.faces.size());
    for (int i = 0; i < static_cast<int>(stage_.faces.size()); ++i)
        if (stage_.faces[i].paintable) ids.push_back(i);
    std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) {
        return std::ceil(stage_.faces[a].height * ppm) > std::ceil(stage_.faces[b].height * ppm);
    });
    int x = 0, y = 0, rowH = 0;
    for (int id : ids) {
        const auto& face = stage_.faces[id];
        const int w = static_cast<int>(std::ceil(face.width * ppm)) + 16;
        const int h = static_cast<int>(std::ceil(face.height * ppm)) + 16;
        if (w > atlasSize_) return false;
        if (x + w > atlasSize_) { x = 0; y += rowH; rowH = 0; }
        if (y + h > atlasSize_) return false;
        atlas_[id] = {x, y, w, h, 8, ppm};
        x += w;
        rowH = std::max(rowH, h);
    }
    usedHeight_ = y + rowH;
    return true;
}

void FacePaint::clear() {
    if (std::any_of(grid_.begin(), grid_.end(), [](std::uint8_t v) { return v != 0; })) ++version_;
    std::fill(grid_.begin(), grid_.end(), 0);
    counts_ = {0, 0};
    visualSplats_.clear();
    visualFloods_.clear();
    visualClear_ = true;
}

bool FacePaint::setCells(const std::vector<int>& cells, int team) {
    if (team < -1 || team > 1) throw std::invalid_argument("invalid paint team");
    for (const int cell : cells)
        if (cell < 0 || cell >= static_cast<int>(grid_.size())) throw std::out_of_range("paint cell index");
    const std::uint8_t value = static_cast<std::uint8_t>(team + 1);
    bool changed = false;
    for (const int cell : cells) {
        auto& current = grid_[static_cast<std::size_t>(cell)];
        if (current == value) continue;
        if (turfCell_[static_cast<std::size_t>(cell)]) {
            if (current) --counts_[current - 1];
            if (value) ++counts_[value - 1];
        }
        current = value;
        changed = true;
    }
    if (changed) ++version_;
    return changed;
}

double FacePaint::splat(const glm::dvec3& center, double radius, int team, double seed, const SplatOptions& options) {
    if (!finite(center) || !std::isfinite(radius) || radius < 0 || !std::isfinite(seed) || (team != 0 && team != 1)
        || !std::isfinite(options.stretchAmt) || options.stretchAmt < 0
        || (options.hasStretch && !finite(options.stretch)))
        throw std::invalid_argument("invalid paint splat");
    if (radius == 0) return 0;
    if (options.kind == SplatKind::Roll && !options.hasStretch)
        throw std::invalid_argument("roller splat requires stretch direction");
    const int kindIndex = static_cast<int>(options.kind);
    if (kindIndex < 0 || kindIndex >= 8) throw std::invalid_argument("invalid paint kind");
    auto visualKind = options.kind;
    if (options.cosmetic && visualKind == SplatKind::Shot) visualKind = SplatKind::Speck;
    visualSplats_.push_back({center, options.stretch, radius, seed, options.stretchAmt, team,
                             visualKind, options.hasStretch, options.cosmetic});
    if (options.cosmetic) return 0;
    const double sAmt = options.hasStretch && options.kind != SplatKind::Roll ? options.stretchAmt : 0.0;
    const double extReach = radius * std::max(3.2, kReach[kindIndex] + 1.4 * sAmt + 0.3);
    double claimed = 0;
    for (int id = 0; id < static_cast<int>(stage_.faces.size()); ++id) {
        const auto& face = stage_.faces[id];
        if (!face.paintable) continue;
        const auto& block = stage_.blocks[face.block];
        // Reference's block AABB reject, computed from the three oriented half axes.
        const glm::dvec3 e = glm::abs(block.axes[0]) * block.half.x
            + glm::abs(block.axes[1]) * block.half.y + glm::abs(block.axes[2]) * block.half.z;
        if (glm::any(glm::lessThan(center, block.center - e - glm::dvec3(extReach)))
            || glm::any(glm::greaterThan(center, block.center + e + glm::dvec3(extReach)))) continue;
        const glm::dvec3 rel = center - face.origin;
        const double dn = glm::dot(rel, face.normal);
        if (dn > radius || dn < -0.12) continue;
        const double lu = glm::dot(rel, face.u), lv = glm::dot(rel, face.v);
        const double rr = std::sqrt(std::max(0.0, radius * radius - dn * dn));
        const double ext = rr * (kReach[kindIndex] + 1.4 * sAmt);
        if (lu < -ext || lu > face.width + ext || lv < -ext - (face.wall ? rr * 3.9 : 0.0)
            || lv > face.height + ext) continue;
        double sdu = 0, sdv = 0, sa = 0;
        if (options.hasStretch) {
            sdu = glm::dot(options.stretch, face.u);
            sdv = glm::dot(options.stretch, face.v);
            const double len = std::hypot(sdu, sdv);
            if (len > 0.2) { sdu /= len; sdv /= len; sa = sAmt * len; }
            else if (options.kind == SplatKind::Roll) { sdu = 1; sdv = 0; }
            else { sdu = sdv = 0; }
        }
        claimed += splatFace(id, lu, lv, rr, team, seed, sdu, sdv, sa, options.kind);
    }
    return claimed;
}

std::vector<VisualSplat> FacePaint::takeVisualSplats() {
    std::vector<VisualSplat> result;
    result.swap(visualSplats_);
    return result;
}

void FacePaint::requestVisualFlood(VisualFlood flood) {
    if (flood.regionId < 0 || flood.team < -1 || flood.team > 1 || !finite(flood.center) ||
        !std::isfinite(flood.y0) || !std::isfinite(flood.y1) || flood.y1 < flood.y0 ||
        !std::isfinite(flood.reach) || flood.reach < 0 || !std::isfinite(flood.start) ||
        !std::isfinite(flood.duration) || flood.duration < 0)
        throw std::invalid_argument("invalid visual paint flood");
    for (const auto& poly : flood.polygons) {
        if (poly.size() < 3) throw std::invalid_argument("invalid visual paint flood polygon");
        for (const auto& point : poly)
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) throw std::invalid_argument("invalid visual paint flood polygon");
    }
    const auto it = std::find_if(visualFloods_.begin(), visualFloods_.end(), [&](const auto& item) { return item.regionId == flood.regionId; });
    if (it == visualFloods_.end()) visualFloods_.push_back(std::move(flood));
    else *it = std::move(flood);
}

std::vector<VisualFlood> FacePaint::takeVisualFloods() {
    std::vector<VisualFlood> result;
    result.swap(visualFloods_);
    return result;
}

bool FacePaint::takeVisualClear() {
    const bool result = visualClear_;
    visualClear_ = false;
    return result;
}

double FacePaint::splatFace(int id, double lu, double lv, double r, int team, double seed,
                            double sdu, double sdv, double sa, SplatKind kind) {
    if (r <= 0.02) return 0;
    const auto& face = stage_.faces[id];
    const auto& g = faces_[id];
    const bool roll = kind == SplatKind::Roll;
    const double ext = roll ? r * (std::hypot(kBandL, kBandW) + kBandR + 0.05) : r * (1 + sa) * kWobMax;
    const int i0 = std::max(0, static_cast<int>(std::floor((lu - ext) / g.cellWidth)));
    const int i1 = std::min(g.columns - 1, static_cast<int>(std::floor((lu + ext) / g.cellWidth)));
    const int j0 = std::max(0, static_cast<int>(std::floor((lv - ext) / g.cellHeight)));
    const int j1 = std::min(g.rows - 1, static_cast<int>(std::floor((lv + ext) / g.cellHeight)));
    if (i1 < i0 || j1 < j0) return 0;
    double claimed = 0;
    const double cellArea = g.cellWidth * g.cellHeight;
    const int val = team + 1;
    for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) {
        double px = (i + 0.5) * g.cellWidth - lu, py = (j + 0.5) * g.cellHeight - lv;
        if (roll) {
            const double qa = std::abs(px * sdu + py * sdv) - r * kBandL;
            const double qb = std::abs(-px * sdv + py * sdu) - r * kBandW;
            const double sd = std::hypot(std::max(qa, 0.0), std::max(qb, 0.0))
                + std::min(std::max(qa, qb), 0.0) - r * kBandR;
            if (sd > -0.03 * r) continue;
        } else {
            if (sa > 0) {
                const double along = px * sdu + py * sdv;
                const double qx = px - along * sdu, qy = py - along * sdv;
                const double stretch = along > 0 ? 1 + sa : 1 + 0.25 * sa;
                px = qx + sdu * (along / stretch);
                py = qy + sdv * (along / stretch);
            }
            const double d = std::hypot(px, py);
            if (d > r * kWobMax || d / (r * blobWobble(std::atan2(py, px), seed)) > 0.97) continue;
        }
        const int k = g.offset + j * g.columns + i;
        const int prev = grid_[k];
        if (prev == val) continue;
        grid_[k] = static_cast<std::uint8_t>(val);
        claimed += cellArea;
        if (face.turf && !dead_[k]) {
            if (prev) --counts_[prev - 1];
            ++counts_[team];
        }
    }
    if (claimed > 0) ++version_;
    return claimed;
}

int FacePaint::sample(int faceId, double u, double v) const {
    if (faceId < 0 || faceId >= static_cast<int>(faces_.size()) || !std::isfinite(u) || !std::isfinite(v)) return 0;
    const auto& g = faces_[faceId];
    if (g.offset < 0) return 0;
    const int i = std::clamp(static_cast<int>(std::floor(u / g.cellWidth)), 0, g.columns - 1);
    const int j = std::clamp(static_cast<int>(std::floor(v / g.cellHeight)), 0, g.rows - 1);
    return grid_[g.offset + j * g.columns + i];
}

std::array<double, 2> FacePaint::coverage() const {
    if (!turfTotal_) return {0, 0};
    return {static_cast<double>(counts_[0]) / turfTotal_, static_cast<double>(counts_[1]) / turfTotal_};
}

} // namespace restoration
