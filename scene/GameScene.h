#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "../engine/scene/Scene.h"
#include "../engine/GameObject.h"
#include "../engine/core/Input.h"
#include "../gameplay/GameWorld.h"
#include "../components/Camera.h"
#include "../components/HUD.h"
#include "../components/Scoreboard.h"

class GameScene : public Scene {
public:
    GameWorld* world = nullptr;
    GameObject* cameraObj = nullptr;
    GameObject* uiObj = nullptr;
    HUD* hud = nullptr;
    Scoreboard* scoreboard = nullptr;
    Shader* shader = nullptr;
    static Camera* CurrentCamera;

    GameScene() {}

    virtual ~GameScene() {
        OnExit();
    }

    // --- 1. 進入場景 (初始化) ---
    void OnEnter() override {
        std::cout << "[Scene] Enter GameScene" << std::endl;

        // A. 載入 Shader
        shader = new Shader("assets/shaders/default.vert", "assets/shaders/default.frag");

        // B. 建立相機
        cameraObj = new GameObject("MainCamera");
        auto camComp = cameraObj->AddComponent<Camera>();
        CurrentCamera = camComp;

        // C. 建立 UI 容器
        uiObj = new GameObject("UI");
        float w = 1280.0f;
        float h = 720.0f;
        hud = uiObj->AddComponent<HUD>(w, h);

        // D. 初始化遊戲世界
        world = new GameWorld();
        // 這裡傳入 cameraObj 和 hud
        world->Init(cameraObj, hud, nullptr);

        // E. 建立 Scoreboard (因為依賴 splatMap，必須在 world->Init 之後)
        scoreboard = uiObj->AddComponent<Scoreboard>(w, h, world->splatMap);
        world->scoreboardRef = scoreboard; // 綁定回去

        // F. 設定輸入模式 (FPS 模式：隱藏滑鼠)
        glfwSetInputMode(glfwGetCurrentContext(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    // --- 2. 離開場景 (清理) ---
    void OnExit() override {
        std::cout << "[Scene] Exit GameScene" << std::endl;

        CurrentCamera = nullptr;

        glfwSetInputMode(glfwGetCurrentContext(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);

        GLFWwindow* currentWindow = glfwGetCurrentContext();
        if (currentWindow != nullptr) {
            glfwSetInputMode(currentWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        // 釋放記憶體 (順序很重要：先清邏輯，再清資源)
        if (world) {
            world->CleanUp();
            delete world;
            world = nullptr;
        }

        if (uiObj) { delete uiObj; uiObj = nullptr; }
        if (cameraObj) { delete cameraObj; cameraObj = nullptr; }
        if (shader) { delete shader; shader = nullptr; }
    }

    // --- 3. 遊戲迴圈 Update ---
    void Update(float dt) override {
        if (!world) return;
        world->Update(dt);
        if (hud) hud->Update(dt);
        if (scoreboard) scoreboard->Update(dt);
        if (CurrentCamera) CurrentCamera->Update(dt);

        // 相機跟隨邏輯
        if (world->localPlayer) {
            glm::vec3 targetPos = world->localPlayer->transform->position;
            float camDist = 5.0f;
            float camHeight = 2.5f;
            if (cameraObj) {
                glm::vec3 camDir = cameraObj->transform->GetForward();
                cameraObj->transform->position = targetPos - (camDir * camDist) + glm::vec3(0, camHeight, 0);
            }
        }
    }

    // --- 4. 遊戲迴圈 Render ---
    void Render() override {
        if (!world || !shader || !CurrentCamera) return;

        glViewport(0, 0, 1280, 720);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader->Bind();
        shader->SetMat4("view", CurrentCamera->GetViewMatrix());
        shader->SetMat4("projection", CurrentCamera->GetProjectionMatrix());
        shader->SetVec3("viewPos", cameraObj->transform->position);

        // 畫 3D 世界
        world->Render(*shader);

        // 畫 UI (HUD & Scoreboard)
        if (hud) hud->Draw(*shader);
        if (scoreboard) scoreboard->Draw(*shader);
    }

    // --- 5. ImGui UI (遊戲中通常只有簡單的 Debug UI 或 ESC 選單) ---
    void DrawUI() override {
        // 如果想按 ESC 跳出選單，可以在這裡寫 ImGui
        // 目前先留空
    }

    // --- 6. 網路封包處理 ---
    void OnPacket(const ReceivedPacket& pkt) override {
        if (world) {
            world->HandlePacket(pkt);
        }
    }
};

Camera* GameScene::CurrentCamera = nullptr;