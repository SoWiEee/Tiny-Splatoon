#pragma once
#include <memory>
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
    std::unique_ptr<GameWorld> world;
    std::unique_ptr<GameObject> cameraObj;
    std::unique_ptr<GameObject> uiObj;
    HUD* hud = nullptr;
    Scoreboard* scoreboard = nullptr;
    std::unique_ptr<Shader> shader;
    static Camera* CurrentCamera;

    GameScene() {}

    virtual ~GameScene() {
        OnExit();
    }

    // --- 1. 進入場景 (初始化) ---
    void OnEnter() override {
        std::cout << "[Scene] Enter GameScene" << std::endl;

        // A. 載入 Shader
        shader = std::make_unique<Shader>("assets/shaders/default.vert", "assets/shaders/default.frag");

        // B. 建立相機
        cameraObj = std::make_unique<GameObject>("MainCamera");
        auto camComp = cameraObj->AddComponent<Camera>();
        CurrentCamera = camComp;

        // C. 建立 UI 容器
        uiObj = std::make_unique<GameObject>("UI");
        hud = uiObj->AddComponent<HUD>(1280, 720);

        // D. 初始化遊戲世界
        world = std::make_unique<GameWorld>();
        world->Init(cameraObj.get(), hud, nullptr);

        // E. 建立 Scoreboard 
        scoreboard = uiObj->AddComponent<Scoreboard>(1280, 720, world->splatMap.get());
        world->scoreboardRef = scoreboard;

        glfwSetInputMode(glfwGetCurrentContext(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        AudioManager::Instance().PlayBGM("assets/game.mp3", 0.1f);
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

        if (world) world->CleanUp();
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
            auto playerState = world->localPlayer->state;
            if (playerState == PlayerState::ALIVE || playerState == PlayerState::LAUNCHING) {

                glm::vec3 targetPos = world->localPlayer->transform->position;

                // 參數設定
                float camDist = 5.0f;
                float camHeight = 2.5f;

                // 如果正在超級跳躍，相機可以拉遠一點，視野更好
                if (playerState == PlayerState::LAUNCHING) {
                    camDist = 8.0f;
                    camHeight = 4.0f;
                }

                if (cameraObj) {
                    glm::vec3 camDir = cameraObj->transform->GetForward();
                    // 設定位置
                    cameraObj->transform->position = targetPos - (camDir * camDist) + glm::vec3(0, camHeight, 0);
                }
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

        // 畫 UI
        if (hud) hud->Draw(*shader);
        if (scoreboard) scoreboard->Draw(*shader);
    }

    // --- 5. ImGui UI (遊戲中通常只有簡單的 Debug UI 或 ESC 選單) ---
    void DrawUI() override {
        if (hud && world && world->localPlayer) {
            float hpPercent = 1.0f;
            auto hpComp = world->localPlayer->GetComponent<Health>();
            if (hpComp) hpPercent = hpComp->currentHP / hpComp->maxHP;
            // 大招能量
            float spPercent = 0.0f;
            if (world->localPlayer->MAX_SPECIAL > 0) {
                spPercent = world->localPlayer->specialCharge / world->localPlayer->MAX_SPECIAL;
            }

            // 呼叫 HUD 繪製覆蓋層
            hud->DrawOverlay(hpPercent, spPercent);
        }
    }

    // --- 6. 網路封包處理 ---
    void OnPacket(const ReceivedPacket& pkt) override {
        if (world) {
            world->HandlePacket(pkt);
        }
    }
};

Camera* GameScene::CurrentCamera = nullptr;