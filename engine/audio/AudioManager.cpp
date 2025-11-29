#include "AudioManager.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <iostream>

AudioManager& AudioManager::Instance() {
    static AudioManager instance;
    return instance;
}

bool AudioManager::Initialize() {
    if (ma_engine_init(NULL, &m_Engine) != MA_SUCCESS) return false;
    return true;
}

void AudioManager::Shutdown() {
    StopBGM();
    for (auto& pair : m_SoundBank) {
        for (int i = 0; i < pair.second.POOL_SIZE; i++) {
            ma_sound_uninit(&pair.second.sounds[i]);
        }
        delete[] pair.second.sounds;
    }
    m_SoundBank.clear();
    ma_engine_uninit(&m_Engine);
}

void AudioManager::LoadSound(const std::string& name, const std::string& path) {
    if (m_SoundBank.find(name) != m_SoundBank.end()) return;

    SoundData data;
    data.poolIndex = 0;
    data.sounds = new ma_sound[SoundData::POOL_SIZE];

    for (int i = 0; i < SoundData::POOL_SIZE; i++) {
        // 初始化聲音物件，但不播放
        ma_result res = ma_sound_init_from_file(&m_Engine, path.c_str(), MA_SOUND_FLAG_DECODE, NULL, NULL, &data.sounds[i]);
        if (res != MA_SUCCESS) {
            std::cout << "Failed to load: " << path << std::endl;
        }
    }
    m_SoundBank[name] = data;
    std::cout << "[Audio] Loaded: " << name << std::endl;
}

void AudioManager::PlayOneShot(const std::string& name, float volume) {
    if (m_SoundBank.find(name) == m_SoundBank.end()) return;

    SoundData& data = m_SoundBank[name];

    // 從池中拿出一個沒在播放的，或者硬搶最舊的
    ma_sound* snd = &data.sounds[data.poolIndex];

    // 調整參數
    ma_sound_set_volume(snd, volume);
    float pitch = 0.9f + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / 0.2f));
    ma_sound_set_pitch(snd, pitch);

    // 播放 (如果正在播，會重頭開始)
    ma_sound_start(snd); // start = play

    // 移動索引 (簡單的 Round Robin)
    data.poolIndex = (data.poolIndex + 1) % SoundData::POOL_SIZE;
}

void AudioManager::StopBGM() {
    if (m_CurrentBGM) {
        // 停止並釋放資源
        ma_sound_stop(m_CurrentBGM);
        ma_sound_uninit(m_CurrentBGM);
        delete m_CurrentBGM;
        m_CurrentBGM = nullptr;
    }
}

void AudioManager::PlayBGM(const std::string& path, float volume, bool loop) {
    // 1. 先停止上一首 (如果有)
    StopBGM();

    // 2. 建立新的 BGM 物件
    m_CurrentBGM = new ma_sound;

    // 3. 初始化 (使用 STREAM 模式，不佔用大量記憶體)
    ma_result result = ma_sound_init_from_file(
        &m_Engine,
        path.c_str(),
        MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_ASYNC,
        NULL,
        NULL,
        m_CurrentBGM
    );

    if (result != MA_SUCCESS) {
        std::cerr << "[Audio] Failed to load BGM: " << path << std::endl;
        delete m_CurrentBGM;
        m_CurrentBGM = nullptr;
        return;
    }

    // 4. 設定參數
    ma_sound_set_volume(m_CurrentBGM, volume);
    ma_sound_set_looping(m_CurrentBGM, loop ? MA_TRUE : MA_FALSE);

    // 5. 開始播放
    ma_sound_start(m_CurrentBGM);

    std::cout << "[Audio] Playing BGM: " << path << std::endl;
}