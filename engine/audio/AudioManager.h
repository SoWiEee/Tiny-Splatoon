#pragma once
#include "miniaudio.h"
#include <string>
#include <map>
#include <vector>

class AudioManager {
public:
    static AudioManager& Instance();
    bool Initialize();
    void Shutdown();

    // 預先載入音效資料 (只讀取檔案一次，節省效能)
    void LoadSound(const std::string& name, const std::string& path);

    // 播放已載入的音效
    void PlayOneShot(const std::string& name, float volume = 1.0f);
    void PlayBGM(const std::string& path, float volume = 0.5f, bool loop = true);
    void StopBGM();

private:
    ma_engine m_Engine;

    // 聲音資源快取
    struct SoundData {
        ma_sound* sounds; // 聲音物件池 (Object Pool)
        int poolIndex;
        static const int POOL_SIZE = 10; // 同一個聲音最多同時重疊 10 個
    };
    std::map<std::string, SoundData> m_SoundBank;
    ma_sound* m_CurrentBGM = nullptr;

    AudioManager() {}
    ~AudioManager() { Shutdown(); }
};