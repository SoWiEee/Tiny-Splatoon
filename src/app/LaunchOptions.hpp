#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace restoration {
struct LaunchOptions {
    std::filesystem::path executable;
    std::string stage = "tidewater", mode = "turf", matchCapture,uiCapture;
    std::string benchmarkReport,graphicsQuality;
    std::string main="shooter",sub,special,difficulty="normal";
    int duration = 0;
    int frames = 0, width = 1600, height = 900;
    int benchmarkWarmup = 120, benchmarkFrames = 600;
    std::uint32_t benchmarkSeed = 1;
    bool match = false, native = false, practice = false;
    bool benchmarkReportSpecified = false;
    bool benchmarkTuningSpecified = false, benchmarkPlayer = false;
    bool widthSpecified = false, heightSpecified = false;
};
}
