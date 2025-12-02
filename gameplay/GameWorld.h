#pragma once

#include <map>
#include <vector>
#include <iostream>
#include <memory>
#include <algorithm>
#include "../scene/Level.h"
#include "../splat/SplatMap.h"
#include "../splat/SplatPainter.h"
#include "../splat/SplatPhysics.h"
#include "../splat/SplatRenderer.h"
#include "ShooterWeapon.h"
#include "BrushWeapon.h"
#include "SlosherWeapon.h"
#include "Player.h"
#include "Enemy.h"
#include "RemotePlayer.h"
#include "Projectile.h"
#include "../components/Scoreboard.h"
#include "../components/Health.h"
#include "../network/NetworkManager.h"
#include "../network/NetworkProtocol.h"

class GameWorld {
public:
    // --- 系統物件 ---
    std::unique_ptr<Level> level;
    std::unique_ptr<SplatMap> splatMap;
    std::unique_ptr<SplatPainter> painter;
    Scoreboard* scoreboardRef = nullptr;
    HUD* hudRef = nullptr;

    // --- 實體物件 ---
    std::unique_ptr<Player> localPlayer;
    std::unique_ptr<Enemy> enemyAI;
    std::vector<std::unique_ptr<Projectile>> projectiles;

    // 遠端玩家列表
    std::map<int, std::unique_ptr<RemotePlayer>> remotePlayers;

    // 同步計時器
    float syncTimer = 0.0f;

    void Init(GameObject* mainCamera, HUD* hud, Scoreboard* scoreboard) {
        level = std::make_unique<Level>();
        level->Load();
        splatMap = std::make_unique<SplatMap>(1024, 1024);
        painter = std::make_unique<SplatPainter>();
        scoreboardRef = scoreboard;
        hudRef = hud;

        int myTeam = NetworkManager::Instance().GetMyTeamID();
        // 如果是單機測試(沒連線)，預設給 1，否則用存好的 ID
        if (!NetworkManager::Instance().IsConnected()) {
            myTeam = 1;
        }
        // Server 強制為 0 號 ID, 1 號隊伍 (雖然在 Lobby 邏輯應該已經設好了，這裡保險起見)
        if (NetworkManager::Instance().IsServer()) {
            NetworkManager::Instance().SetMyPlayerID(0);
            NetworkManager::Instance().SetMyTeamID(1);
            myTeam = 1;
        }

        WeaponType myWeaponType = NetworkManager::Instance().GetMyWeaponType();
        localPlayer = std::make_unique<Player>(glm::vec3(-5, 0, -5), myTeam, splatMap.get(), mainCamera, hud);
        glm::vec3 color = (myTeam == 1) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);  // replace weapon
        switch (myWeaponType) {
        case WeaponType::SHOOTER:
            localPlayer->weapon = new ShooterWeapon(myTeam, color);
            break;
        case WeaponType::BRUSH:
            localPlayer->weapon = new BrushWeapon(myTeam, color);
            break;
        case WeaponType::SLOSHER:
            localPlayer->weapon = new SlosherWeapon(myTeam, color);
            break;
        default:
            localPlayer->weapon = new ShooterWeapon(myTeam, color);
            break;
        }

        if (NetworkManager::Instance().IsServer()) {
            NetworkManager::Instance().SetMyPlayerID(0); // 強制設為 0
            localPlayer->teamID = 1;
            localPlayer->weapon->inkColor = glm::vec3(1, 0, 0);
            // 只有 Server 建立 AI (具備邏輯的實體)
            enemyAI = std::make_unique<Enemy>(glm::vec3(5, 0, 5), 2);
        }
        else {
            enemyAI = nullptr;
        }
    }

    // AABB 碰撞檢測 (包含球體半徑判定)
    bool CheckCollision(GameObject* bullet, GameObject* target) {
        glm::vec3 posB = bullet->transform->position;
        glm::vec3 posT = target->transform->position;

        // 判定中心點稍微上移 (因為人是站著的)
        glm::vec3 centerT = posT + glm::vec3(0, 1.0f, 0);
        float bulletRadius = bullet->transform->scale.x * 0.5f;

        float dist = glm::distance(posB, centerT);

        // 判定距離 = 人身半徑 (0.5) + 子彈半徑
        return dist < (0.5f + bulletRadius);
    }

    void Update(float dt) {
        // --- 1. 更新本機實體 ---
        if (localPlayer) {
            localPlayer->UpdateLogic(dt);
            if (localPlayer->weapon) CollectProjectiles(*(localPlayer->weapon));
        }
        if (enemyAI) {
            enemyAI->UpdateLogic(dt);
            if (enemyAI->weapon) CollectProjectiles(*(enemyAI->weapon));
        }

        // 檢查雷射請求
        if (localPlayer && localPlayer->requestLaser) {
            // 參數：發射位置 (稍微高一點，從槍口或眼睛)、發射方向、隊伍
            glm::vec3 startPos = localPlayer->transform->position + glm::vec3(0, 1.5f, 0);
            glm::vec3 dir = localPlayer->transform->GetForward(); // 瞄準方向

            TriggerLaserBeam(startPos, dir, localPlayer->teamID);

            // 如果有網路，這裡要發送封包 (PacketSpecialLaser) 通知 Server
            // ... (省略網路部分，邏輯同之前的 Splashdown) ...

            localPlayer->requestLaser = false;
        }

        // --- 2. 網路同步 (發送本機狀態) ---
        if (NetworkManager::Instance().IsConnected()) {
            syncTimer += dt;
            if (syncTimer > 0.05f) {
                // 1. 發送玩家自己的狀態
                PacketPlayerState pkt;
                pkt.header.type = PacketType::C2S_PLAYER_STATE;
                pkt.playerID = NetworkManager::Instance().GetMyPlayerID();
                pkt.position = localPlayer->transform->position;
                pkt.rotationY = localPlayer->transform->rotation.y;
                pkt.isSwimming = localPlayer->isSwimming;

                // Server or Client
                if (NetworkManager::Instance().IsServer()) {
                    // Server: 廣播自己 (ID 0)
                    PacketPlayerState worldStatePkt = pkt;
                    worldStatePkt.header.type = PacketType::S2C_WORLD_STATE;
                    NetworkManager::Instance().Broadcast(&worldStatePkt, sizeof(worldStatePkt), false);

                    // Server: 廣播 AI (ID 100)
                    if (enemyAI) {
                        PacketPlayerState aiPkt;
                        aiPkt.header.type = PacketType::S2C_WORLD_STATE;
                        aiPkt.playerID = 100;
                        aiPkt.position = enemyAI->transform->position;
                        aiPkt.rotationY = enemyAI->transform->rotation.y;
                        aiPkt.isSwimming = false;
                        NetworkManager::Instance().Broadcast(&aiPkt, sizeof(aiPkt), false);
                    }
                }
                else {
                    // Client: 傳送給 Server
                    NetworkManager::Instance().SendToServer(&pkt, sizeof(pkt), false);
                }
                syncTimer = 0.0f;
            }

            // B. 分數與遊戲狀態同步 (低頻率: 0.5s = 2Hz)
            static float scoreTimer = 0.0f;
            scoreTimer += dt;

            if (scoreTimer > 0.5f) {
                // 只有 Server 有權力廣播分數
                if (NetworkManager::Instance().IsServer()) {
                    // 計算分數
                    glm::vec2 scores = splatMap->CalculateScore();

                    // 發送分數封包
                    PacketGameState scorePkt;
                    scorePkt.header.type = PacketType::S2C_GAME_STATE;
                    scorePkt.scoreTeam1 = scores.x;
                    scorePkt.scoreTeam2 = scores.y;
                    scorePkt.timeRemaining = 180.0f; // 範例時間

                    NetworkManager::Instance().Broadcast(&scorePkt, sizeof(scorePkt), true);

                    // Server 本地 Scoreboard 更新
                    if (scoreboardRef) scoreboardRef->SetScores(scores.x, scores.y);
                }
                scoreTimer = 0.0f;
            }
        }

        // --- 3. 更新遠端玩家 (插值) ---
        for (auto& pair : remotePlayers) {
            pair.second->UpdateInterp(dt);
        }

        // --- 4. 更新子彈物理與碰撞 ---
        UpdateProjectiles(dt);
    }

    void Render(Shader& shader) {
        // 1. 畫地板 (SplatMap)
        SplatRenderer::RenderFloor(shader, level->floor, splatMap.get());

        // 2. 畫牆壁與障礙物 (不透明)
        shader.SetInt("useInk", 0);
        shader.SetFloat("alpha", 1.0f);
        for (auto wall : level->walls) wall->Draw(shader);

        // 3. 畫實體
        for (const auto& p : projectiles) {
            if (p) p->Draw(shader);
        }

        if (localPlayer->GetVisualBody()) localPlayer->GetVisualBody()->Draw(shader);
        if (enemyAI && enemyAI->GetVisualBody()) enemyAI->GetVisualBody()->Draw(shader);

        for (auto& pair : remotePlayers) {
            if (pair.second->GetVisualBody()) pair.second->GetVisualBody()->Draw(shader);
        }

        // 4. 畫陰影 (開啟半透明混合)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE); // 關閉深度寫入
        shader.SetFloat("alpha", 0.5f); // 設定半透明

        // Helper Lambda: 繪製單個陰影
        auto DrawShadow = [&](GameObject* owner) {
            if (!owner) return;
            glm::vec3 shadowPos = owner->transform->position;
            shadowPos.y = 0.02f; // 貼地

            float height = owner->transform->position.y;
            float scale = 1.5f - (height * 0.3f);
            if (scale < 0) scale = 0;

            // 借用 localPlayer 的 shadow 物件來繪製 (省去每個物件都 new 一個 shadow 的開銷)
            // 前提是 localPlayer 必須存在且有 shadow
            if (localPlayer && localPlayer->shadow) {
                GameObject* s = localPlayer->shadow;
                s->transform->position = shadowPos;
                s->transform->scale = glm::vec3(scale, 1.0f, scale);
                s->Draw(shader);
            }
            };

        // 繪製所有人的陰影
        DrawShadow(localPlayer.get());
        if (enemyAI) DrawShadow(enemyAI.get());
        for (auto& pair : remotePlayers) DrawShadow(pair.second.get());

        // 復原 Render State
        shader.SetFloat("alpha", 1.0f);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    // 統一收集並生成子彈 (包含網路發送)
    void CollectProjectiles(Weapon& weapon) {
        for (const auto& info : weapon.pendingSpawns) {
            // A. 本地生成 (視覺立即回饋)
            glm::vec3 velocity = info.dir * info.speed;
            velocity.y += 2.0f;

            int myID = NetworkManager::Instance().GetMyPlayerID();
            auto p = std::make_unique<Projectile>(velocity, info.color, info.team, info.scale, myID);
            p->transform->position = info.pos;
            projectiles.push_back(std::move(p));

            // B. 網路同步 (通知其他人)
            if (NetworkManager::Instance().IsConnected()) {
                PacketShoot pkt;
                pkt.header.type = PacketType::C2S_SHOOT;

                // 判斷這把武器是誰的
                if (weapon.teamID == localPlayer->teamID) {
                    pkt.playerID = NetworkManager::Instance().GetMyPlayerID();
                }
                else if (enemyAI && weapon.teamID == enemyAI->teamID) {
                    pkt.playerID = 100; // 如果是 AI 的武器，ID 填 100
                }
                else {
                    pkt.playerID = -1; // 防呆
                }

                pkt.playerID = NetworkManager::Instance().GetMyPlayerID();
                pkt.origin = info.pos;
                pkt.direction = info.dir;
                pkt.speed = info.speed;
                pkt.scale = info.scale;
                pkt.color = info.color;

                // 傳送邏輯
                if (NetworkManager::Instance().IsServer()) {
                    // 如果我是 Server，直接轉成 S2C 廣播給所有人 (除了自己)
                    // 這裡為了簡化，廣播給全體，Client 端再濾掉自己 ID
                    pkt.header.type = PacketType::S2C_SHOOT_EVENT;
                    NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true);
                }
                else {
                    // 如果我是 Client，請求 Server
                    NetworkManager::Instance().SendToServer(&pkt, sizeof(pkt), true);
                }
            }
        }
        weapon.pendingSpawns.clear();
    }

    void CleanUp() {
        remotePlayers.clear();
    }

    void HandlePacket(const ReceivedPacket& received) {
        auto& net = NetworkManager::Instance();

        // =================================================
        // A. Server 專用邏輯：負責轉發 (Relay) Client 的請求
        // =================================================
        if (net.IsServer()) {
            // 1. 收到 Client 的位置更新 -> 轉發為 WORLD_STATE
            if (received.type == PacketType::C2S_PLAYER_STATE) {
                auto* inPkt = (PacketPlayerState*)received.data.data();

                PacketPlayerState outPkt = *inPkt;
                outPkt.header.type = PacketType::S2C_WORLD_STATE; // 改頭換面

                // 廣播給所有人 (UDP Unreliable)
                net.Broadcast(&outPkt, sizeof(outPkt), false);

                // Server 本地也需要更新這個遠端玩家的視覺位置
                HandleWorldState(&outPkt);
            }
            // 2. 收到 Client 的射擊請求 -> 轉發為 SHOOT_EVENT
            else if (received.type == PacketType::C2S_SHOOT) {
                auto* inPkt = (PacketShoot*)received.data.data();

                PacketShoot outPkt = *inPkt;
                outPkt.header.type = PacketType::S2C_SHOOT_EVENT; // 改頭換面

                // 廣播給所有人 (TCP-like Reliable)
                net.Broadcast(&outPkt, sizeof(outPkt), true);

                // Server 本地生成子彈 (除非是 Server 自己發的，那就重複了，需過濾)
                if (inPkt->playerID != net.GetMyPlayerID()) {
                    SpawnRemoteProjectile(outPkt);
                }
            }
            else if (received.type == PacketType::S2C_KILL_EVENT) {
                auto* pkt = (PacketKillEvent*)received.data.data();

                // 顯示在 UI
                if (hudRef) {
                    hudRef->AddKillLog(pkt->killerID, pkt->victimID, pkt->killerTeam, pkt->victimTeam);
                }
            }
        }

        // =================================================
        // B. 通用邏輯 (Client 與 Server 都需要處理的接收邏輯)
        // =================================================

        // 1. 收到世界狀態 (別人移動了)
        if (received.type == PacketType::S2C_WORLD_STATE) {
            HandleWorldState((PacketPlayerState*)received.data.data());
        }
        // 2. 收到射擊事件 (別人開槍了)
        else if (received.type == PacketType::S2C_SHOOT_EVENT) {
            auto* pkt = (PacketShoot*)received.data.data();
            // 關鍵：忽略自己發出的射擊 (因為 CollectProjectiles 已經在本地生成過了)
            if (pkt->playerID != net.GetMyPlayerID()) {
                SpawnRemoteProjectile(*pkt);
            }
        }
        // 3. 收到分數與遊戲狀態更新 (計分板)
        else if (received.type == PacketType::S2C_GAME_STATE) {
            auto* pkt = (PacketGameState*)received.data.data();
            // 更新計分板
            if (scoreboardRef) {
                scoreboardRef->SetScores(pkt->scoreTeam1, pkt->scoreTeam2);
            }
        }
        // 4. (選用) 收到 Join Accept
        // 通常這在大廳階段就處理完了，但如果是中途加入(Hot Join)可能會用到
        else if (received.type == PacketType::S2C_JOIN_ACCEPT) {
            auto* pkt = (PacketJoinAccept*)received.data.data();
            net.SetMyPlayerID(pkt->yourPlayerID);
            localPlayer->teamID = pkt->yourTeamID;
            // 更新顏色...
            glm::vec3 c = (pkt->yourTeamID == 1) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            localPlayer->weapon->inkColor = c;
            localPlayer->weapon->teamID = pkt->yourTeamID;
            if (localPlayer->GetVisualBody())
                localPlayer->GetVisualBody()->GetComponent<MeshRenderer>()->SetColor(c);
        }
    }

private:
    // 處理網路封包
    void ProcessNetworkPackets() {
        auto& net = NetworkManager::Instance();
        while (net.HasPackets()) {
            auto received = net.PopPacket();

            // --- Server 邏輯 (轉發 Relay) ---
            if (net.IsServer()) {
                if (received.type == PacketType::C2S_PLAYER_STATE) {
                    // 轉發玩家移動
                    auto* inPkt = (PacketPlayerState*)received.data.data();
                    PacketPlayerState outPkt = *inPkt;
                    outPkt.header.type = PacketType::S2C_WORLD_STATE;
                    net.Broadcast(&outPkt, sizeof(outPkt), false); // UDP

                    // Server 本地也更新顯示
                    HandleWorldState(&outPkt);
                }
                else if (received.type == PacketType::C2S_SHOOT) {
                    // 轉發射擊事件
                    auto* inPkt = (PacketShoot*)received.data.data();
                    PacketShoot outPkt = *inPkt;
                    outPkt.header.type = PacketType::S2C_SHOOT_EVENT;
                    net.Broadcast(&outPkt, sizeof(outPkt), true); // TCP-like

                    // Server 本地生成子彈 (除非發送者是 Server 自己，那就重複了，需要過濾)
                    if (inPkt->playerID != net.GetMyPlayerID()) {
                        SpawnRemoteProjectile(outPkt);
                    }
                }
            }

            // --- Client & Server 通用邏輯 ---
            if (received.type == PacketType::S2C_WORLD_STATE) {
                HandleWorldState((PacketPlayerState*)received.data.data());
            }
            else if (received.type == PacketType::S2C_SHOOT_EVENT) {
                auto* pkt = (PacketShoot*)received.data.data();
                // 關鍵：忽略自己發出的射擊 (因為 CollectProjectiles 已經在本地生成過了)
                if (pkt->playerID != net.GetMyPlayerID()) {
                    SpawnRemoteProjectile(*pkt);
                }
            }
            else if (received.type == PacketType::S2C_JOIN_ACCEPT) {
                auto* pkt = (PacketJoinAccept*)received.data.data();
                net.SetMyPlayerID(pkt->yourPlayerID);   // ID
                localPlayer->teamID = pkt->yourTeamID;  // team
                glm::vec3 teamColor = (pkt->yourTeamID == 1) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                localPlayer->weapon->inkColor = teamColor;
                localPlayer->weapon->teamID = pkt->yourTeamID;
                if (localPlayer->GetVisualBody()) {
                    auto mr = localPlayer->GetVisualBody()->GetComponent<MeshRenderer>();
                    if (mr) {
                        mr->SetColor(teamColor);
                    }
                }
                std::cout << "Joined Game! ID: " << pkt->yourPlayerID << " Team: " << pkt->yourTeamID << std::endl;
            }

            // 處理分數同步
            if (received.type == PacketType::S2C_GAME_STATE) {
                auto* pkt = (PacketGameState*)received.data.data();

                // 更新 Client 端的記分板
                if (scoreboardRef) {
                    scoreboardRef->SetScores(pkt->scoreTeam1, pkt->scoreTeam2);
                }
            }
        }
    }

    // 生成網路傳來的子彈
    void SpawnRemoteProjectile(const PacketShoot& pkt) {
        glm::vec3 velocity = pkt.direction * pkt.speed;
        velocity.y += 2.0f;

        int team = (pkt.color.x > 0.5f) ? 1 : 2;    // red=1, green=2

        auto p = std::make_unique<Projectile>(velocity, pkt.color, team, pkt.scale, pkt.playerID);
        p->transform->position = pkt.origin;
        projectiles.push_back(std::move(p));
    }

    // 更新或建立遠端玩家
    void HandleWorldState(PacketPlayerState* pkt) {
        int id = pkt->playerID;

        if (id == NetworkManager::Instance().GetMyPlayerID()) return;
        if (id == -1) return;

        if (remotePlayers.find(id) != remotePlayers.end()) {
            remotePlayers[id]->SetTargetState(pkt->position, pkt->rotationY);
        }
        else {

            int guessedTeam = 1; // 預設紅隊
            if (id == 100) {
                guessedTeam = 2; // AI (ID 100) 固定是綠隊
            }
            else {
                guessedTeam = (id % 2 == 0) ? 1 : 2;
            }
            auto newGuy = std::make_unique<RemotePlayer>(id, guessedTeam, pkt->position);
            remotePlayers[id] = std::move(newGuy);
            std::cout << "Spawned Remote Player: " << id << " (Team " << guessedTeam << ")" << std::endl;
        }
    }

    // 子彈物理更新迴圈
    void UpdateProjectiles(float dt) {
        for (auto it = projectiles.begin(); it != projectiles.end(); ) {
            Projectile* p = it->get();
            p->UpdatePhysics(dt);
            bool hitSomething = false;

            // 檢查碰撞 (本機 + AI + 遠端玩家)
            std::vector<Entity*> targets;
            targets.push_back(localPlayer.get());
            if (enemyAI) targets.push_back(enemyAI.get());
            for (auto& pair : remotePlayers) targets.push_back(pair.second.get());

            for (Entity* target : targets) {
                if (!target) continue;
                int targetTeam = target->teamID;
                if (targetTeam == p->ownerTeam) continue;

                if (CheckCollision(p, target)) {
                    Health* hp = target->GetComponent<Health>();
                    if (hp) {
                        bool wasAlive = !hp->isDead;
                        hp->TakeDamage(10.0f);

                        if (wasAlive && hp->isDead) {
                            if (NetworkManager::Instance().IsServer()) {
                                // 獲取受害者 ID
                                int victimID = -99;
                                if (target == localPlayer.get()) victimID = NetworkManager::Instance().GetMyPlayerID();
                                else if (target == enemyAI.get()) victimID = 100;
                                else {
                                    // 找 RemotePlayer ID
                                    for (auto& rp : remotePlayers) {
                                        if (rp.second.get() == target) {
                                            victimID = rp.first;
                                            break;
                                        }
                                    }
                                }

                                // 發送擊殺封包
                                PacketKillEvent pkt;
                                pkt.header.type = PacketType::S2C_KILL_EVENT;
                                pkt.killerID = p->ownerID;
                                pkt.victimID = victimID;
                                pkt.killerTeam = p->ownerTeam;
                                pkt.victimTeam = hp->teamID; // 受害者隊伍

                                NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true);

                                if (hudRef) hudRef->AddKillLog(p->ownerID, victimID, p->ownerTeam, hp->teamID);
                            }

                            // A. 如果是本機玩家
                            if (target == localPlayer.get()) {
                                localPlayer->Die();
                                SpawnDeathSplat(localPlayer->transform->position, p->inkColor);
                            }
                            // B. 如果是 AI
                            else if (target == enemyAI.get()) {
                                // AI 可能直接重生，或者也寫一個 Die 邏輯
                                SpawnDeathSplat(enemyAI->transform->position, p->inkColor);
                                hp->Reset();
                                enemyAI->transform->position = hp->spawnPoint;
                            }
                        }
                    }
                    if (localPlayer && p->ownerTeam == localPlayer->teamID) {

                        AudioManager::Instance().PlayOneShot("hit", 0.8f);
                        if (hudRef) {
                            hudRef->ShowHitMarker();
                        }
                    }
                    hitSomething = true;
                    break;
                }
            }

            if (hitSomething) {
                it = projectiles.erase(it);
                continue;
            }

            // 地板碰撞塗地
            if (p->hasHitFloor) {
                auto result = SplatPhysics::WorldToUV(
                    p->hitPosition, level->floor->transform->position,
                    level->floor->width, level->floor->depth
                );

                if (result.hit) {
                    float rot = (float)(rand() % 360);
                    float paintSize = p->transform->scale.x * 0.7f;
                    painter->Paint(splatMap.get(), result.uv, paintSize, p->inkColor, rot, p->ownerTeam);

                    // charge ultimate
                    if (localPlayer && p->ownerID == NetworkManager::Instance().GetMyPlayerID()) {
                        localPlayer->AddSpecialCharge(0.5f);
                    }
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

    // 處理擊殺事件：識別身分 -> 發送封包 -> 更新本地 UI
    void ProcessKillEvent(int killerID, Entity* victim, int killerTeam) {
        // 1. 找出受害者 ID
        int victimID = -99;

        // 檢查是否是本機玩家 (Host)
        if (victim == localPlayer.get()) {
            victimID = NetworkManager::Instance().GetMyPlayerID();
        }
        // 檢查是否是 AI
        else if (enemyAI && victim == enemyAI.get()) {
            victimID = 100;
        }
        // 檢查是否是遠端玩家
        else {
            for (auto& rp : remotePlayers) {
                if (rp.second.get() == victim) {
                    victimID = rp.first;
                    break;
                }
            }
        }

        // 2. 建構封包
        PacketKillEvent pkt;
        pkt.header.type = PacketType::S2C_KILL_EVENT;
        pkt.killerID = killerID;
        pkt.victimID = victimID;
        pkt.killerTeam = killerTeam;
        pkt.victimTeam = victim->teamID;

        // 3. 廣播給所有 Client (Reliable = true)
        NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true);

        // 4. 更新 Server 本地的擊殺提示 (UI)
        if (hudRef) {
            hudRef->AddKillLog(killerID, victimID, killerTeam, victim->teamID);
        }
    }

    // 雷射核心邏輯
    void TriggerLaserBeam(glm::vec3 start, glm::vec3 dir, int teamID) {
        float maxDist = 60.0f; // 最大射程
        float stepSize = 1.0f; // 步長 (檢測精度)

        glm::vec3 currentPos = start;
        glm::vec3 endPos = start + (dir * maxDist); // 預設終點

        // 1. 尋找撞牆點 (Raycast 模擬)
        // 我們沿著方向每隔 1米檢查一次高度
        for (float d = 0; d < maxDist; d += stepSize) {
            currentPos += dir * stepSize;

            // 檢查地形高度
            float terrainH = level->GetHeightAt(currentPos.x, currentPos.z);

            // 如果地形高度 > 雷射高度，代表撞牆了
            // (注意：這裡假設牆壁很高，如果是矮箱子可能會穿過去，視需求調整判定)
            if (terrainH > currentPos.y) {
                endPos = currentPos; // 更新終點為撞擊點
                break;
            }
        }

        // 2. 畫墨水 (Linear Painting)
        // 從起點到終點，每隔一段距離畫一個墨跡
        float dist = glm::distance(start, endPos);
        float inkSpacing = 1.5f; // 墨水間隔 (越小越密，但也越耗效能)
        int paintSteps = (int)(dist / inkSpacing);

        // 設定雷射寬度 (很粗!)
        float beamWidth = 4.0f;
        float uvSize = beamWidth / level->mapSize;
        glm::vec3 color = (teamID == 1) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);

        // 沿線繪製
        for (int i = 0; i <= paintSteps; i++) {
            float t = (float)i / (float)paintSteps;
            glm::vec3 paintPos = glm::mix(start, endPos, t);

            // 我們只在地面上畫圖，所以要把 Y 壓到地面
            // 使用 WorldToUV 會自動忽略 Y，但為了準確，我們可以用 GetHeightAt 修正 Y
            // 不過 WorldToUV 已經夠用了

            auto result = SplatPhysics::WorldToUV(paintPos, glm::vec3(0), level->mapSize, level->mapSize);
            if (result.hit) {
                painter->Paint(splatMap.get(), result.uv, uvSize, color, 0, teamID);
            }
        }

        // 3. 視覺特效 (Beam Model)
        // 這裡可以生成一個長條狀的圓柱體 GameObject，存活 0.5 秒後消失
        // 為了簡單，我們先只播放音效和畫墨水
        AudioManager::Instance().PlayOneShot("laser_fire", 1.0f);

        // 4. 傷害判定 (Line Segment Intersection)
        // 檢查敵人是否在雷射路徑上
        if (!NetworkManager::Instance().IsServer()) return; // 傷害由 Server 判定

        std::vector<Entity*> targets;
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
                        ProcessKillEvent(NetworkManager::Instance().GetMyPlayerID(), t, teamID);
                        if (t == enemyAI.get()) {
                            hp->Reset();
                            enemyAI->transform->position = hp->spawnPoint;
                        }
                        // 視覺
                        SpawnDeathSplat(t->transform->position, color);
                    }
                }
            }
        }
    }

    // 計算點到線段的最短距離
    float PointToLineSegmentDistance(glm::vec3 p, glm::vec3 a, glm::vec3 b) {
        glm::vec3 ab = b - a;
        float t = glm::dot(p - a, ab) / glm::dot(ab, ab);

        // 限制 t 在 0~1 之間 (線段內)
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        glm::vec3 closest = a + ab * t;
        return glm::distance(p, closest);
    }

    void SpawnDeathSplat(glm::vec3 pos, glm::vec3 color) {
        // 1. 準備參數
        // 假設地板中心在 (0,0,0)，如果你的地板有位移，請填入 level->floor->transform->position
        glm::vec3 floorPos = glm::vec3(0.0f, 0.0f, 0.0f);

        float mapSize = 80.0f;
        if (level) mapSize = level->mapSize;

        // 2. 呼叫 SplatPhysics 計算 UV
        auto result = SplatPhysics::WorldToUV(pos, floorPos, mapSize, mapSize);

        // 3. 如果在範圍內，畫圖
        if (result.hit) {
            // 隨機旋轉
            float rot = (float)(rand() % 360);
            float uvSize = 4.0f / mapSize;

            painter->Paint(splatMap.get(), result.uv, uvSize, color, rot, 0);

            // 播放音效
            AudioManager::Instance().PlayOneShot("splat_die", 0.5f);
        }
    }
};