#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <glad/glad.h>

namespace restoration {

// Capture one rendered offline match frame at a time, with the GL context current.
class PerformanceCapture {
public:
    struct FrameCounts {
        int actors = 0;
        int draws = 0;
        int particles = 0;
        int projectiles = 0;
    };

    PerformanceCapture(std::filesystem::path jsonPath, std::string stage, std::string mode, std::uint32_t seed,
                       int width, int height, int warmupFrames = 120, int measuredFrames = 600);
    ~PerformanceCapture();
    PerformanceCapture(const PerformanceCapture&) = delete;
    PerformanceCapture& operator=(const PerformanceCapture&) = delete;

    void beginFrame(); // Before poll, fixed-step simulation and rendering.
    void endGpu();     // After the last GL draw, before swapping buffers.
    bool endFrame(FrameCounts counts); // After swap; true when the run is complete.
    void write();

private:
    struct Frame {
        double cpuMs = 0.0;
        double gpuMs = 0.0;
        FrameCounts counts;
        std::optional<std::uint64_t> workingSetBytes;
        std::optional<int> gpuAvailableKiB;
    };

    std::filesystem::path jsonPath_;
    std::string stage_, mode_, vendor_, renderer_, glVersion_, cpuModel_;
    std::uint32_t seed_;
    int width_, height_, warmupFrames_, measuredFrames_, frameIndex_ = 0;
    unsigned logicalCpus_ = 0;
    bool inFrame_ = false, gpuOpen_ = false, written_ = false, nvxMemory_ = false;
    std::chrono::steady_clock::time_point frameStart_, captureStart_;
    std::vector<GLuint> queries_;
    std::vector<Frame> frames_;
};

} // namespace restoration
