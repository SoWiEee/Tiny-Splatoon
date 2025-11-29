#pragma once
#include "Scene.h"
#include <memory>

class SceneManager {
public:
    static SceneManager& Instance();

    void SwitchTo(std::unique_ptr<Scene> newScene);

    // Forwarding
    void Update(float dt);
    void Render();
    void DrawUI();
    void HandlePacket(const ReceivedPacket& pkt);

    Scene* GetCurrentScene() { return m_CurrentScene.get(); }

private:
    std::unique_ptr<Scene> m_CurrentScene = nullptr;

    SceneManager() {}
    ~SceneManager();

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
};