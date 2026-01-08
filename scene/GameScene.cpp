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
    hud = uiObj->AddComponent<HUD>(1600.0f, 900.0f);

    // game world init
    world = std::make_unique<GameWorld>();
    world->Init(cameraObj.get(), hud, nullptr);

    // create scoreboard 
    scoreboard = uiObj->AddComponent<Scoreboard>(1600.0f, 900.0f, world->mapFloor.get());
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

            if (playerState == PlayerState::ALIVE ||
                playerState == PlayerState::LAUNCHING ||
                playerState == PlayerState::SHARKING)
            {
                glm::vec3 targetPos = world->localPlayer->transform->position;

                float camDist = 5.0f;
                float camHeight = 2.5f;
                float targetFOV = 60.0f;

                if (playerState == PlayerState::LAUNCHING) {
                    camDist = 8.0f;
                    camHeight = 4.0f;
                }
                else if (playerState == PlayerState::SHARKING) {
                    camDist = 6.5f;
                    camHeight = 3.0f;
                    targetFOV = 75.0f;
                }

                if (cameraObj) {
                    glm::vec3 camDir = cameraObj->transform->GetForward();
                    glm::vec3 desiredPos = targetPos - (camDir * camDist) + glm::vec3(0, camHeight, 0);
                    cameraObj->transform->position = desiredPos;
                }
            }
        }
    }
    else if (world->state == WorldState::FINISHED) {
        // Top-Down view
        if (cameraObj) {
            // 目標位置：地圖中心高空
            glm::vec3 targetPos = glm::vec3(0, 40.0f, 0);
            glm::vec3 currentPos = cameraObj->transform->position;

            cameraObj->transform->position = glm::mix(currentPos, targetPos, 5.0f * dt);
            cameraObj->transform->LookAt(glm::vec3(0, 0, 0));
        }
    }
}

// Render loop
void GameScene::Render() {
    if (!world || !shader || !CurrentCamera) return;

    glViewport(0, 0, 1600, 900);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader->Bind();
    shader->SetMat4("view", CurrentCamera->GetViewMatrix());
    shader->SetMat4("projection", CurrentCamera->GetProjectionMatrix());
    shader->SetVec3("viewPos", cameraObj->transform->position);

    // draw world
    world->Render(*shader, CurrentCamera);

    // draw UI
    if (hud) hud->Draw(*shader);
    if (scoreboard) scoreboard->Draw(*shader);
}

// ImGui UI
void GameScene::DrawUI() {
    if (!world || !hud || !scoreboard) return;

    if (world->state == WorldState::PLAYING) {
        scoreboard->SetShowScoreBar(false);

        if (world->localPlayer) {
            float hpPercent = 1.0f;
            auto hpComp = world->localPlayer->GetComponent<Health>();
            if (hpComp) hpPercent = hpComp->currentHP / hpComp->maxHP;

            float spPercent = 0.0f;
            if (world->localPlayer->MAX_SPECIAL > 0)
                spPercent = world->localPlayer->currentCharge / world->localPlayer->MAX_SPECIAL;

            hud->DrawOverlay(hpPercent, spPercent);
        }

        // 收集所有玩家狀態
        std::vector<UIPlayerStatus> playerStatuses;

        if (world->localPlayer) {
            bool isDead = (world->localPlayer->state == PlayerState::DEAD);
            bool hasSpecial = world->localPlayer->IsSpecialReady();
            playerStatuses.push_back({
                NetworkManager::Instance().GetMyPlayerID(),
                world->localPlayer->teamID,
                isDead,
                true, // isSelf
                hasSpecial
                });
        }

        // AI
        if (world->enemyAI) {
            auto hp = world->enemyAI->GetComponent<Health>();
            bool isDead = (hp && hp->isDead);
            playerStatuses.push_back({
                100, // AI ID
                world->enemyAI->teamID,
                isDead,
                false,
                false // AI 暫無大招
                });
        }

        // 遠端玩家
        for (auto& pair : world->remotePlayers) {
            int id = pair.first;
            RemotePlayer* rp = pair.second.get();
            auto hp = rp->GetComponent<Health>();
            bool isDead = (hp && hp->isDead);

            playerStatuses.push_back({
                id,
                rp->teamID,
                isDead,
                false,
                false
                });
        }

        if (world->localPlayer) {
            bool hasBomb = world->localPlayer->hasBomb;
            hud->DrawBombIndicator(hasBomb);
        }

        scoreboard->DrawPlayerIcons(playerStatuses);
        scoreboard->DrawUITimer(world->gameTimeRemaining);
    }
    else if (world->state == WorldState::FINISHED) {
        scoreboard->SetShowScoreBar(true);
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