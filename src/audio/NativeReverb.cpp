#include "NativeReverb.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace restoration {
namespace {
constexpr std::uint32_t kPlazaSeed = 3;
constexpr float kSourceReturnGain = 0.6f;
constexpr float kMusicReturnGain = 0.5f;
constexpr float kMusicSendGain = 0.25f;
constexpr float kPlazaNormalization48k = 0.011418401f; // Source OfflineAudioContext, normalize=true.
constexpr float kMusicNormalization48k = 0.011699766f; // Source OfflineAudioContext, normalize=true.
constexpr std::size_t kMinimumPartitionFrames = 64;
constexpr std::size_t kMaximumPartitionFrames = 4096;
constexpr double kPositionSmoothingSeconds = 0.05;

struct Complex {
    float real = 0;
    float imag = 0;
};

Complex Add(Complex a, Complex b) { return {a.real + b.real, a.imag + b.imag}; }
Complex Multiply(Complex a, Complex b) { return {a.real * b.real - a.imag * b.imag, a.real * b.imag + a.imag * b.real}; }

std::size_t NextPowerOfTwo(std::size_t value) {
    std::size_t result = 1;
    while (result < value && result <= std::numeric_limits<std::size_t>::max() / 2) result <<= 1;
    return result;
}

void Transform(Complex* values, std::size_t count, const std::vector<Complex>& roots, bool inverse) {
    for (std::size_t i = 1, j = 0; i < count; ++i) {
        std::size_t bit = count >> 1;
        for (; (j & bit) != 0; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(values[i], values[j]);
    }

    for (std::size_t length = 2; length <= count; length <<= 1) {
        const std::size_t rootStep = count / length;
        for (std::size_t base = 0; base < count; base += length) {
            for (std::size_t j = 0; j < length / 2; ++j) {
                Complex root = roots[j * rootStep];
                if (inverse) root.imag = -root.imag;
                const Complex odd = Multiply(values[base + j + length / 2], root);
                const Complex even = values[base + j];
                values[base + j] = Add(even, odd);
                values[base + j + length / 2] = {even.real - odd.real, even.imag - odd.imag};
            }
        }
    }

    if (inverse) {
        const float scale = 1.0f / static_cast<float>(count);
        for (std::size_t i = 0; i < count; ++i) {
            values[i].real *= scale;
            values[i].imag *= scale;
        }
    }
}

std::uint32_t MulberryNext(std::uint32_t& value) {
    value += 0x6D2B79F5u;
    std::uint32_t t = value;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + ((t ^ (t >> 7)) * (t | 61u));
    return t ^ (t >> 14);
}

double MulberryUnit(std::uint32_t& value) {
    return static_cast<double>(MulberryNext(value)) / 4294967296.0;
}

std::vector<float> MakeImpulse(ma_uint32 sampleRate, double seconds, double decay,
    double preSeconds, std::uint32_t seed, double bright, double dark) {
    if (sampleRate == 0 || !std::isfinite(seconds) || seconds <= 0) return {};
    const auto frames = std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(sampleRate * seconds)));
    const auto pre = static_cast<std::size_t>(std::floor(preSeconds * sampleRate));
    std::vector<float> impulse(frames * 2, 0.0f);
    for (std::size_t channel = 0; channel < 2; ++channel) {
        std::uint32_t randomState = seed + static_cast<std::uint32_t>(channel * 101);
        double lowpass = 0;
        for (std::size_t i = pre; i < frames; ++i) {
            const double x = static_cast<double>(i - pre) / static_cast<double>(frames - pre);
            const double envelope = std::exp(-x * decay * 2.3) * (1.0 - x) *
                std::min(1.0, static_cast<double>(i - pre) / (0.002 * sampleRate));
            const double alpha = dark + (bright - dark) * std::exp(-x * 4.0);
            lowpass += ((MulberryUnit(randomState) * 2.0 - 1.0) - lowpass) * alpha;
            impulse[i * 2 + channel] = static_cast<float>(lowpass * envelope);
        }

        constexpr std::array<std::pair<double, double>, 5> taps{{
            {0.011, 0.5}, {0.019, 0.36}, {0.027, 0.3}, {0.041, 0.22}, {0.058, 0.15}
        }};
        for (const auto& [time, amplitude] : taps) {
            const auto tap = pre + static_cast<std::size_t>(std::floor((time + (channel ? 0.0031 : 0.0)) * sampleRate));
            if (tap < frames - 2) {
                auto& first = impulse[tap * 2 + channel];
                auto& next = impulse[(tap + 1) * 2 + channel];
                first = static_cast<float>(first + amplitude * (channel ? -1.0 : 1.0));
                next = static_cast<float>(next + amplitude * 0.4);
            }
        }
    }
    return impulse;
}

double Energy(const std::vector<float>& samples) {
    double sum = 0;
    for (float sample : samples) sum += static_cast<double>(sample) * sample;
    return sum;
}

// The scalar comes from the source OfflineAudioContext fixture at 48 kHz.
// Other sample rates use measured-IR RMS scaling; fixture equality is checked at 48 kHz.
float SourceNormalization(const std::vector<float>& impulse, ma_uint32 sampleRate, float normalization48k,
    const std::vector<float>& impulse48k) {
    if (sampleRate == 48000) return normalization48k;
    const double sourceFrames = static_cast<double>(impulse48k.size() / 2);
    const double targetFrames = static_cast<double>(impulse.size() / 2);
    const double sourceEnergy = Energy(impulse48k);
    const double targetEnergy = Energy(impulse);
    if (!(sourceFrames > 0) || !(targetFrames > 0) || !(sourceEnergy > 0) || !(targetEnergy > 0)) return 0;
    return static_cast<float>(normalization48k * std::sqrt((sourceEnergy / sourceFrames) / (targetEnergy / targetFrames)));
}

struct Partitions {
    std::size_t blockFrames = 0;
    std::size_t transformSize = 0;
    std::size_t partitionCount = 0;
    std::vector<Complex> roots;
    std::array<std::vector<Complex>, 2> impulseSpectra;
    std::vector<Complex> inputHistory;
    std::vector<Complex> transformScratch;
    std::array<std::vector<Complex>, 2> accumulation;
    std::array<std::vector<float>, 2> overlap;
    std::vector<float> inputBlock;
    std::size_t writePartition = 0;

    bool Initialize(const std::vector<float>& impulse, std::size_t requestedBlock, float scale) {
        if (impulse.size() < 2 || (impulse.size() & 1u) != 0 || requestedBlock == 0) return false;
        blockFrames = requestedBlock;
        transformSize = NextPowerOfTwo(blockFrames * 2);
        if (transformSize < blockFrames * 2 || transformSize < 2) return false;
        partitionCount = (impulse.size() / 2 + blockFrames - 1) / blockFrames;
        roots.resize(transformSize / 2);
        for (std::size_t k = 0; k < roots.size(); ++k) {
            const double angle = -6.28318530717958647692 * static_cast<double>(k) / transformSize;
            roots[k] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
        }
        for (auto& spectra : impulseSpectra) spectra.assign(partitionCount * transformSize, {});
        inputHistory.assign(partitionCount * transformSize, {});
        transformScratch.assign(transformSize, {});
        for (auto& values : accumulation) values.assign(transformSize, {});
        for (auto& values : overlap) values.assign(blockFrames, 0.0f);
        inputBlock.assign(blockFrames, 0.0f);

        for (std::size_t partition = 0; partition < partitionCount; ++partition) {
            for (std::size_t channel = 0; channel < 2; ++channel) {
                std::fill(transformScratch.begin(), transformScratch.end(), Complex{});
                const auto begin = partition * blockFrames;
                for (std::size_t i = 0; i < blockFrames && begin + i < impulse.size() / 2; ++i)
                    transformScratch[i].real = impulse[(begin + i) * 2 + channel] * scale;
                Transform(transformScratch.data(), transformSize, roots, false);
                auto& destination = impulseSpectra[channel];
                std::copy(transformScratch.begin(), transformScratch.end(), destination.begin() + partition * transformSize);
            }
        }
        writePartition = 0;
        return true;
    }

    void Reset() {
        std::fill(inputHistory.begin(), inputHistory.end(), Complex{});
        for (auto& values : overlap) std::fill(values.begin(), values.end(), 0.0f);
        writePartition = 0;
    }

    void Process(const float* input, float* output, std::size_t frames, float sendGain) {
        if (frames != blockFrames) {
            std::fill(output, output + frames * 2, 0.0f);
            return;
        }
        const std::size_t currentOffset = writePartition * transformSize;
        std::fill(transformScratch.begin(), transformScratch.end(), Complex{});
        for (std::size_t i = 0; i < blockFrames; ++i) {
            const float value = input && std::isfinite(input[i]) ? input[i] * sendGain : 0.0f;
            transformScratch[i].real = value;
        }
        Transform(transformScratch.data(), transformSize, roots, false);
        std::copy(transformScratch.begin(), transformScratch.end(), inputHistory.begin() + currentOffset);

        for (std::size_t channel = 0; channel < 2; ++channel) {
            auto& sum = accumulation[channel];
            std::fill(sum.begin(), sum.end(), Complex{});
            for (std::size_t partition = 0; partition < partitionCount; ++partition) {
                const std::size_t inputPartition = (writePartition + partitionCount - partition) % partitionCount;
                const std::size_t xOffset = inputPartition * transformSize;
                const std::size_t hOffset = partition * transformSize;
                const auto& h = impulseSpectra[channel];
                for (std::size_t bin = 0; bin <= transformSize / 2; ++bin) {
                    const Complex product = Multiply(inputHistory[xOffset + bin], h[hOffset + bin]);
                    sum[bin].real += product.real;
                    sum[bin].imag += product.imag;
                }
            }
            for (std::size_t bin = 1; bin < transformSize / 2; ++bin)
                sum[transformSize - bin] = {sum[bin].real, -sum[bin].imag};
            Transform(sum.data(), transformSize, roots, true);
            for (std::size_t i = 0; i < blockFrames; ++i) {
                const float value = sum[i].real + overlap[channel][i];
                output[i * 2 + channel] = std::isfinite(value) ? value : 0.0f;
                overlap[channel][i] = sum[i + blockFrames].real;
            }
        }
        writePartition = (writePartition + 1) % partitionCount;
    }
};

struct DrySpatializer {
    ma_node_base base{};
    ma_spatializer_listener* listener = nullptr;
    ma_spatializer spatializer{};
    std::atomic<ma_bool32> enabled{MA_FALSE};
    std::atomic<float> targetX{0}, targetY{0}, targetZ{0};
    float currentX = 0, currentY = 0, currentZ = 0;
    float smoothingFrames = 1;
    ma_mono_expansion_mode monoExpansion = ma_mono_expansion_mode_duplicate;
    bool spatializerInitialized = false;
};

void ProcessDry(ma_node* node, const float** input, ma_uint32* inputFrames, float** output, ma_uint32* outputFrames) {
    auto& dry = *reinterpret_cast<DrySpatializer*>(node);
    const ma_uint32 frames = std::min(*inputFrames, *outputFrames);
    if (!input || !input[0]) {
        std::fill(output[0], output[0] + frames * 2, 0.0f);
    } else if (dry.enabled.load(std::memory_order_relaxed) == MA_TRUE) {
        const float smoothing = std::min(1.0f, static_cast<float>(frames) / dry.smoothingFrames);
        const float targetX = dry.targetX.load(std::memory_order_relaxed);
        const float targetY = dry.targetY.load(std::memory_order_relaxed);
        const float targetZ = dry.targetZ.load(std::memory_order_relaxed);
        dry.currentX += (targetX - dry.currentX) * smoothing;
        dry.currentY += (targetY - dry.currentY) * smoothing;
        dry.currentZ += (targetZ - dry.currentZ) * smoothing;
        ma_spatializer_set_position(&dry.spatializer, dry.currentX, dry.currentY, dry.currentZ);
        ma_spatializer_process_pcm_frames(&dry.spatializer, dry.listener, output[0], input[0], frames);
    } else {
        // This node is fixed to a stereo output. Match miniaudio's mono
        // expansion behavior for the configured mode without using its
        // private channel-map helper.
        const float weight = dry.monoExpansion == ma_mono_expansion_mode_average ? 0.5f : 1.0f;
        for (ma_uint32 frame = 0; frame < frames; ++frame) {
            const float sample = input[0][frame] * weight;
            output[0][frame * 2] = sample;
            output[0][frame * 2 + 1] = sample;
        }
    }
    *inputFrames = frames;
    *outputFrames = frames;
}

const ma_node_vtable kDryVtable{ProcessDry, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING};

} // namespace

struct NativeReverb::Impl {
    ma_node_base base{};
    bool nodeInitialized = false;
    ma_uint32 sampleRate = 0;
    std::array<std::atomic<float>, kSendCount> sendGain{};
    Partitions partitions;
    std::atomic<bool> resetPending{false};
    std::vector<float> monoInput;
    std::vector<float> stereoOutput;

    static void Process(ma_node* node, const float** input, ma_uint32* inputFrames, float** output, ma_uint32* outputFrames) {
        auto& self = *reinterpret_cast<Impl*>(node);
        const auto frames = *outputFrames;
        if (self.resetPending.exchange(false, std::memory_order_acq_rel)) self.partitions.Reset();

        const auto blockFrames = self.partitions.blockFrames;
        if (!blockFrames || frames != blockFrames) {
            std::fill(output[0], output[0] + frames * 2, 0.0f);
            *inputFrames = frames;
            return;
        }
        std::array<float, kSendCount> levels{};
        for (std::size_t send = 0; send < kSendCount; ++send)
            levels[send] = self.sendGain[send].load(std::memory_order_relaxed);
        for (std::size_t frame = 0; frame < blockFrames; ++frame) {
            double sum = 0;
            for (std::size_t send = 0; send < kSendCount; ++send) {
                const float sample = input && input[send] && frame < *inputFrames ? input[send][frame] : 0.0f;
                if (std::isfinite(sample) && std::isfinite(levels[send])) sum += sample * levels[send];
            }
            self.monoInput[frame] = static_cast<float>(sum);
        }
        self.partitions.Process(self.monoInput.data(), output[0], blockFrames, 1.0f);
        *inputFrames = frames;
    }

    static const ma_node_vtable& Vtable() {
        static const ma_node_vtable table{Process, nullptr, MA_NODE_BUS_COUNT_UNKNOWN, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING};
        return table;
    }
};

NativeReverb::NativeReverb() : impl_(std::make_unique<Impl>()) {}
NativeReverb::~NativeReverb() { Shutdown(); }

std::vector<float> NativeReverb::MakePlazaImpulse(ma_uint32 sampleRate) {
    return MakeImpulse(sampleRate, 1.5, 3.4, 0.012, kPlazaSeed, 0.85, 0.12);
}

std::vector<float> NativeReverb::MakeMusicImpulse(ma_uint32 sampleRate) {
    return MakeImpulse(sampleRate, 2.2, 2.4, 0.01, 5, 0.7, 0.08);
}

std::vector<float> NativeReverb::ConvolveMusicRiser(const std::vector<float>& monoInput, ma_uint32 sampleRate) {
    if (monoInput.empty() || sampleRate == 0 || kMusicNormalization48k <= 0) return {};
    const auto impulse = MakeMusicImpulse(sampleRate);
    const auto impulse48k = sampleRate == 48000 ? impulse : MakeMusicImpulse(48000);
    const float normalization = SourceNormalization(impulse, sampleRate, kMusicNormalization48k, impulse48k);
    const std::size_t impulseFrames = impulse.size() / 2;
    if (impulseFrames == 0 || monoInput.size() > std::numeric_limits<std::size_t>::max() - impulseFrames + 1) return {};
    const std::size_t outputFrames = monoInput.size() + impulseFrames - 1;
    if (outputFrames > std::numeric_limits<std::size_t>::max() / 2) return {};

    // Offline block size is deliberately large: this render runs before playback,
    // so it has no callback latency and substantially fewer FFT products.
    constexpr std::size_t blockFrames = 2048;
    Partitions convolver;
    if (!convolver.Initialize(impulse, blockFrames, normalization * kMusicReturnGain)) return {};
    std::vector<float> inputBlock(blockFrames, 0.0f), outputBlock(blockFrames * 2, 0.0f);
    std::vector<float> result(outputFrames * 2, 0.0f);
    const std::size_t blockCount = (outputFrames + blockFrames - 1) / blockFrames;
    for (std::size_t block = 0; block < blockCount; ++block) {
        const std::size_t frameOffset = block * blockFrames;
        for (std::size_t frame = 0; frame < blockFrames; ++frame) {
            const std::size_t sourceFrame = frameOffset + frame;
            const float sample = sourceFrame < monoInput.size() ? monoInput[sourceFrame] : 0.0f;
            inputBlock[frame] = std::isfinite(sample) ? sample * kMusicSendGain : 0.0f;
        }
        convolver.Process(inputBlock.data(), outputBlock.data(), blockFrames, 1.0f);
        const std::size_t framesToCopy = std::min(blockFrames, outputFrames - frameOffset);
        std::copy_n(outputBlock.data(), framesToCopy * 2, result.data() + frameOffset * 2);
    }
    return result;
}

ma_result NativeReverb::Initialize(ma_node_graph* graph, ma_node* returnNode, ma_uint32 sampleRate) {
    Shutdown();
    if (!graph || !returnNode || sampleRate == 0) return MA_INVALID_ARGS;

    auto rawImpulse = MakePlazaImpulse(sampleRate);
    const auto plazaAt48k = sampleRate == 48000 ? rawImpulse : MakePlazaImpulse(48000);
    const float normalization = SourceNormalization(rawImpulse, sampleRate, kPlazaNormalization48k, plazaAt48k);
    if (!(normalization > 0) || !std::isfinite(normalization)) return MA_INVALID_ARGS;

    const std::size_t blockFrames = graph->processingSizeInFrames;
    if (blockFrames < kMinimumPartitionFrames || blockFrames > kMaximumPartitionFrames) return MA_INVALID_ARGS;
    if (!impl_->partitions.Initialize(rawImpulse, blockFrames, normalization * kSourceReturnGain)) return MA_OUT_OF_MEMORY;
    impl_->monoInput.assign(blockFrames, 0.0f);
    impl_->sampleRate = sampleRate;

    std::array<ma_uint32, kSendCount> inputChannels{};
    inputChannels.fill(1);
    const ma_uint32 outputChannels = 2;
    auto config = ma_node_config_init();
    config.vtable = &Impl::Vtable();
    config.inputBusCount = static_cast<ma_uint32>(kSendCount);
    config.pInputChannels = inputChannels.data();
    config.pOutputChannels = &outputChannels;
    const ma_result initResult = ma_node_init(graph, &config, nullptr, reinterpret_cast<ma_node*>(&impl_->base));
    if (initResult != MA_SUCCESS) { Shutdown(); return initResult; }
    impl_->nodeInitialized = true;

    const ma_result attachResult = ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&impl_->base), 0, returnNode, 0);
    if (attachResult != MA_SUCCESS) { Shutdown(); return attachResult; }
    for (auto& gain : impl_->sendGain) gain.store(0.0f, std::memory_order_relaxed);
    return MA_SUCCESS;
}

void NativeReverb::Shutdown() {
    if (!impl_) return;
    if (impl_->nodeInitialized) {
        impl_->nodeInitialized = false;
        ma_node_uninit(reinterpret_cast<ma_node*>(&impl_->base), nullptr);
    }
    impl_->sampleRate = 0;
    impl_->resetPending.store(false, std::memory_order_release);
    impl_->partitions = {};
    impl_->monoInput.clear();
    impl_->stereoOutput.clear();
}

void NativeReverb::Reset() {
    if (impl_->sampleRate != 0) impl_->resetPending.store(true, std::memory_order_release);
}

void NativeReverb::SetSendGain(std::size_t send, float gain) {
    if (send >= kSendCount) return;
    impl_->sendGain[send].store(std::isfinite(gain) ? std::clamp(gain, 0.0f, 1.0f) : 0.0f, std::memory_order_release);
}

ma_node* NativeReverb::node() { return reinterpret_cast<ma_node*>(&impl_->base); }
bool NativeReverb::initialized() const { return impl_->sampleRate != 0 && impl_->nodeInitialized; }
ma_uint32 NativeReverb::sampleRate() const { return impl_->sampleRate; }

struct NativeReverbSend::Impl {
    ma_splitter_node splitter{};
    bool splitterInitialized = false;
    DrySpatializer dry{};
    bool dryInitialized = false;

    void Shutdown() {
        if (splitterInitialized) {
            ma_node_uninit(reinterpret_cast<ma_node*>(&splitter.base), nullptr);
            splitterInitialized = false;
        }
        if (dryInitialized) {
            ma_node_uninit(reinterpret_cast<ma_node*>(&dry.base), nullptr);
            dry.listener = nullptr;
            dryInitialized = false;
        }
        if (dry.spatializerInitialized) {
            ma_spatializer_uninit(&dry.spatializer, nullptr);
            dry.spatializerInitialized = false;
        }
    }
};

NativeReverbSend::NativeReverbSend() : impl_(std::make_unique<Impl>()) {}
NativeReverbSend::~NativeReverbSend() { Shutdown(); }

ma_result NativeReverbSend::Initialize(ma_node_graph* graph, ma_node* wetNode, std::size_t wetInput,
    ma_node* dryReturn, ma_spatializer_listener* listener, ma_uint32 channelsOut,
    ma_mono_expansion_mode monoExpansion, ma_uint32 sampleRate, ma_bool32 spatialized, ma_vec3f position) {
    Shutdown();
    if (!graph || !wetNode || !dryReturn || !listener || wetInput >= NativeReverb::kSendCount || channelsOut != 2 || sampleRate == 0) return MA_INVALID_ARGS;

    auto splitterConfig = ma_splitter_node_config_init(1);
    splitterConfig.outputBusCount = 2;
    ma_result result = ma_splitter_node_init(graph, &splitterConfig, nullptr, &impl_->splitter);
    if (result != MA_SUCCESS) return result;
    impl_->splitterInitialized = true;

    auto spatializerConfig = ma_spatializer_config_init(1, channelsOut);
    result = ma_spatializer_init(&spatializerConfig, nullptr, &impl_->dry.spatializer);
    if (result != MA_SUCCESS) { Shutdown(); return result; }
    impl_->dry.spatializerInitialized = true;
    impl_->dry.listener = listener;
    impl_->dry.monoExpansion = monoExpansion;
    impl_->dry.smoothingFrames = std::max(1.0f, static_cast<float>(sampleRate * kPositionSmoothingSeconds));
    impl_->dry.enabled.store(spatialized ? MA_TRUE : MA_FALSE, std::memory_order_relaxed);
    impl_->dry.targetX.store(position.x, std::memory_order_relaxed);
    impl_->dry.targetY.store(position.y, std::memory_order_relaxed);
    impl_->dry.targetZ.store(position.z, std::memory_order_relaxed);
    impl_->dry.currentX = position.x;
    impl_->dry.currentY = position.y;
    impl_->dry.currentZ = position.z;

    const ma_uint32 inputChannels = 1, outputChannels = channelsOut;
    auto dryConfig = ma_node_config_init();
    dryConfig.vtable = &kDryVtable;
    dryConfig.pInputChannels = &inputChannels;
    dryConfig.pOutputChannels = &outputChannels;
    result = ma_node_init(graph, &dryConfig, nullptr, reinterpret_cast<ma_node*>(&impl_->dry.base));
    if (result != MA_SUCCESS) { Shutdown(); return result; }
    impl_->dryInitialized = true;
    ma_spatializer_set_position(&impl_->dry.spatializer, position.x, position.y, position.z);

    result = ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&impl_->splitter.base), 0,
        reinterpret_cast<ma_node*>(&impl_->dry.base), 0);
    if (result != MA_SUCCESS) { Shutdown(); return result; }
    result = ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&impl_->splitter.base), 1, wetNode,
        static_cast<ma_uint32>(wetInput));
    if (result != MA_SUCCESS) { Shutdown(); return result; }
    result = ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&impl_->dry.base), 0, dryReturn, 0);
    if (result != MA_SUCCESS) { Shutdown(); return result; }
    return MA_SUCCESS;
}

ma_node* NativeReverbSend::inputNode() { return reinterpret_cast<ma_node*>(&impl_->splitter.base); }

void NativeReverbSend::SetPosition(ma_vec3f position) {
    if (!impl_->dryInitialized) return;
    impl_->dry.targetX.store(position.x, std::memory_order_release);
    impl_->dry.targetY.store(position.y, std::memory_order_release);
    impl_->dry.targetZ.store(position.z, std::memory_order_release);
}

void NativeReverbSend::SetSpatialized(ma_bool32 spatialized) {
    if (impl_->dryInitialized) impl_->dry.enabled.store(spatialized ? MA_TRUE : MA_FALSE, std::memory_order_release);
}

void NativeReverbSend::Shutdown() { if (impl_) impl_->Shutdown(); }
bool NativeReverbSend::initialized() const { return impl_->splitterInitialized && impl_->dryInitialized; }

} // namespace restoration
