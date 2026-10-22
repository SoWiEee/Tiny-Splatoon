#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <miniaudio.h>

namespace restoration {

// Shared, partitioned stereo convolver for live native sends. The node is
// continuously processed so its tail is independent of the lifetime of any
// source voice connected to it.
class NativeReverb {
public:
    static constexpr std::size_t kSendCount = 4;

    NativeReverb();
    ~NativeReverb();
    NativeReverb(const NativeReverb&) = delete;
    NativeReverb& operator=(const NativeReverb&) = delete;

    ma_result Initialize(ma_node_graph* graph, ma_node* returnNode, ma_uint32 sampleRate);
    void Shutdown();
    void Reset();
    void SetSendGain(std::size_t send, float gain);
    ma_node* node();
    bool initialized() const;
    ma_uint32 sampleRate() const;

    // Exact float-buffer port of splat-ref/src/audio/music.js makeImpulse()
    // for the source plaza SFX return. Samples are interleaved L/R.
    static std::vector<float> MakePlazaImpulse(ma_uint32 sampleRate);

    // Source music-chain riser return: 2.2 s / 2.4 decay, seed 5, bright .7,
    // dark .08. Offline helper applies the source .25 send and .5 return.
    static std::vector<float> MakeMusicImpulse(ma_uint32 sampleRate);
    static std::vector<float> ConvolveMusicRiser(const std::vector<float>& monoInput, ma_uint32 sampleRate);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A bounded pre-panner splitter for a single mono source. Output 0 preserves
// the normal dry spatial path; output 1 feeds the shared non-spatial return.
class NativeReverbSend {
public:
    NativeReverbSend();
    ~NativeReverbSend();
    NativeReverbSend(const NativeReverbSend&) = delete;
    NativeReverbSend& operator=(const NativeReverbSend&) = delete;

    ma_result Initialize(ma_node_graph* graph, ma_node* wetNode, std::size_t wetInput,
        ma_node* dryReturn, ma_spatializer_listener* listener, ma_uint32 channelsOut,
        ma_mono_expansion_mode monoExpansion, ma_uint32 sampleRate, ma_bool32 spatialized, ma_vec3f position);
    ma_node* inputNode();
    void SetPosition(ma_vec3f position);
    void SetSpatialized(ma_bool32 spatialized);
    void Shutdown();
    bool initialized() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace restoration
