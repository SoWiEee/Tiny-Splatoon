#include "GameWorld.hpp"

void GameWorld::Init(GameObject* mainCamera, HUD* hud, Scoreboard* scoreboard) {
        level = std::make_unique<Level>();
        level->Load();
        mapFloor = std::make_unique<SplatMap>(1024, 1024);
        mapObstacle = std::make_unique<SplatMap>(1024, 1024);
        painter = std::make_unique<SplatPainter>();
        particleSystem = std::make_unique<ParticleSystem>();
        scoreboardRef = scoreboard;
        hudRef = hud;

        visualEntities.clear();

        int myTeam = NetworkManager::Instance().GetMyTeamID();

        // local test
        if (!NetworkManager::Instance().IsConnected()) {
            myTeam = 1;
        }
        // Server 強制設定
        if (NetworkManager::Instance().IsServer()) {
            NetworkManager::Instance().SetMyPlayerID(0);
            NetworkManager::Instance().SetMyTeamID(1);
            myTeam = 1;
        }

        // create player instance
        WeaponType myWeaponType = NetworkManager::Instance().GetMyWeaponType();
        localPlayer = std::make_unique<Player>(glm::vec3(-5, 0, -5), myTeam, mapFloor.get(), mapObstacle.get(), mainCamera, hud, level.get());

        glm::vec3 teamColor = (myTeam == 1) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);

        switch (myWeaponType) {
        case WeaponType::SHOOTER:
            localPlayer->EquipWeapon(std::make_unique<ShooterWeapon>(myTeam, teamColor));
            break;
        case WeaponType::BRUSH:
            localPlayer->EquipWeapon(std::make_unique<BrushWeapon>(myTeam, teamColor));
            break;
        case WeaponType::SLOSHER:
            localPlayer->EquipWeapon(std::make_unique<SlosherWeapon>(myTeam, teamColor));
            break;
        default:
            localPlayer->EquipWeapon(std::make_unique<ShooterWeapon>(myTeam, teamColor));
            break;
        }

        // ==========================================================
        // 4. 預先載入資源 (Pre-load Assets)
        // ==========================================================

        // --- Red Team Resources ---
        meshRedTeam = ModelLoader::LoadOBJ("assets/models/character-b.obj");
        texRedTeam = std::make_shared<Texture>();
        texRedTeam->Load("assets/textures/texture-b.png");

        // --- Green Team Resources ---
        meshGreenTeam = ModelLoader::LoadOBJ("assets/models/character-f.obj");
        texGreenTeam = std::make_shared<Texture>();
        texGreenTeam->Load("assets/textures/texture-f.png");

        // 防呆：如果載入失敗，互相指派，避免空指標 crash
        if (!meshRedTeam) meshRedTeam = MeshFactory::GetCube();
        if (!meshGreenTeam) meshGreenTeam = meshRedTeam;

        // [關鍵修正] 根據我的隊伍，選擇要使用的資源
        std::shared_ptr<Mesh> myMesh = (myTeam == 1) ? meshRedTeam : meshGreenTeam;
        std::shared_ptr<Texture> myTex = (myTeam == 1) ? texRedTeam : texGreenTeam;


        // ==========================================================
        // 5. 建立 [人型態] 視覺 (Human Visual)
        // ==========================================================
        auto humanObj = std::make_unique<GameObject>("Visual_Human");
        humanObj->SetParent(localPlayer.get()); // 綁定到 Player

        if (myMesh) {
            // 使用白色 (1.0f) 才能顯示貼圖原色
            auto mr = humanObj->AddComponent<MeshRenderer>(myMesh, glm::vec3(1.0f));

            if (myTex) {
                mr->SetTexture(myTex);
            }
            else {
                // 如果沒貼圖，就用隊伍顏色頂替
                mr->SetColor(teamColor);
            }

            humanObj->transform->scale = glm::vec3(0.6f);
            humanObj->transform->rotation = glm::vec3(0, 0, 0);
            humanObj->transform->position = glm::vec3(0, 0.0f, 0);
        }
        else {
            humanObj->AddComponent<MeshRenderer>("Cube", teamColor);
            humanObj->transform->scale = glm::vec3(0.5f, 1.0f, 0.5f);
        }

        // ==========================================================
        // 6. 建立 [魷魚型態] 視覺 (Squid Visual)
        // ==========================================================
        auto squidObj = std::make_unique<GameObject>("Visual_Squid");
        squidObj->SetParent(localPlayer.get());
        squidObj->active = false; // 預設隱藏

        // 暫時用扁方塊代替魷魚
        squidObj->AddComponent<MeshRenderer>("Cube", teamColor);
        squidObj->transform->scale = glm::vec3(0.4f, 0.2f, 0.6f); // 扁長型
        squidObj->transform->position = glm::vec3(0, -0.8f, 0);   // 貼地

        // ==========================================================
        // 7. [新增] 建立影子 (Shadow) - 之前移除建構子後要補回來
        // ==========================================================
        auto shadowObj = std::make_unique<GameObject>("Shadow");
        shadowObj->SetParent(localPlayer.get());
        shadowObj->AddComponent<MeshRenderer>("Plane", glm::vec3(0, 0, 0));
        shadowObj->transform->position = glm::vec3(0, 0.05f, 0); // 稍微浮起
        shadowObj->transform->scale = glm::vec3(1.2f, 1.0f, 1.2f);


        // ==========================================================
        // 8. 綁定與註冊
        // ==========================================================

        // 告訴 Player 視覺物件在哪 (用於變身切換)
        localPlayer->SetupVisuals(humanObj.get(), squidObj.get());

        // 加入 World 管理列表 (渲染用)
        visualEntities.push_back(std::move(humanObj));
        visualEntities.push_back(std::move(squidObj));
        visualEntities.push_back(std::move(shadowObj));

        // Create AI (Server Only)
        /*if (NetworkManager::Instance().IsServer()) {
            NetworkManager::Instance().SetMyPlayerID(0);
            localPlayer->teamID = 1;
            localPlayer->weapon->inkColor = glm::vec3(1, 0, 0);
            enemyAI = std::make_unique<Enemy>(glm::vec3(5, 0, 5), 2);
        }
        else {
            enemyAI = nullptr;
        }*/
    }

void GameWorld::CreateProjectile(int ownerID, int teamID, glm::vec3 startPos, glm::vec3 dir, ProjectileType type) {
        float speed = 25.0f;
        float scale = 0.3f;
        glm::vec3 color = (teamID == 1) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);

        // 根據類型調整參數
        if (type == ProjectileType::ROCKET) {
            speed = 35.0f; // 火箭很快
            scale = 0.8f;  // 火箭較大
        }
        else if (type == ProjectileType::BOMB) {
            speed = 10.0f; // 炸彈拋物線
        }

        glm::vec3 velocity = dir * speed;

        // 如果不是火箭也不是炸彈，一般子彈會加一點上拋
        if (type != ProjectileType::ROCKET && type != ProjectileType::BOMB) {
            velocity.y += 2.0f;
        }

        auto proj = std::make_unique<Projectile>(startPos, velocity, color, teamID, scale, ownerID, type);

        projectiles.push_back(std::move(proj));
    }

void GameWorld::SendShootPacket(glm::vec3 pos, glm::vec3 dir, ProjectileType type) {
        if (!NetworkManager::Instance().IsConnected()) return;

        PacketShoot pkt;
        pkt.header.type = PacketType::C2S_SHOOT;
        pkt.playerID = NetworkManager::Instance().GetMyPlayerID();;
        pkt.origin = pos;
        pkt.direction = dir;
        pkt.type = type;

        // 這些參數可以不用傳，讓接收端根據 Type 自己決定，但為了彈性先傳
        pkt.speed = (type == ProjectileType::ROCKET) ? 35.0f : 25.0f;
        pkt.scale = (type == ProjectileType::ROCKET) ? 0.8f : 0.3f;
        pkt.color = localPlayer->weapon->inkColor;

        if (NetworkManager::Instance().IsServer()) {
            // 如果是 Server 自己射的，直接轉發給別人
            pkt.header.type = PacketType::S2C_SHOOT_EVENT;
            NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true);
        }
        else {
            // Client 請求 Server
            NetworkManager::Instance().SendToServer(&pkt, sizeof(pkt), true);
        }
    }

bool GameWorld::CheckCollision(GameObject* bullet, GameObject* target) {
        glm::vec3 posB = bullet->transform->position;
        glm::vec3 posT = target->transform->position;

        // 判定中心點稍微上移 (因為人是站著的)
        glm::vec3 centerT = posT + glm::vec3(0, 1.0f, 0);
        float bulletRadius = bullet->transform->scale.x * 0.5f;

        float dist = glm::distance(posB, centerT);

        // 判定距離 = 人身半徑 (0.5) + 子彈半徑
        return dist < (kEntityHitRadius + bulletRadius);
    }

void GameWorld::Update(float dt) {
        if (state == WorldState::PLAYING) {
            UpdateActiveMatch(dt);
        }
        else if (state == WorldState::FINISHED) {
            UpdateFinishedMatch(dt);
        }
    }

void GameWorld::Render(Shader& shader, Camera* cam) {
        // 設定共用參數
        shader.SetFloat("mapSize", level->mapSize);
        shader.SetInt("useInk", 1);
        shader.SetInt("inkMap", 1); // 對應 GL_TEXTURE1

        // ==========================================
        // 1. [地板層] 綁定 mapFloor -> 畫地板
        // ==========================================
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, mapFloor->textureID);

        // 畫地板
        if (level->floor) {
            level->floor->Draw(shader);
        }

        // ==========================================
        // 2. [障礙物層] 綁定 mapObstacle -> 畫箱子
        // ==========================================
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, mapObstacle->textureID);

        // 畫障礙物
        for (auto obj : level->obstacles) {
            if (obj) obj->Draw(shader);
        }

        // ==========================================
        // 3. [牆壁層] 關閉墨水 -> 畫牆壁
        // ==========================================
        shader.SetInt("useInk", 0);
        for (auto wall : level->walls) {
            wall->Draw(shader);
        }

        // 畫道具
        shader.SetInt("useInk", 0);
        for (auto& item : items) {
            item->Draw(shader);
        }

        shader.SetInt("useInk", 0);
        for (auto& obj : visualEntities) {
            if (obj->active) obj->Draw(shader);
        }

        for (const auto& p : projectiles) {
            if (p) p->Draw(shader);
        }

        if (localPlayer->GetVisualBody()) localPlayer->GetVisualBody()->Draw(shader);
        if (enemyAI && enemyAI->GetVisualBody()) enemyAI->GetVisualBody()->Draw(shader);

        // 4. 畫陰影 (開啟半透明混合)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE); // 關閉深度寫入
        shader.SetFloat("alpha", 0.5f); // 設定半透明

        auto DrawShadow = [&](GameObject* owner) {
            if (!owner) return;

            // 1. 檢查本機玩家
            if (owner == localPlayer.get()) {
                if (localPlayer->isSwimming) return;
            }
            // 2. 檢查遠端玩家 (RemotePlayer?)

            Health* hp = owner->GetComponent<Health>();
            if (hp && hp->isDead) return;

            glm::vec3 shadowPos = owner->transform->position;
            shadowPos.y = 0.02f; // 貼地

            float height = owner->transform->position.y;
            float scale = 1.5f - (height * 0.3f);
            if (scale < 0) scale = 0;

            if (localPlayer && localPlayer->shadow) {
                GameObject* s = localPlayer->shadow;
                s->transform->position = shadowPos;
                s->transform->scale = glm::vec3(scale, 1.0f, scale);
                s->Draw(shader);
            }
            };

        if (particleSystem && cam) {
            particleSystem->Draw(cam->GetViewMatrix(), cam->GetProjectionMatrix());
        }

        DrawShadow(localPlayer.get());
        if (enemyAI) DrawShadow(enemyAI.get());
        for (auto& pair : remotePlayers) DrawShadow(pair.second.get());

        // restore Render State
        shader.SetFloat("alpha", 1.0f);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

void GameWorld::CollectProjectiles(Weapon& weapon) {
        for (const auto& info : weapon.pendingSpawns) {
            // 一般子彈射擊也改用 Helper
            CreateProjectile(NetworkManager::Instance().GetMyPlayerID(), info.team, info.pos, info.dir, ProjectileType::BULLET);
            SendShootPacket(info.pos, info.dir, ProjectileType::BULLET);
        }
        weapon.pendingSpawns.clear();
    }

void GameWorld::CleanUp() {
        remotePlayers.clear();
    }

void GameWorld::HandlePacket(const ReceivedPacket& received) {
        auto& net = NetworkManager::Instance();
        if (net.IsServer()) {
            HandleServerPacket(received, net);
        }
        HandleSharedPacket(received, net);
    }

void GameWorld::UpdateActiveMatch(float dt) {
        gameTimeRemaining -= dt;

        if (localPlayer) {
            localPlayer->UpdateLogic(dt);
            if (localPlayer->weapon) CollectProjectiles(*(localPlayer->weapon));
        }
        if (enemyAI) {
            enemyAI->UpdateLogic(dt);
            if (enemyAI->weapon) CollectProjectiles(*(enemyAI->weapon));
        }

        UpdateVisualEntities(dt);
        HandleLocalAbilityRequests();
        UpdateItems(dt);
        UpdateNetworkSync(dt);
        UpdateRemotePlayers(dt);

        if (particleSystem) particleSystem->Update(dt);
        UpdateProjectiles(dt);

        if (gameTimeRemaining <= 0.0f) {
            EndGame();
        }
    }

void GameWorld::UpdateFinishedMatch(float dt) {
        finishTimer -= dt;
        if (localPlayer) localPlayer->velocity = glm::vec3(0);
    }

void GameWorld::UpdateVisualEntities(float dt) {
        for (auto& obj : visualEntities) {
            if (obj->active) obj->Update(dt);
        }
    }

void GameWorld::HandleLocalAbilityRequests() {
        if (!localPlayer) return;

        if (localPlayer->requestSharkSpray) {
            SpawnSharkBullets();
            localPlayer->requestSharkSpray = false;
        }

        if (localPlayer->requestBombThrow) {
            localPlayer->requestBombThrow = false;

            glm::vec3 camFwd = localPlayer->cameraRef->transform->GetForward();
            glm::vec3 spawnPos = localPlayer->transform->position + glm::vec3(0, 1.5f, 0) + camFwd * 1.0f;

            CreateProjectile(NetworkManager::Instance().GetMyPlayerID(), localPlayer->teamID, spawnPos, camFwd, ProjectileType::BOMB);
            SendShootPacket(spawnPos, camFwd, ProjectileType::BOMB);
        }

        if (localPlayer->requestRocketFire) {
            localPlayer->requestRocketFire = false;

            glm::vec3 camFwd = localPlayer->cameraRef->transform->GetForward();
            glm::vec3 spawnPos = localPlayer->transform->position + glm::vec3(0, 1.5f, 0) + camFwd * 1.5f;

            CreateProjectile(NetworkManager::Instance().GetMyPlayerID(), localPlayer->teamID, spawnPos, camFwd, ProjectileType::ROCKET);
            SendShootPacket(spawnPos, camFwd, ProjectileType::ROCKET);
            AudioManager::Instance().PlayOneShot("rocket_launch", 1.0f);
        }
    }

void GameWorld::UpdateItems(float dt) {
        if (items.size() < 2) {
            itemRespawnTimer += dt;
            if (itemRespawnTimer > kItemRespawnIntervalSeconds) {
                SpawnRandomItem();
                itemRespawnTimer = 0.0f;
            }
        }

        for (auto it = items.begin(); it != items.end(); ) {
            (*it)->Update(dt);

            if (localPlayer && !localPlayer->hasBomb) {
                float dist = glm::distance(localPlayer->transform->position, (*it)->transform->position);
                if (dist < 1.5f) {
                    localPlayer->PickupBomb();
                    AudioManager::Instance().PlayOneShot("pick_bomb", 0.5f);
                    it = items.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

void GameWorld::UpdateRemotePlayers(float dt) {
        for (auto& pair : remotePlayers) {
            pair.second->UpdateInterp(dt);
        }
    }

void GameWorld::UpdateNetworkSync(float dt) {
        auto& net = NetworkManager::Instance();
        if (!net.IsConnected()) return;

        syncTimer += dt;
        if (syncTimer > kPlayerSyncIntervalSeconds) {
            SyncLocalPlayerState(net);
            syncTimer = 0.0f;
        }

        scoreSyncTimer += dt;
        if (scoreSyncTimer > kScoreSyncIntervalSeconds) {
            if (net.IsServer()) {
                BroadcastScoreState();
            }
            scoreSyncTimer = 0.0f;
        }
    }

void GameWorld::BroadcastScoreState() {
        std::pair<float, float> scores = mapFloor->CalculatePercentages();
        finalScoreTeam1 = scores.first;
        finalScoreTeam2 = scores.second;

        PacketGameState scorePkt;
        scorePkt.header.type = PacketType::S2C_GAME_STATE;
        scorePkt.scoreTeam1 = scores.first;
        scorePkt.scoreTeam2 = scores.second;
        scorePkt.timeRemaining = std::max(0.0f, gameTimeRemaining);

        NetworkManager::Instance().Broadcast(&scorePkt, sizeof(scorePkt), false);
    }

void GameWorld::HandleServerPacket(const ReceivedPacket& received, NetworkManager& net) {
        if (received.type == PacketType::C2S_PLAYER_STATE) {
            const auto* inPkt = TryGetPacket<PacketPlayerState>(received);
            if (!inPkt) return;
            PacketPlayerState outPkt = *inPkt;
            outPkt.header.type = PacketType::S2C_WORLD_STATE;
            net.Broadcast(&outPkt, sizeof(outPkt), false);
            HandleWorldState(outPkt);
        }
        else if (received.type == PacketType::C2S_SHOOT) {
            const auto* inPkt = TryGetPacket<PacketShoot>(received);
            if (!inPkt) return;
            PacketShoot outPkt = *inPkt;
            outPkt.header.type = PacketType::S2C_SHOOT_EVENT;
            net.Broadcast(&outPkt, sizeof(outPkt), true);
            if (inPkt->playerID != net.GetMyPlayerID()) {
                SpawnRemoteProjectile(outPkt);
            }
        }
        else if (received.type == PacketType::C2S_SPECIAL_ATTACK) {
            const auto* inPkt = TryGetPacket<PacketSpecialLaser>(received);
            if (!inPkt) return;
            TriggerLaserBeam(inPkt->origin, inPkt->direction, inPkt->teamID, inPkt->playerID);
            PacketSpecialLaser outPkt = *inPkt;
            outPkt.header.type = PacketType::S2C_SPECIAL_ATTACK;
            net.Broadcast(&outPkt, sizeof(outPkt), true, received.fromConnection);
        }
    }

void GameWorld::HandleSharedPacket(const ReceivedPacket& received, NetworkManager& net) {
        if (received.type == PacketType::S2C_WORLD_STATE) {
            if (const auto* worldState = TryGetPacket<PacketPlayerState>(received)) {
                HandleWorldState(*worldState);
            }
        }
        else if (received.type == PacketType::S2C_SHOOT_EVENT) {
            const auto* pkt = TryGetPacket<PacketShoot>(received);
            if (!pkt) return;
            if (pkt->playerID != net.GetMyPlayerID()) {
                SpawnRemoteProjectile(*pkt);
            }
        }
        else if (received.type == PacketType::S2C_GAME_STATE) {
            const auto* pkt = TryGetPacket<PacketGameState>(received);
            if (!pkt) return;
            finalScoreTeam1 = pkt->scoreTeam1;
            finalScoreTeam2 = pkt->scoreTeam2;
            gameTimeRemaining = std::max(0.0f, pkt->timeRemaining);
            if (scoreboardRef) {
                scoreboardRef->SetScores(pkt->scoreTeam1, pkt->scoreTeam2);
            }
            if (pkt->timeRemaining <= 0.0f && state != WorldState::FINISHED) {
                EndGame();
            }
        }
        else if (received.type == PacketType::S2C_JOIN_ACCEPT) {
            const auto* pkt = TryGetPacket<PacketJoinAccept>(received);
            if (!pkt) return;
            net.SetMyPlayerID(pkt->yourPlayerID);
            localPlayer->teamID = pkt->yourTeamID;
            glm::vec3 c = (pkt->yourTeamID == 1) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            localPlayer->weapon->inkColor = c;
            localPlayer->weapon->teamID = pkt->yourTeamID;
            if (localPlayer->GetVisualBody()) {
                localPlayer->GetVisualBody()->GetComponent<MeshRenderer>()->SetColor(c);
            }
        }
        else if (received.type == PacketType::S2C_SPECIAL_ATTACK) {
            const auto* pkt = TryGetPacket<PacketSpecialLaser>(received);
            if (!pkt) return;
            TriggerLaserBeam(pkt->origin, pkt->direction, pkt->teamID, pkt->playerID);
            AudioManager::Instance().PlayOneShot("laser_fire", 1.0f);
        }
        else if (received.type == PacketType::S2C_KILL_EVENT) {
            const auto* pkt = TryGetPacket<PacketKillEvent>(received);
            if (!pkt) return;
            if (hudRef) {
                hudRef->AddKillLog(pkt->killerID, pkt->victimID, pkt->killerTeam, pkt->victimTeam);
            }
            int myID = NetworkManager::Instance().GetMyPlayerID();
            if (pkt->victimID == myID) {
                if (localPlayer) {
                    localPlayer->Die();
                    AudioManager::Instance().PlayOneShot("splat_die", 1.0f);
                }
            }
        }
    }

void GameWorld::SyncLocalPlayerState(NetworkManager& net) {
        PacketPlayerState pkt;
        pkt.header.type = PacketType::C2S_PLAYER_STATE;
        pkt.playerID = net.GetMyPlayerID();
        pkt.position = localPlayer->transform->position;
        pkt.rotationY = localPlayer->transform->rotation.y;
        pkt.isSwimming = localPlayer->isSwimming;
        auto myHP = localPlayer->GetComponent<Health>();
        pkt.isDead = (myHP && myHP->isDead);
        pkt.isSharking = (localPlayer->state == PlayerState::SHARKING);

        if (net.IsServer()) {
            PacketPlayerState worldStatePkt = pkt;
            worldStatePkt.header.type = PacketType::S2C_WORLD_STATE;
            net.Broadcast(&worldStatePkt, sizeof(worldStatePkt), false);
            SyncAiState(net);
        }
        else {
            net.SendToServer(&pkt, sizeof(pkt), false);
        }
    }

void GameWorld::SyncAiState(NetworkManager& net) {
        if (!enemyAI) return;

        PacketPlayerState aiPkt;
        aiPkt.header.type = PacketType::S2C_WORLD_STATE;
        aiPkt.playerID = 100;
        aiPkt.position = enemyAI->transform->position;
        aiPkt.rotationY = enemyAI->transform->rotation.y;
        aiPkt.isSwimming = false;
        auto aiHP = enemyAI->GetComponent<Health>();
        aiPkt.isDead = (aiHP && aiHP->isDead);
        net.Broadcast(&aiPkt, sizeof(aiPkt), false);
    }

void GameWorld::SpawnRemoteProjectile(const PacketShoot& pkt) {
        int team = (pkt.playerID % 2 == 0) ? 1 : 2;
        if (remotePlayers.find(pkt.playerID) != remotePlayers.end()) {
            team = remotePlayers[pkt.playerID]->teamID;
        }

        CreateProjectile(pkt.playerID, team, pkt.origin, pkt.direction, pkt.type);
    }

void GameWorld::HandleWorldState(const PacketPlayerState& pkt) {
        int id = pkt.playerID;
        if (id == NetworkManager::Instance().GetMyPlayerID()) return;
        if (id == -1) return;
        auto it = remotePlayers.find(id);
        int guessedTeam = (id == 100) ? 2 : ((id % 2 == 0) ? 1 : 2);
        if (it == remotePlayers.end()) {
            CreateRemotePlayer(id, guessedTeam, pkt.position);
        }
        else {
            it->second->teamID = guessedTeam;
            it->second->SetTargetState(pkt.position, pkt.rotationY, pkt.isSwimming, pkt.isDead, pkt.isSharking);
        }
    }

void GameWorld::UpdateProjectiles(float dt) {
        float mapSize = level->mapSize;
        float inkMultiplier = 50.0f;
        frameCollisionTargets.clear();
        frameCollisionTargets.reserve(2 + remotePlayers.size());
        if (localPlayer) frameCollisionTargets.push_back(localPlayer.get());
        if (enemyAI) frameCollisionTargets.push_back(enemyAI.get());
        for (auto& pair : remotePlayers) frameCollisionTargets.push_back(pair.second.get());

        for (auto it = projectiles.begin(); it != projectiles.end(); ) {
            Projectile* p = it->get();
            p->UpdatePhysics(dt);

            bool hitEntity = false; // 是否撞到實體
            bool hitObstacle = false; // 是否撞到障礙物

            for (Entity* target : frameCollisionTargets) {
                if (!target) continue;
                int targetTeam = target->teamID;
                if (targetTeam == p->ownerTeam) continue; // 不打隊友

                if (CheckCollision(p, target)) {

                    // --- [修改重點] 針對火箭的特殊處理 ---
                    if (p->pType == ProjectileType::ROCKET) {
                        p->isDead = true; // 標記為死亡，讓後面的 Rocket Logic 觸發爆炸
                        hitEntity = true; // 標記撞到了，但"不要"在這裡 erase，也不要在這裡扣血
                        break;            // 跳出碰撞迴圈
                    }
                    // ----------------------------------

                    // 普通子彈邏輯 (維持原樣)
                    Health* hp = target->GetComponent<Health>();
                    if (hp) {
                        bool wasAlive = !hp->isDead;
                        hp->TakeDamage(10.0f); // 普通子彈傷害

                        // 特效
                        particleSystem->Emit(p->transform->position, p->inkColor, 15, 8.0f);

                        // 擊殺判定 (Server)
                        if (wasAlive && hp->isDead) {
                            if (NetworkManager::Instance().IsServer()) {
                                int victimID = -99;
                                if (target == localPlayer.get()) victimID = NetworkManager::Instance().GetMyPlayerID();
                                else if (target == enemyAI.get()) victimID = 100;
                                else {
                                    for (auto& rp : remotePlayers) {
                                        if (rp.second.get() == target) {
                                            victimID = rp.first;
                                            break;
                                        }
                                    }
                                }
                                // Send Kill Packet
                                PacketKillEvent pkt;
                                pkt.header.type = PacketType::S2C_KILL_EVENT;
                                pkt.killerID = p->ownerID;
                                pkt.victimID = victimID;
                                pkt.killerTeam = p->ownerTeam;
                                pkt.victimTeam = hp->teamID;
                                NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true);
                                if (hudRef) hudRef->AddKillLog(p->ownerID, victimID, p->ownerTeam, hp->teamID);
                            }
                            // 本地死亡處理
                            if (target == localPlayer.get()) {
                                localPlayer->Die();
                                SpawnDeathSplat(localPlayer->transform->position, p->inkColor);
                            }
                            else if (target == enemyAI.get()) {
                                SpawnDeathSplat(enemyAI->transform->position, p->inkColor);
                                hp->Reset();
                                enemyAI->transform->position = hp->spawnPoint;
                            }
                        }
                    }

                    // 擊中回饋
                    if (localPlayer && p->ownerTeam == localPlayer->teamID) {
                        AudioManager::Instance().PlayOneShot("hit", 0.8f);
                        if (hudRef) hudRef->ShowHitMarker();
                    }

                    hitEntity = true; // 普通子彈撞到實體
                    break;
                }
            }

            // 2. 檢查障礙物碰撞 (Box) - 如果是火箭撞牆也要爆炸
            if (!hitEntity) { // 如果還沒撞到人再檢查牆
                for (auto& box : level->colliders) {
                    glm::vec3 pos = p->transform->position;
                    if (pos.x >= box.min.x && pos.x <= box.max.x &&
                        pos.y >= box.min.y && pos.y <= box.max.y &&
                        pos.z >= box.min.z && pos.z <= box.max.z) {

                        if (p->pType == ProjectileType::ROCKET) {
                            p->isDead = true; // 火箭撞牆 -> 觸發爆炸
                        }
                        else {
                            // 普通子彈撞牆 -> 塗牆並消失
                            auto result = SplatPhysics::WorldToUV(pos, glm::vec3(0), mapSize, mapSize);
                            if (result.hit) {
                                float uvSize = (p->transform->scale.x * inkMultiplier) / mapSize;
                                float rot = (float)(rand() % 360);
                                painter->Paint(mapObstacle.get(), result.uv, uvSize, p->inkColor, rot, p->ownerTeam);
                                particleSystem->Emit(pos, p->inkColor, 5, 5.0f);
                            }
                        }
                        hitObstacle = true;
                        break;
                    }
                }
            }

            // 處理刪除邏輯
            // 如果是普通子彈撞到東西 -> 刪除
            // 如果是火箭撞到東西 -> 不要刪除 (hitEntity/hitObstacle 為 true，但我們把 p->isDead 設為 true 了，讓它進入下方的 Rocket Logic)
            if (p->pType != ProjectileType::ROCKET && (hitEntity || hitObstacle)) {
                it = projectiles.erase(it);
                continue;
            }

            // --- Rocket Logic ---
            if (p->pType == ProjectileType::ROCKET) {
                if (p->isDead) {
                    p->hasExploded = true;
                    glm::vec3 hitPos = p->transform->position;

                    // 1. 爆炸特效
                    if (particleSystem) particleSystem->Emit(hitPos, p->inkColor, 80, 40.0f);
                    AudioManager::Instance().PlayOneShot("explode", 1.0f);

                    // 2. 塗地 (大範圍)
                    // 塗一個大圓
                    float rRadius = 4.0f;
                    float uvSize = (rRadius * 2.0f) / mapSize;
                    auto res = SplatPhysics::WorldToUV(hitPos, glm::vec3(0), mapSize, mapSize);
                    if (res.hit) {
                        float h = level->GetHeightAt(hitPos.x, hitPos.z);
                        SplatMap* target = (h > 0.5f) ? mapObstacle.get() : mapFloor.get();
                        painter->Paint(target, res.uv, uvSize, p->inkColor, 0, p->ownerTeam);
                    }

                    // 3. 傷害判定 (Server Only)
                    if (NetworkManager::Instance().IsServer()) {
                        float blastRadius = 8.0f;
                        // 檢查本機
                        if (localPlayer && localPlayer->teamID != p->ownerTeam) {
                            if (glm::distance(localPlayer->transform->position, hitPos) < blastRadius) {
                                // 傷害
                                auto hp = localPlayer->GetComponent<Health>();
                                if (hp && !hp->isDead) {
                                    hp->TakeDamage(50.0f); // 直接秒殺
                                    if (hp->isDead) {
                                        // Send Kill Packet
                                        PacketKillEvent kPkt;
                                        kPkt.header.type = PacketType::S2C_KILL_EVENT;
                                        kPkt.killerID = p->ownerID;
                                        kPkt.victimID = NetworkManager::Instance().GetMyPlayerID();
                                        kPkt.killerTeam = p->ownerTeam;
                                        kPkt.victimTeam = localPlayer->teamID;
                                        NetworkManager::Instance().Broadcast(&kPkt, sizeof(kPkt), true);
                                    }
                                }
                            }
                        }
                        for (auto& pair : remotePlayers) {
                            RemotePlayer* rp = pair.second.get();
                            float dist = glm::distance(p->transform->position, rp->transform->position);

                            if (dist < blastRadius) {
                                // 判斷敵我 (或自殺)
                                if (rp->teamID != p->ownerTeam || pair.first == p->ownerID) {
                                    ProcessKillEvent(p->ownerID, rp, p->ownerTeam);
                                }
                            }
                        }
                    }

                    it = projectiles.erase(it);
                    continue;
                }
            }
            else if (p->pType == ProjectileType::BOMB) {
                // 1. 倒數警示音效 (剩 1.0 秒時)
                if (!p->warningPlayed && p->fuseTimer <= 1.0f && p->fuseTimer > 0.0f) {
                    p->warningPlayed = true;

                    // 簡單的 3D 音效模擬：只有離炸彈夠近的人才聽得到
                    if (localPlayer) {
                        float dist = glm::distance(p->transform->position, localPlayer->transform->position);
                        if (dist < 15.0f) { // 15米內聽得到
                            AudioManager::Instance().PlayOneShot("bomb_beep", 1.0f);
                        }
                    }
                }

                if (p->hasExploded) {
                    glm::vec3 bombPos = p->transform->position;
                    float mapSize = level->mapSize;

                    // =========================================================
                    // 1. [集束炸彈邏輯] 模擬 60+ 發子彈同時落地
                    // =========================================================

                    // 設定參數
                    float maxRadius = 15.0f; // 最大爆炸半徑 (公尺)
                    int layers = 5;          // 分 5 層擴散 (同心圓)

                    // Loop 1: 每一層 (從中心往外)
                    for (int l = 0; l <= layers; l++) {
                        float currentRadius = (maxRadius / layers) * l;

                        // 越外圈，墨水數量越多
                        int countInLayer = (l == 0) ? 1 : (l * 8);

                        // Loop 2: 每一滴墨水
                        for (int i = 0; i < countInLayer; i++) {
                            // 計算角度
                            float angle = (360.0f / countInLayer) * i;
                            // 加入一點隨機偏移，讓形狀不要太圓，比較自然
                            float randOffset = ((rand() % 100) / 100.0f) * 2.0f;
                            float finalRadius = currentRadius + randOffset;

                            // 計算位置
                            float rad = glm::radians(angle);
                            glm::vec3 offset(cos(rad) * finalRadius, 0, sin(rad) * finalRadius);
                            glm::vec3 splatPos = bombPos + offset;

                            // 計算墨水大小 (中間大，旁邊小)
                            // 內圈大小約 4.0，外圈遞減到 1.5
                            float scale = 4.0f - (2.5f * (float)l / layers);
                            float uvSize = (scale * 2.0f) / mapSize;
                            float rot = (float)(rand() % 360);

                            // 執行塗地 (Paint)
                            auto result = SplatPhysics::WorldToUV(splatPos, glm::vec3(0), mapSize, mapSize);
                            if (result.hit) {
                                // 判斷高度 (地板 vs 障礙物)
                                float h = level->GetHeightAt(splatPos.x, splatPos.z);
                                SplatMap* target = (h > 0.5f) ? mapObstacle.get() : mapFloor.get();

                                // 每一滴都執行一次 UpdateCPUData，確保網格被填滿
                                painter->Paint(target, result.uv, uvSize, p->inkColor, rot, p->ownerTeam);
                            }
                        }
                    }
                    AudioManager::Instance().PlayOneShot("explode", 1.0f);
                    if (particleSystem) particleSystem->Emit(bombPos, p->inkColor, 50, 25.0f);

                    // --- B. 傷害判定 (只有 Server 執行) ---
                    if (NetworkManager::Instance().IsServer()) {
                        float blastRadius = 10.0f;
                        float damage = 999.0f;

                        // 1. 檢查本機玩家 (Server 自己)
                        if (localPlayer) {
                            float dist = glm::distance(p->transform->position, localPlayer->transform->position);
                            if (dist < blastRadius) {
                                // 規則：會炸死敵人，也會炸死自己(自殺)，但不會炸死隊友
                                if (localPlayer->teamID != p->ownerTeam || p->ownerID == NetworkManager::Instance().GetMyPlayerID()) {
                                    auto hp = localPlayer->GetComponent<Health>();
                                    if (hp) {
                                        hp->TakeDamage(damage);
                                        if (hp->isDead) {
                                            ProcessKillEvent(p->ownerID, localPlayer.get(), p->ownerTeam);
                                        }
                                    }
                                }
                            }
                        }

                        // 2. 檢查 AI
                        if (enemyAI) {
                            float dist = glm::distance(p->transform->position, enemyAI->transform->position);
                            if (dist < blastRadius) {
                                if (enemyAI->teamID != p->ownerTeam) {
                                    auto hp = enemyAI->GetComponent<Health>();
                                    if (hp) {
                                        hp->TakeDamage(damage);
                                        if (hp->isDead) ProcessKillEvent(p->ownerID, enemyAI.get(), p->ownerTeam);
                                    }
                                }
                            }
                        }

                        // 3. 檢查遠端玩家 (Clients)
                        for (auto& pair : remotePlayers) {
                            RemotePlayer* rp = pair.second.get();
                            float dist = glm::distance(p->transform->position, rp->transform->position);

                            if (dist < blastRadius) {
                                // 判斷敵我 (或自殺)
                                if (rp->teamID != p->ownerTeam || pair.first == p->ownerID) {
                                    ProcessKillEvent(p->ownerID, rp, p->ownerTeam);
                                }
                            }
                        }
                    }

                    it = projectiles.erase(it);
                    continue;
                }

                // B. 撞地反彈 (Bouncing)
                if (p->hasHitFloor) {
                    // 簡單反彈：Y 軸速度反轉並衰減
                    p->velocity.y = -p->velocity.y * 0.8f; // 彈性係數
                    p->velocity.x *= 0.9f; // 摩擦力
                    p->velocity.z *= 0.9f;

                    // 如果彈跳太小就停止
                    if (abs(p->velocity.y) < 1.0f) p->velocity.y = 0;

                    // 修正位置
                    p->transform->position = p->hitPosition + glm::vec3(0, 0.1f, 0);
                    p->hasHitFloor = false; // 重置碰撞旗標
                }
                ++it;
                continue;
            }

            // 地板碰撞塗地
            if (p->hasHitFloor) {
                auto result = SplatPhysics::WorldToUV(
                    p->hitPosition, level->floor->transform->position,
                    level->floor->width, level->floor->depth
                );

                if (result.hit) {
                    float uvSize = (p->transform->scale.x * inkMultiplier) / mapSize;
                    float rot = (float)(rand() % 360);
                    float paintSize = p->transform->scale.x * 0.7f;
                    painter->Paint(mapFloor.get(), result.uv, uvSize, p->inkColor, rot, p->ownerTeam);
                    // 擊中地板噴墨水
                    // 產生 10 顆粒子，速度 5.0f
                    particleSystem->Emit(p->hitPosition + glm::vec3(0, 0.2f, 0), p->inkColor, 10, 5.0f);
                }
                it = projectiles.erase(it);
            }
            else if (p->isDead) {
                it = projectiles.erase(it);
            }
            else {
                ++it;
            }
        }
    }

void GameWorld::ProcessKillEvent(int killerID, Entity* victim, int killerTeam) {
        int victimID = -99;
        int victimTeam = victim->teamID;

        if (localPlayer && victim == localPlayer.get()) {
            victimID = NetworkManager::Instance().GetMyPlayerID();
            localPlayer->Die();
            std::cout << "[Server] I was killed by Player " << killerID << std::endl;
        }
        else if (enemyAI && victim == enemyAI.get()) {
            victimID = 100;
            auto hp = enemyAI->GetComponent<Health>();
            if (hp) {
                hp->Reset();
                enemyAI->transform->position = hp->spawnPoint;
            }
        }
        else {
            for (auto& rp : remotePlayers) {
                if (rp.second.get() == victim) {
                    victimID = rp.first;
                    if (NetworkManager::Instance().IsServer()) {
                        rp.second->ForceDeadByServer(2.0f);
                    }
                    break;
                }
            }
        }

        // send packet
        PacketKillEvent pkt;
        pkt.header.type = PacketType::S2C_KILL_EVENT;
        pkt.killerID = killerID;
        pkt.victimID = victimID;
        pkt.killerTeam = killerTeam;
        pkt.victimTeam = victimTeam;
        NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true);

        std::cout << "[Server] Sent Kill Event: " << killerID << " -> " << victimID << std::endl;

        // update local kill log
        if (hudRef) {
            hudRef->AddKillLog(killerID, victimID, killerTeam, victimTeam);
        }
    }

void GameWorld::CreateRemotePlayer(int playerID, int teamID, glm::vec3 startPos) {
        if (remotePlayers.find(playerID) != remotePlayers.end()) return;

        auto remoteP = std::make_unique<RemotePlayer>(playerID, teamID, startPos);
        remoteP->transform->position = startPos;

		// fetch resource via teamID
        std::shared_ptr<Mesh> targetMesh;
        std::shared_ptr<Texture> targetTex;
        glm::vec3 tintColor = glm::vec3(1.0f); // 預設白色
        glm::vec3 teamColor = (teamID == 1) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);

        if (teamID == 1) { // 紅隊
            targetMesh = meshRedTeam;
            targetTex = texRedTeam;
            if (!targetTex) tintColor = glm::vec3(1.0f, 0.2f, 0.2f);
        }
        else { // 綠隊
            targetMesh = meshGreenTeam;
            targetTex = texGreenTeam;
            if (!targetTex) tintColor = glm::vec3(0.2f, 1.0f, 0.2f);
        }

        auto humanObj = std::make_unique<GameObject>("RemoteHuman");
        humanObj->SetParent(remoteP.get());
        {
            auto mr = humanObj->AddComponent<MeshRenderer>(targetMesh ? targetMesh : MeshFactory::GetCube(), glm::vec3(1.0f));
            if (targetTex) mr->SetTexture(targetTex);
            else mr->SetColor(teamColor);

            humanObj->transform->scale = glm::vec3(0.6f);
            humanObj->transform->position = glm::vec3(0, 0.0f, 0);
        }

        // 2) 魷魚
        auto squidObj = std::make_unique<GameObject>("RemoteSquid");
        squidObj->SetParent(remoteP.get());
        squidObj->active = false;
        squidObj->AddComponent<MeshRenderer>("Cube", teamColor);
        squidObj->transform->scale = glm::vec3(0.4f, 0.2f, 0.6f);
        squidObj->transform->position = glm::vec3(0, -0.8f, 0);

        remoteP->SetupVisuals(humanObj.get(), squidObj.get());
        visualEntities.push_back(std::move(humanObj));
        visualEntities.push_back(std::move(squidObj));
        remotePlayers[playerID] = std::move(remoteP);
        std::cout << "Spawned Remote Player " << playerID << " (Team " << teamID << ")" << std::endl;
    }

void GameWorld::SpawnSharkBullets() {
        if (!localPlayer) return;
        Player* p = localPlayer.get();

        glm::vec3 center = p->transform->position + glm::vec3(0, 0.5f, 0);
        glm::vec3 fwd = p->transform->GetForward();
        glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));

        int bulletCount = 6;
        float baseSpeed = 6.0f;

        for (int i = 0; i < bulletCount; i++) {
            float side = (rand() % 2 == 0) ? 1.0f : -1.0f;
            glm::vec3 dir = right * side;
            float randX = ((rand() % 100) / 100.0f - 0.5f) * 0.6f;
            dir.x += randX;
            dir.z += randX;
            float randY = 0.2f + ((rand() % 100) / 100.0f) * 0.6f;
            dir.y = randY;
            dir = glm::normalize(dir);
            float randSpeed = baseSpeed + ((rand() % 100) / 100.0f * 5.0f); // 12 ~ 17
            float randScale = 0.3f + ((rand() % 100) / 100.0f * 0.4f); // 0.3 ~ 0.7

            auto bullet = std::make_unique<Projectile>(
                center,
                dir * randSpeed,
                p->weapon->inkColor,
                p->teamID,
                randScale,
                NetworkManager::Instance().GetMyPlayerID(),
                ProjectileType::BULLET
            );

            bullet->lifeTime = 1.0f + ((rand() % 100) / 100.0f);

            projectiles.push_back(std::move(bullet));

            // 發送封包
            if (NetworkManager::Instance().IsConnected()) {
                PacketShoot pkt;
                pkt.header.type = PacketType::C2S_SHOOT;
                pkt.playerID = NetworkManager::Instance().GetMyPlayerID();
                pkt.origin = center;
                pkt.direction = dir;
                pkt.speed = randSpeed;
                pkt.scale = randScale;
                pkt.color = p->weapon->inkColor;
                pkt.type = ProjectileType::BULLET;

                NetworkManager::Instance().SendToServer(&pkt, sizeof(pkt), false);
            }
        }
    }

void GameWorld::SpawnSharkExplosion() {
        Player* p = localPlayer.get();
        auto bomb = std::make_unique<Projectile>(
            p->transform->position,
            glm::vec3(0), // 沒速度
            p->weapon->inkColor,
            p->teamID,
            1.0f,
            NetworkManager::Instance().GetMyPlayerID(),
            ProjectileType::BULLET
        );

        bomb->fuseTimer = 0.0f;

        projectiles.push_back(std::move(bomb));

        // 這裡不需要發送 SHOOT 封包，因為爆炸是 Server 權威判定的
        // 但為了視覺同步，我們可以發送一個 SHOOT 封包帶有 BOMB 屬性且速度為 0
        // 或者依靠 Server 廣播爆炸事件
        // 最簡單的方法：發送一個 BOMB SHOOT，但 fuseTimer 極短
        if (NetworkManager::Instance().IsConnected()) {
            PacketShoot pkt;
            pkt.header.type = PacketType::C2S_SHOOT;
            pkt.playerID = NetworkManager::Instance().GetMyPlayerID();
            pkt.origin = p->transform->position;
            pkt.direction = glm::vec3(0, 1, 0);
            pkt.speed = 0.0f;
            pkt.scale = 1.0f;
            pkt.color = p->weapon->inkColor;
            pkt.type = ProjectileType::BOMB;
            NetworkManager::Instance().SendToServer(&pkt, sizeof(pkt), true);
        }
    }

void GameWorld::TriggerLaserBeam(glm::vec3 start, glm::vec3 dir, int teamID, int attackerID) {
        float maxDist = 60.0f;
        float stepSize = 1.0f;

        glm::vec3 currentPos = start;
        glm::vec3 endPos = start + (dir * maxDist);

        // 1. 尋找撞牆點 (raycast)
        for (float d = 0; d < maxDist; d += stepSize) {
            currentPos += dir * stepSize;
            float terrainH = level->GetHeightAt(currentPos.x, currentPos.z);

            // 如果地形高度 > 雷射高度，代表撞牆了
            if (terrainH > currentPos.y) {
                endPos = currentPos; // 更新終點為撞擊點
                break;
            }
        }

        // 2. 畫墨水
        // 從起點到終點，每隔一段距離畫一個墨跡
        float dist = glm::distance(start, endPos);
        float inkSpacing = 1.5f;
        int paintSteps = (int)(dist / inkSpacing);
        float beamWidth = 4.0f;
        float uvSize = beamWidth / level->mapSize;
        glm::vec3 color = (teamID == 1) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);

        // 沿線繪製
        for (int i = 0; i <= paintSteps; i++) {
            float t = (float)i / (float)paintSteps;
            glm::vec3 paintPos = glm::mix(start, endPos, t);

            auto result = SplatPhysics::WorldToUV(paintPos, glm::vec3(0), level->mapSize, level->mapSize);
            if (result.hit) {
                // 雷射經過的地方，如果是高處就畫 ObstacleMap，低處就畫 FloorMap
                float h = level->GetHeightAt(paintPos.x, paintPos.z);
                SplatMap* targetMap = (h > 0.5f) ? mapObstacle.get() : mapFloor.get();

                painter->Paint(targetMap, result.uv, uvSize, color, 0, teamID);
            }
        }

        AudioManager::Instance().PlayOneShot("laser_fire", 1.0f);

        if (!NetworkManager::Instance().IsServer()) return;

        std::vector<Entity*> targets;
        if (localPlayer) targets.push_back(localPlayer.get());
        if (enemyAI) targets.push_back(enemyAI.get());
        for (auto& pair : remotePlayers) targets.push_back(pair.second.get());

        // 雷射判定寬度 (比墨水寬度小一點，要求精準)
        float hitWidth = 3.0f;

        for (Entity* t : targets) {
            // 計算 點(Enemy) 到 線段(Start-End) 的最短距離
            float d = PointToLineSegmentDistance(t->transform->position, start, endPos);

            if (d < hitWidth) {
                Health* hp = t->GetComponent<Health>();
                if (hp && hp->teamID != teamID) {
                    hp->TakeDamage(999.0f); // 秒殺

                    // 死亡處理
                    if (hp->isDead) {
                        ProcessKillEvent(attackerID, t, teamID);
                        if (t == enemyAI.get()) {
                            hp->Reset();
                            enemyAI->transform->position = hp->spawnPoint;
                        }
                        if (t == localPlayer.get()) {
                            localPlayer->Die();
                        }
                        SpawnDeathSplat(t->transform->position, color);
                    }
                }
            }
        }
    }

void GameWorld::SpawnRandomItem() {
        if (level->itemSpawnPoints.empty()) return;
        int idx = rand() % level->itemSpawnPoints.size();
        glm::vec3 pos = level->itemSpawnPoints[idx];

        // 避免重疊生成
        for (auto& item : items) if (glm::distance(item->transform->position, pos) < 1.0f) return;

        items.push_back(std::make_unique<Item>(pos, ItemType::BOMB));
    }

void GameWorld::SpawnBombProjectile() {
        if (!localPlayer) return;
        Player* pl = localPlayer.get();
        int myID = NetworkManager::Instance().GetMyPlayerID();
        glm::vec3 spawnPos = pl->transform->position + glm::vec3(0, 2.5f, 0) + pl->transform->GetForward() * 0.5f;

        // 往上看一點
        glm::vec3 dir = glm::vec3(0, 0, -1);
        if (pl->cameraRef) {
            dir = pl->cameraRef->transform->GetForward();
        }
        dir.y += 0.4f;
        glm::vec3 velocity = glm::normalize(dir) * 15.0f;

        // 建立炸彈
        auto bomb = std::make_unique<Projectile>(
            spawnPos, velocity, pl->weapon->inkColor, pl->teamID, 1.0f,
            NetworkManager::Instance().GetMyPlayerID(), ProjectileType::BOMB
        );
        projectiles.push_back(std::move(bomb));

        if (NetworkManager::Instance().IsConnected()) {
            PacketShoot pkt;
            pkt.header.type = PacketType::C2S_SHOOT;
            pkt.playerID = myID;
            pkt.origin = spawnPos;
            pkt.direction = dir;
            pkt.speed = pl->swimSpeed;
            pkt.scale = 1.0f;
            pkt.color = pl->weapon->inkColor;
            pkt.type = ProjectileType::BOMB;

            NetworkManager::Instance().SendToServer(&pkt, sizeof(pkt), true);
        }

        AudioManager::Instance().PlayOneShot("shoot", 0.8f);
    }

float GameWorld::PointToLineSegmentDistance(glm::vec3 p, glm::vec3 a, glm::vec3 b) {
        glm::vec3 ab = b - a;
        float t = glm::dot(p - a, ab) / glm::dot(ab, ab);

        // 限制 t 在 0~1 之間 (線段內)
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        glm::vec3 closest = a + ab * t;
        return glm::distance(p, closest);
    }

void GameWorld::SpawnDeathSplat(glm::vec3 pos, glm::vec3 color) {
        float mapSize = level->mapSize;
        auto result = SplatPhysics::WorldToUV(pos, glm::vec3(0), mapSize, mapSize);

        if (result.hit) {
            float rot = (float)(rand() % 360);
            float uvSize = 25.0f / mapSize;

            // 判斷死在哪裡，就畫在哪裡
            float h = level->GetHeightAt(pos.x, pos.z);
            SplatMap* targetMap = (h > 0.5f) ? mapObstacle.get() : mapFloor.get();

            painter->Paint(targetMap, result.uv, uvSize, color, rot, 0);
            AudioManager::Instance().PlayOneShot("splat_die", 0.5f);
        }
    }

void GameWorld::EndGame() {
        if (state == WorldState::FINISHED) return;

        state = WorldState::FINISHED;
        finishTimer = kMatchFinishDelaySeconds; // 停留 5 秒
        gameTimeRemaining = 0.0f;

        if (NetworkManager::Instance().IsServer()) {
            auto scores = mapFloor->CalculatePercentages();

            // 0.0~1.0
            finalScoreTeam1 = scores.first;
            finalScoreTeam2 = scores.second;

            PacketGameState pkt;
            pkt.header.type = PacketType::S2C_GAME_STATE;
            pkt.scoreTeam1 = finalScoreTeam1;
            pkt.scoreTeam2 = finalScoreTeam2;
            pkt.timeRemaining = 0.0f;
            NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true);
        }

        if (finalScoreTeam1 > finalScoreTeam2) winningTeam = 1;
        else if (finalScoreTeam2 > finalScoreTeam1) winningTeam = 2;
        else winningTeam = 0;

        std::cout << "GAME FINISHED! T1: " << finalScoreTeam1 * 100.0f << " T2: " << finalScoreTeam2 * 100.0f << std::endl;
        AudioManager::Instance().PlayOneShot("whistle", 1.0f);
    }
