#include "../src/audio/NativeReverb.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::size_t kHeaderBytes = 32;
constexpr ma_uint32 kSampleRate = 48000;
constexpr ma_uint32 kInputFrames = 1024;
constexpr ma_uint32 kBlockFrames = 256;
constexpr float kWetTolerance = 3.0e-6f;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture {
    ma_uint32 sampleRate = 0;
    ma_uint32 impulseFrames = 0;
    ma_uint32 channels = 0;
    ma_uint32 inputFrames = 0;
    ma_uint32 outputFrames = 0;
    float normalization = 0;
    std::vector<float> impulse;
    std::vector<float> input;
    std::vector<float> wet;
};

std::uint32_t readU32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    require(offset + 4 <= bytes.size(), "Truncated reverb fixture integer");
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

float readF32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    const std::uint32_t bits = readU32(bytes, offset);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::vector<float> readFloats(const std::vector<unsigned char>& bytes, std::size_t& offset, std::size_t count) {
    require(count <= (bytes.size() - offset) / sizeof(float), "Truncated reverb fixture samples");
    std::vector<float> result(count);
    for (float& value : result) {
        value = readF32(bytes, offset);
        offset += sizeof(float);
    }
    return result;
}

Fixture loadFixture(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    require(file.good(), "Could not open source reverb fixture");
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    require(bytes.size() >= kHeaderBytes, "Source reverb fixture header is missing");
    require(std::string(bytes.begin(), bytes.begin() + 8) == "TSRVBF02", "Unsupported source reverb fixture version");

    Fixture fixture;
    fixture.sampleRate = readU32(bytes, 8);
    fixture.impulseFrames = readU32(bytes, 12);
    fixture.channels = readU32(bytes, 16);
    fixture.inputFrames = readU32(bytes, 20);
    fixture.outputFrames = readU32(bytes, 24);
    fixture.normalization = readF32(bytes, 28);
    require(fixture.sampleRate == kSampleRate && fixture.channels == 2 && fixture.inputFrames == kInputFrames,
        "Unexpected source reverb fixture format");
    require(fixture.impulseFrames > 0 && fixture.outputFrames == fixture.impulseFrames + fixture.inputFrames - 1,
        "Unexpected source reverb fixture dimensions");
    const std::uint64_t sampleCount = static_cast<std::uint64_t>(fixture.impulseFrames) * 2 + fixture.inputFrames +
        static_cast<std::uint64_t>(fixture.outputFrames) * 2;
    require(sampleCount <= (std::numeric_limits<std::size_t>::max() - kHeaderBytes) / sizeof(float) &&
            bytes.size() == kHeaderBytes + static_cast<std::size_t>(sampleCount) * sizeof(float),
        "Unexpected source reverb fixture length");
    require(fixture.normalization > 0 && std::isfinite(fixture.normalization), "Invalid source convolver normalization");

    std::size_t offset = kHeaderBytes;
    fixture.impulse = readFloats(bytes, offset, static_cast<std::size_t>(fixture.impulseFrames) * 2);
    fixture.input = readFloats(bytes, offset, fixture.inputFrames);
    fixture.wet = readFloats(bytes, offset, static_cast<std::size_t>(fixture.outputFrames) * 2);
    require(offset == bytes.size(), "Source reverb fixture has trailing data");
    for (const auto* samples : {&fixture.impulse, &fixture.input, &fixture.wet})
        require(std::all_of(samples->begin(), samples->end(), [](float value) { return std::isfinite(value); }),
            "Source reverb fixture contains a non-finite sample");
    return fixture;
}

float maxError(const std::vector<float>& actual, const std::vector<float>& expected) {
    require(actual.size() == expected.size(), "Reverb output has the wrong number of samples");
    float error = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        require(std::isfinite(actual[i]) && std::isfinite(expected[i]), "Reverb comparison contains a non-finite sample");
        error = std::max(error, std::abs(actual[i] - expected[i]));
    }
    return error;
}

float maxMagnitude(const std::vector<float>& samples) {
    float magnitude = 0;
    for (float sample : samples) {
        require(std::isfinite(sample), "Reverb output contains a non-finite sample");
        magnitude = std::max(magnitude, std::abs(sample));
    }
    return magnitude;
}

struct TestSource {
    ma_node_base base{};
    std::vector<float> samples;
    std::size_t cursor = 0;
    bool initialized = false;

    static void Process(ma_node* node, const float**, ma_uint32* inputFrames, float** output, ma_uint32* outputFrames) {
        auto& source = *reinterpret_cast<TestSource*>(node);
        const ma_uint32 frames = *outputFrames;
        for (ma_uint32 frame = 0; frame < frames; ++frame)
            output[0][frame] = source.cursor < source.samples.size() ? source.samples[source.cursor++] : 0.0f;
        *inputFrames = frames;
        *outputFrames = frames;
    }

    static const ma_node_vtable& Vtable() {
        static const ma_node_vtable vtable{Process, nullptr, 0, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING};
        return vtable;
    }

    ma_result Initialize(ma_node_graph* graph, const std::vector<float>& input) {
        samples = input;
        ma_uint32 outputChannels = 1;
        auto config = ma_node_config_init();
        config.vtable = &Vtable();
        config.pOutputChannels = &outputChannels;
        const ma_result result = ma_node_init(graph, &config, nullptr, reinterpret_cast<ma_node*>(&base));
        initialized = result == MA_SUCCESS;
        return result;
    }

    void Shutdown() {
        if (initialized) {
            ma_node_uninit(reinterpret_cast<ma_node*>(&base), nullptr);
            initialized = false;
        }
    }
};

struct GraphHarness {
    ma_node_graph graph{};
    restoration::NativeReverb reverb;
    restoration::NativeReverbSend send;
    TestSource source;
    ma_spatializer_listener listener{};
    bool initialized = false;
    bool listenerInitialized = false;

    GraphHarness(const std::vector<float>& input, float sendGain, bool useSendSplit = false, ma_uint32 blockFrames = kBlockFrames) {
        auto config = ma_node_graph_config_init(2);
        config.processingSizeInFrames = blockFrames;
        require(ma_node_graph_init(&config, nullptr, &graph) == MA_SUCCESS, "Could not initialize offline reverb graph");
        initialized = true;
        require(reverb.Initialize(&graph, ma_node_graph_get_endpoint(&graph), kSampleRate) == MA_SUCCESS,
            "Could not initialize native reverb node");
        reverb.SetSendGain(0, sendGain);
        ma_node* inputNode = reverb.node();
        if (useSendSplit) {
            auto listenerConfig = ma_spatializer_listener_config_init(2);
            require(ma_spatializer_listener_init(&listenerConfig, nullptr, &listener) == MA_SUCCESS,
                "Could not initialize offline spatializer listener");
            listenerInitialized = true;
            require(send.Initialize(&graph, reverb.node(), 0, ma_node_graph_get_endpoint(&graph), &listener,
                    2, ma_mono_expansion_mode_duplicate, kSampleRate, MA_FALSE, ma_vec3f{0, 0, 0}) == MA_SUCCESS,
                "Could not initialize native dry/wet send splitter");
            inputNode = send.inputNode();
        }
        require(source.Initialize(&graph, input) == MA_SUCCESS, "Could not initialize deterministic graph source");
        require(ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&source.base), 0, inputNode, 0) == MA_SUCCESS,
            "Could not connect deterministic source to native reverb");
    }

    ~GraphHarness() {
        source.Shutdown();
        send.Shutdown();
        reverb.Shutdown();
        if (listenerInitialized) ma_spatializer_listener_uninit(&listener, nullptr);
        if (initialized) ma_node_graph_uninit(&graph, nullptr);
    }

    std::vector<float> Read(ma_uint32 frames) {
        std::vector<float> output(static_cast<std::size_t>(frames) * 2);
        ma_uint64 framesRead = 0;
        require(ma_node_graph_read_pcm_frames(&graph, output.data(), frames, &framesRead) == MA_SUCCESS && framesRead == frames,
            "Offline reverb graph did not return the requested frames");
        return output;
    }
};

ma_uint32 paddedFrames(ma_uint32 frames) {
    return (frames + kBlockFrames - 1) / kBlockFrames * kBlockFrames;
}

void compareImpulse(const std::vector<float>& native, const Fixture& fixture) {
    require(native.size() == fixture.impulse.size(), "Native reverb impulse has the wrong size");
    require(maxError(native, fixture.impulse) <= 1.0e-7f, "Native reverb impulse differs from the source makeImpulse() output");
}
} // namespace

int main(int argc, char** argv) {
    using restoration::NativeReverb;
    try {
        const std::filesystem::path fixtures = argc > 1 ? argv[1] : "tests/fixtures";
        const Fixture plaza = loadFixture(fixtures / "audio-plaza-reverb-source.bin");
        const Fixture music = loadFixture(fixtures / "audio-music-reverb-source.bin");
        compareImpulse(NativeReverb::MakePlazaImpulse(kSampleRate), plaza);
        compareImpulse(NativeReverb::MakeMusicImpulse(kSampleRate), music);

        const auto musicWet = NativeReverb::ConvolveMusicRiser(music.input, kSampleRate);
        std::vector<float> musicExpected = music.wet;
        for (float& sample : musicExpected) sample *= 0.25f; // SEND_REV.fx in the source music chain.
        require(maxError(musicWet, musicExpected) <= kWetTolerance,
            "Music riser reverb differs from the source ConvolverNode render");

        const ma_uint32 renderFrames = paddedFrames(plaza.outputFrames);
        {
            GraphHarness zeroSend(plaza.input, 0.0f);
            const auto silence = zeroSend.Read(renderFrames);
            require(maxMagnitude(silence) == 0, "A zero reverb send must produce digital silence");
        }
        {
            GraphHarness live(plaza.input, 1.0f);
            const auto output = live.Read(renderFrames);
            std::vector<float> sourceWet(static_cast<std::size_t>(plaza.outputFrames) * 2);
            std::copy_n(output.begin(), sourceWet.size(), sourceWet.begin());
            require(maxError(sourceWet, plaza.wet) <= kWetTolerance,
                "Live native reverb differs from the source ConvolverNode render or shifts its timing");
            require(maxMagnitude(std::vector<float>(output.begin() + sourceWet.size(), output.end())) <= 1.0e-6f,
                "Live native reverb tail extends beyond the source impulse response");
        }
        {
            GraphHarness deviceQuantum(plaza.input, 1.0f, false, 480);
            const auto started = std::chrono::steady_clock::now();
            const auto output = deviceQuantum.Read(plaza.outputFrames);
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            require(maxError(output, plaza.wet) <= kWetTolerance,
                "A non-power-of-two device quantum must preserve source convolution timing");
            std::cout << "Plaza wet render: " << plaza.outputFrames << " frames in " << elapsed
                << " s (480-frame graph quantum)\n";
        }
        {
            constexpr ma_uint32 readSizes[]{1, 127, 480, 513};
            constexpr ma_uint32 totalFrames = 1121;
            GraphHarness fixed(plaza.input, 1.0f, true);
            const auto fixedOutput = fixed.Read(totalFrames);
            GraphHarness variable(plaza.input, 1.0f, true);
            std::vector<float> variableOutput;
            variableOutput.reserve(static_cast<std::size_t>(totalFrames) * 2);
            for (ma_uint32 frames : readSizes) {
                auto block = variable.Read(frames);
                variableOutput.insert(variableOutput.end(), block.begin(), block.end());
            }
            require(maxError(variableOutput, fixedOutput) <= 1.0e-7f,
                "Native reverb send timing changed with non-quantum graph reads");

            std::vector<float> expected(static_cast<std::size_t>(totalFrames) * 2, 0.0f);
            for (ma_uint32 frame = 0; frame < totalFrames; ++frame) {
                const float dry = frame < plaza.inputFrames ? plaza.input[frame] : 0.0f;
                for (ma_uint32 channel = 0; channel < 2; ++channel)
                    expected[static_cast<std::size_t>(frame) * 2 + channel] = dry + plaza.wet[static_cast<std::size_t>(frame) * 2 + channel];
            }
            require(maxError(fixedOutput, expected) <= kWetTolerance,
                "NativeReverbSend did not preserve the dry signal and source-timed stereo wet return");
            require(std::abs(fixedOutput[static_cast<std::size_t>(1104) * 2] - fixedOutput[static_cast<std::size_t>(1104) * 2 + 1]) > 1.0e-4f,
                "The wet reverb return should retain its stereo image outside the dry spatializer");
        }
        {
            GraphHarness dryOnly(plaza.input, 0.0f, true);
            const auto dry = dryOnly.Read(kInputFrames + kBlockFrames);
            std::vector<float> expected(dry.size(), 0.0f);
            for (ma_uint32 frame = 0; frame < kInputFrames; ++frame) {
                expected[static_cast<std::size_t>(frame) * 2] = plaza.input[frame];
                expected[static_cast<std::size_t>(frame) * 2 + 1] = plaza.input[frame];
            }
            require(maxError(dry, expected) <= 1.0e-7f,
                "A zero wet send must preserve the immediate dry stereo-expanded signal");
        }
        {
            std::vector<float> nonFiniteInput = plaza.input;
            nonFiniteInput[32] = std::numeric_limits<float>::quiet_NaN();
            nonFiniteInput[64] = std::numeric_limits<float>::infinity();
            GraphHarness finite(nonFiniteInput, 1.0f);
            require(maxMagnitude(finite.Read(renderFrames)) > 0, "Finite-input reverb regression produced silence");
        }
        {
            GraphHarness cleared(plaza.input, 1.0f);
            const auto beforeClear = cleared.Read(8 * kBlockFrames);
            require(maxMagnitude(beforeClear) > 0, "Reverb should produce a tail before clear");
            cleared.reverb.Reset();
            require(maxMagnitude(cleared.Read(renderFrames - 8 * kBlockFrames)) == 0,
                "Clear must discard the accumulated reverb tail");
        }

        std::cout << "Native reverb matches source impulse and convolver fixtures at 48 kHz\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "audio reverb test failed: " << error.what() << '\n';
        return 1;
    }
}
