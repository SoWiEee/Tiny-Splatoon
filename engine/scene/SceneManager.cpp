#include "SceneManager.h"
#include <iostream>

SceneManager& SceneManager::Instance() {
    static SceneManager instance;
    return instance;
}

SceneManager::~SceneManager() {
    if (m_CurrentScene) {
        m_CurrentScene->OnExit();
        delete m_CurrentScene;
        m_CurrentScene = nullptr;
    }
}

void SceneManager::SwitchTo(Scene* newScene) {
    if (m_CurrentScene) {
        m_CurrentScene->OnExit();
        delete m_CurrentScene;
    }

    m_CurrentScene = newScene;

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