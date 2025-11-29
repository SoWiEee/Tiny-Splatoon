#include "SceneManager.h"
#include <iostream>

SceneManager& SceneManager::Instance() {
    static SceneManager instance;
    return instance;
}

SceneManager::~SceneManager() = default;

void SceneManager::SwitchTo(std::unique_ptr<Scene> newScene) {
    if (m_CurrentScene) {
        m_CurrentScene->OnExit();
    }
    m_CurrentScene = std::move(newScene);

    if (m_CurrentScene) {
        m_CurrentScene->OnEnter();
    }
}

void SceneManager::Update(float dt) {
    if (m_CurrentScene) {
        m_CurrentScene->Update(dt);
    }
}

void SceneManager::Render() {
    if (m_CurrentScene) {
        m_CurrentScene->Render();
    }
}

void SceneManager::DrawUI() {
    if (m_CurrentScene) {
        m_CurrentScene->DrawUI();
    }
}

void SceneManager::HandlePacket(const ReceivedPacket& pkt) {
    if (m_CurrentScene) {
        m_CurrentScene->OnPacket(pkt);
    }
}