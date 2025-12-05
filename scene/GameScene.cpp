#include "GameScene.h"
#include "LoginScene.h"

Camera* GameScene::CurrentCamera = nullptr;

void GameScene::OnEnter() {
    isExited = false;
    std::cout << "[Scene] Enter GameScene" << std::endl;

    // load shader
    shader = std::make_unique<Shader>("assets/shaders/default.vert", "assets/shaders/default.frag");

    // camera
    cameraObj = std::make_unique<GameObject>("MainCamera");
    auto camComp = cameraObj->AddComponent<Camera>();
    CurrentCamera = camComp;

    // create UI
    uiObj = std::make_unique<GameObject>("UI");
    hud = uiObj->AddComponent<HUD>(1280, 720);

    // game world init
    world = std::make_unique<GameWorld>();
    world->Init(cameraObj.get(), hud, nullptr);

    // create scoreboard 
    scoreboard = uiObj->AddComponent<Scoreboard>(1280, 720, world->splatMap.get());
    world->scoreboardRef = scoreboard;

    glfwSetInputMode(glfwGetCurrentContext(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    AudioManager::Instance().PlayBGM("assets/game.mp3", 0.1f);
}

void GameScene::OnExit() {
    if (isExited) return;
    isExited = true;

    std::cout << "[Scene] Exit GameScene" << std::endl;

    CurrentCamera = nullptr;

    glfwSetInputMode(glfwGetCurrentContext(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    // 離開遊戲場景時，斷開網路連線是一個好習慣
    // 這樣回到 LoginScene 才能重新連線或當 Host
    if (NetworkManager::Instance().IsConnected()) {
        NetworkManager::Instance().Disconnect();
    }

    if (world) world->CleanUp();
    world.reset();
}

// game update
void GameScene::Update(float dt) {
    if (!world) return;
    world->Update(dt);
    if (hud) hud->Update(dt);
    if (scoreboard) scoreboard->Update(dt);
    if (CurrentCamera) CurrentCamera->Update(dt);

    if (world->state == WorldState::FINISHED && world->finishTimer <= 0.0f) {
        std::cout << "Return to Lobby..." << std::endl;
        SceneManager::Instance().SwitchTo(std::make_unique<LoginScene>());
        return;
    }

	// camera follow player
    if (world->state == WorldState::PLAYING) {
        if (world->localPlayer && cameraObj) {
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
    else if (world->state == WorldState::FINISHED) {
        // Top-Down view
        if (cameraObj) {
            // 目標位置：地圖中心高空 (例如 80米高)
            glm::vec3 targetPos = glm::vec3(0, 80.0f, 0);
            glm::vec3 currentPos = cameraObj->transform->position;

            // 平滑移動 (Lerp) 讓鏡頭慢慢拉上去
            cameraObj->transform->position = glm::mix(currentPos, targetPos, 5.0f * dt);
            cameraObj->transform->LookAt(glm::vec3(0, 0, 0));
        }
    }
}

// Render loop
void GameScene::Render() {
    if (!world || !shader || !CurrentCamera) return;

    glViewport(0, 0, 1280, 720);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader->Bind();
    shader->SetMat4("view", CurrentCamera->GetViewMatrix());
    shader->SetMat4("projection", CurrentCamera->GetProjectionMatrix());
    shader->SetVec3("viewPos", cameraObj->transform->position);

    // draw world
    world->Render(*shader);

    // draw UI
    if (hud) hud->Draw(*shader);
    if (scoreboard) scoreboard->Draw(*shader);
}

// ImGui UI
void GameScene::DrawUI() {
    if (!world || !hud) return;

    if (world->state == WorldState::PLAYING) {
        if (world->localPlayer) {
            float hpPercent = 1.0f;
            auto hpComp = world->localPlayer->GetComponent<Health>();
            if (hpComp) hpPercent = hpComp->currentHP / hpComp->maxHP;

            float spPercent = 0.0f;
            if (world->localPlayer->MAX_SPECIAL > 0)
                spPercent = world->localPlayer->currentCharge / world->localPlayer->MAX_SPECIAL;

            hud->DrawOverlay(hpPercent, spPercent);
        }
    }
    else if (world->state == WorldState::FINISHED) {
        float animTime = 5.0f - world->finishTimer;

        int myTeam = 1;
        if (world->localPlayer) myTeam = world->localPlayer->teamID;

        hud->DrawResultScreen(
            world->finalScoreTeam1,
            world->finalScoreTeam2,
            myTeam,
            animTime
        );
    }
}

void GameScene::OnPacket(const ReceivedPacket& pkt) {
    if (world) {
        world->HandlePacket(pkt);
    }
}