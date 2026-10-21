#pragma once
#include <filesystem>
#include <glm/glm.hpp>
#include <memory>

namespace restoration {
class ZoneControl;
class ZoneRenderer {
public:
  ZoneRenderer(const std::filesystem::path &assets, const std::string &stage);
  ~ZoneRenderer();
  ZoneRenderer(const ZoneRenderer &) = delete;
  ZoneRenderer &operator=(const ZoneRenderer &) = delete;
  int Draw(const ZoneControl &zones, const glm::dmat4 &view,
           const glm::dmat4 &projection, glm::dvec3 eye, double mapView,
           bool finished = false, double visualTime = -1);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace restoration
