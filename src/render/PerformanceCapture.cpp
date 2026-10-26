#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#ifdef _MSC_VER
#include <intrin.h>
#pragma comment(lib, "Psapi.lib")
#endif
#endif
#include "PerformanceCapture.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <limits>
#include <stdexcept>
#include <thread>

namespace restoration {
namespace {

std::string GlString(GLenum name) {
    const auto* value = glGetString(name);
    if (!value) throw std::runtime_error("Cannot query active OpenGL device");
    return reinterpret_cast<const char*>(value);
}

std::string CpuModel() {
#ifdef _MSC_VER
    int registers[4]{};
    __cpuid(registers, 0x80000000);
    if (static_cast<unsigned>(registers[0]) < 0x80000004u) return "unavailable";
    char brand[49]{};
    for (int leaf = 0; leaf < 3; ++leaf) {
        __cpuid(registers, 0x80000002 + leaf);
        std::memcpy(brand + leaf * 16, registers, 16);
    }
    return brand;
#else
    return "unavailable";
#endif
}

std::optional<std::uint64_t> WorkingSetBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS memory{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
        return static_cast<std::uint64_t>(memory.WorkingSetSize);
#endif
    return std::nullopt;
}

bool HasExtension(const char* wanted) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const auto* extension = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
        if (extension && std::strcmp(reinterpret_cast<const char*>(extension), wanted) == 0) return true;
    }
    return false;
}

std::optional<int> AvailableGpuMemoryKiB(bool supported) {
    if (!supported) return std::nullopt;
    GLint value = 0;
    glGetIntegerv(0x9049, &value); // GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
    return value > 0 ? std::optional<int>(value) : std::nullopt;
}

std::string JsonString(const std::string& input) {
    std::string result = "\"";
    const char hex[] = "0123456789abcdef";
    for (unsigned char c : input) {
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 0x20) {
            result += "\\u00";
            result += hex[c >> 4];
            result += hex[c & 15];
        } else result += static_cast<char>(c);
    }
    return result + '"';
}

double Mean(const std::vector<double>& values) {
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double P95(std::vector<double> values) {
    const auto index = static_cast<std::size_t>(std::ceil(values.size() * 0.95)) - 1;
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}

template<class T> void JsonOptional(std::ostream& out, const std::optional<T>& value) {
    if (value) out << *value;
    else out << "null";
}

} // namespace

PerformanceCapture::PerformanceCapture(std::filesystem::path jsonPath, std::string stage, std::string mode, std::uint32_t seed,
                                       int width, int height, int warmupFrames, int measuredFrames, bool playerView,GraphicsSettings graphics)
    : jsonPath_(std::move(jsonPath)), stage_(std::move(stage)), mode_(std::move(mode)), seed_(seed), width_(width), height_(height),
      warmupFrames_(warmupFrames), measuredFrames_(measuredFrames), logicalCpus_(std::thread::hardware_concurrency()),playerView_(playerView),graphics_(graphics) {
    if (jsonPath_.empty() || width != 1920 || height != 1080 || warmupFrames < 0 || measuredFrames < 1 ||
        measuredFrames > std::numeric_limits<GLsizei>::max() / 3 || warmupFrames > std::numeric_limits<int>::max() - measuredFrames)
        throw std::invalid_argument("Benchmark requires a report path, 1920x1080, and positive sample count");
    if (mode_ != "turf" && mode_ != "boss")
        throw std::invalid_argument("Benchmark requires Turf or Boss mode");
    vendor_ = GlString(GL_VENDOR);
    renderer_ = GlString(GL_RENDERER);
    glVersion_ = GlString(GL_VERSION);
    if (renderer_.find("GTX 1650") == std::string::npos)
        throw std::runtime_error("GTX 1650 acceptance capture selected a different GPU: " + renderer_);
    cpuModel_ = CpuModel();
    nvxMemory_ = HasExtension("GL_NVX_gpu_memory_info");
    frames_.reserve(static_cast<std::size_t>(measuredFrames_));
    // Frame elapsed queries, followed by one shadow timestamp pair per measured frame.
    queries_.resize(static_cast<std::size_t>(measuredFrames_) * 3);
    glGenQueries(static_cast<GLsizei>(queries_.size()), queries_.data());
}

PerformanceCapture::~PerformanceCapture() {
    if (!queries_.empty()) glDeleteQueries(static_cast<GLsizei>(queries_.size()), queries_.data());
}

void PerformanceCapture::beginFrame() {
    if (inFrame_ || frameIndex_ >= warmupFrames_ + measuredFrames_)
        throw std::logic_error("Benchmark frame started out of order");
    // Drain warmup work once so measured throughput includes only measured frames.
    if (frameIndex_ == warmupFrames_) glFinish();
    frameStart_ = std::chrono::steady_clock::now();
    if (frameIndex_ == warmupFrames_) captureStart_ = frameStart_;
    inFrame_ = true;
    shadowRecorded_ = false;
    if (frameIndex_ >= warmupFrames_) {
        glBeginQuery(GL_TIME_ELAPSED, queries_[static_cast<std::size_t>(frameIndex_ - warmupFrames_)]);
        gpuOpen_ = true;
    }
}

void PerformanceCapture::beginShadow() {
    if (!inFrame_ || shadowOpen_ || shadowRecorded_ || !graphics_.shadows)
        throw std::logic_error("Benchmark shadow pass started out of order");
    shadowOpen_ = true;
    if (gpuOpen_) glQueryCounter(queries_[measuredFrames_ + 2 * (frameIndex_ - warmupFrames_)], GL_TIMESTAMP);
}

void PerformanceCapture::endShadow() {
    if (!shadowOpen_) throw std::logic_error("Benchmark shadow pass ended before it started");
    if (gpuOpen_) glQueryCounter(queries_[measuredFrames_ + 2 * (frameIndex_ - warmupFrames_) + 1], GL_TIMESTAMP);
    shadowOpen_ = false;
    shadowRecorded_ = true;
}

void PerformanceCapture::endGpu() {
    if (!inFrame_ || shadowOpen_ || (graphics_.shadows && !shadowRecorded_)) throw std::logic_error("Benchmark GPU frame ended before its passes completed");
    if (gpuOpen_) { glEndQuery(GL_TIME_ELAPSED); gpuOpen_ = false; }
}

bool PerformanceCapture::endFrame(FrameCounts counts) {
    if (!inFrame_ || gpuOpen_) throw std::logic_error("Benchmark frame ended before GPU rendering completed");
    const auto end = std::chrono::steady_clock::now();
    inFrame_ = false;
    if (frameIndex_ >= warmupFrames_) {
        if (counts.actors != 8 || counts.draws <= 8 || counts.particles < 0 || counts.projectiles < 0)
            throw std::runtime_error("Benchmark requires eight live actors and instrumented draw counts");
        Frame frame;
        frame.cpuMs = std::chrono::duration<double, std::milli>(end - frameStart_).count();
        frame.counts = counts;
        frame.workingSetBytes = WorkingSetBytes();
        frame.gpuAvailableKiB = AvailableGpuMemoryKiB(nvxMemory_);
        frames_.push_back(frame);
    }
    ++frameIndex_;
    return frameIndex_ == warmupFrames_ + measuredFrames_;
}

void PerformanceCapture::write() {
    if (written_ || inFrame_ || frames_.size() != static_cast<std::size_t>(measuredFrames_))
        throw std::logic_error("Benchmark report requires one complete capture");
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        GLuint64 nanoseconds = 0;
        glGetQueryObjectui64v(queries_[i], GL_QUERY_RESULT, &nanoseconds);
        frames_[i].gpuMs = static_cast<double>(nanoseconds) / 1000000.0;
        if (graphics_.shadows) {
            GLuint64 begin = 0, end = 0;
            glGetQueryObjectui64v(queries_[measuredFrames_ + 2 * i], GL_QUERY_RESULT, &begin);
            glGetQueryObjectui64v(queries_[measuredFrames_ + 2 * i + 1], GL_QUERY_RESULT, &end);
            if (end < begin) throw std::runtime_error("Invalid shadow GPU timestamps");
            frames_[i].shadowGpuMs = static_cast<double>(end - begin) / 1000000.0;
        }
    }
    const double measuredWallMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - captureStart_).count();
    if (jsonPath_.has_parent_path()) std::filesystem::create_directories(jsonPath_.parent_path());
    auto csvPath = jsonPath_;
    csvPath.replace_extension(".csv");
    std::ofstream csv(csvPath);
    if (!csv) throw std::runtime_error("Cannot write benchmark CSV: " + csvPath.string());
    csv << "frame,cpu_ms,gpu_ms,actors,draws,particles,projectiles,working_set_bytes,gpu_available_kib,shadow_gpu_ms\n";
    csv << std::fixed << std::setprecision(4);
    std::vector<double> cpu, gpu, shadow;
    cpu.reserve(frames_.size()); gpu.reserve(frames_.size());
    std::uint64_t totalDraws = 0, totalParticles = 0;
    std::optional<std::uint64_t> maxMemory;
    std::optional<int> minGpuAvailable;
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        const auto& frame = frames_[i];
        cpu.push_back(frame.cpuMs); gpu.push_back(frame.gpuMs); shadow.push_back(frame.shadowGpuMs);
        totalDraws += frame.counts.draws;
        totalParticles += frame.counts.particles;
        if (frame.workingSetBytes) maxMemory = std::max(maxMemory.value_or(0), *frame.workingSetBytes);
        if (frame.gpuAvailableKiB) minGpuAvailable = std::min(minGpuAvailable.value_or(*frame.gpuAvailableKiB), *frame.gpuAvailableKiB);
        csv << i << ',' << frame.cpuMs << ',' << frame.gpuMs << ',' << frame.counts.actors << ','
            << frame.counts.draws << ',' << frame.counts.particles << ',' << frame.counts.projectiles << ',';
        if (frame.workingSetBytes) csv << *frame.workingSetBytes;
        csv << ',';
        if (frame.gpuAvailableKiB) csv << *frame.gpuAvailableKiB;
        csv << ',' << frame.shadowGpuMs << '\n';
    }
    if (!csv) throw std::runtime_error("Benchmark CSV write failed: " + csvPath.string());
    csv.close();
    std::ofstream json(jsonPath_);
    if (!json) throw std::runtime_error("Cannot write benchmark JSON: " + jsonPath_.string());
    const double cpuMean = Mean(cpu), cpuP95 = P95(cpu), gpuMean = Mean(gpu), gpuP95 = P95(gpu);
    json << std::fixed << std::setprecision(4)
         << "{\n  \"schema\": 2,\n  \"scope\": " << JsonString("native_live_" + mode_ + "_diagnostic")
         << ",\n  \"mode\": " << JsonString(mode_) << ",\n"
         << "  \"qa12_claim\": false,\n"
         << "  \"camera\": " << JsonString(playerView_?"player_follow_with_hud":"distant_overview_without_hud") << ",\n"
         << "  \"graphics\": {\"render_scale\": " << graphics_.renderScale << ", \"shadow_map_size\": " << graphics_.shadowMapSize
         << ", \"shadows\": " << (graphics_.shadows?"true":"false") << ", \"reflections\": " << (graphics_.reflections?"true":"false") << ", \"post_effects\": " << (graphics_.postEffects?"true":"false") << "},\n"
         << "  \"limits\": \"Fixed-seed integration measurement. Whole-frame browser pixel parity, subjective listening and the full worst-case QA12 effects/quality matrix require separate evidence.\",\n"
         << "  \"stage\": " << JsonString(stage_) << ",\n  \"seed\": " << seed_
         << ",\n  \"resolution\": [" << width_ << ", " << height_ << "],\n"
         << "  \"warmup_frames\": " << warmupFrames_ << ",\n  \"measured_frames\": " << measuredFrames_ << ",\n"
         << "  \"vsync\": false,\n  \"sim_step_hz\": 60,\n"
         << "  \"cpu_model\": " << JsonString(cpuModel_) << ",\n  \"cpu_logical_processors\": " << logicalCpus_ << ",\n"
         << "  \"gl_vendor\": " << JsonString(vendor_) << ",\n  \"gl_renderer\": " << JsonString(renderer_)
         << ",\n  \"gl_version_driver\": " << JsonString(glVersion_) << ",\n"
         << "  \"cpu_frame_ms_mean\": " << cpuMean << ",\n  \"cpu_frame_ms_p95\": " << cpuP95
         << ",\n  \"gpu_frame_ms_mean\": " << gpuMean << ",\n  \"gpu_frame_ms_p95\": " << gpuP95
         << ",\n  \"shadow_gpu_ms_mean\": " << Mean(shadow) << ",\n  \"shadow_gpu_ms_p95\": " << P95(shadow)
         << ",\n  \"measured_wall_ms\": " << measuredWallMs
         << ",\n  \"fps_mean\": " << 1000.0 * frames_.size() / measuredWallMs
         << ",\n  \"cpu_submit_fps_mean\": " << 1000.0 / cpuMean
         << ",\n  \"cpu_fps_p95_frame\": " << 1000.0 / cpuP95
         << ",\n  \"draws_per_frame_mean\": " << static_cast<double>(totalDraws) / frames_.size()
         << ",\n  \"particles_per_frame_mean\": " << static_cast<double>(totalParticles) / frames_.size()
         << ",\n  \"working_set_bytes_max\": ";
    JsonOptional(json, maxMemory);
    json << ",\n  \"gpu_available_kib_min\": ";
    JsonOptional(json, minGpuAvailable);
    json << ",\n  \"frames_csv\": " << JsonString(csvPath.filename().string()) << "\n}\n";
    if (!json) throw std::runtime_error("Benchmark JSON write failed: " + jsonPath_.string());
    written_ = true;
}

} // namespace restoration
