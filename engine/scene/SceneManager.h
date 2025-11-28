#pragma once
#include "Scene.h"

class SceneManager {
public:
    static SceneManager& Instance();

    // 切換場景的核心函式
    // 用法: SceneManager::Instance().SwitchTo(new LobbyScene());
    // 注意: Manager 會接管並負責 delete 舊的場景
    void SwitchTo(Scene* newScene);

    // Forwarding
    void Update(float dt);
    void Render();
    void DrawUI();
    void HandlePacket(const ReceivedPacket& pkt);

    Scene* GetCurrentScene() { return m_CurrentScene; }

private:
    Scene* m_CurrentScene = nullptr;

    SceneManager() {}
    ~SceneManager();

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
};