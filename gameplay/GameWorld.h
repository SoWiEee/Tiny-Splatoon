#pragma once

#include <map>
#include <vector>
#include <iostream>
#include <memory>
#include <algorithm>
#include "../engine/fx/ParticleSystem.h"
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
#include "Item.hpp"
#include "../components/Scoreboard.h"
#include "../components/Health.h"
#include "../network/NetworkManager.h"
#include "../network/NetworkProtocol.h"

enum class WorldState {
    PLAYING,
    FINISHED
};

class GameWorld {
public:
    // --- 系統物件 ---
    std::unique_ptr<Level> level;
    std::unique_ptr<SplatMap> mapFloor;
    std::unique_ptr<SplatMap> mapObstacle;
    std::unique_ptr<SplatPainter> painter;
    std::unique_ptr<ParticleSystem> particleSystem;
    Scoreboard* scoreboardRef = nullptr;
    HUD* hudRef = nullptr;

    // --- 實體物件 ---
    std::unique_ptr<Player> localPlayer;
    std::unique_ptr<Enemy> enemyAI;
    std::vector<std::unique_ptr<Projectile>> projectiles;
    std::vector<std::unique_ptr<Item>> items;
    float itemRespawnTimer = 0.0f;

    // 遠端玩家列表
    std::map<int, std::unique_ptr<RemotePlayer>> remotePlayers;


    // 同步計時器
    float syncTimer = 0.0f;

    // 遊戲狀態變數
    WorldState state = WorldState::PLAYING;
    float gameTimeRemaining = 180.0f; // 3分鐘
    float finishTimer = 0.0f;         // 結束後的 5秒倒數

    // 最終結果緩存
    float finalScoreTeam1 = 0.0f;
    float finalScoreTeam2 = 0.0f;
    int winningTeam = 0; // 0=平手, 1=紅, 2=綠

    void Init(GameObject* mainCamera, HUD* hud, Scoreboard* scoreboard) {
        level = std::make_unique<Level>();
        level->Load();
        mapFloor = std::make_unique<SplatMap>(1024, 1024);
        mapObstacle = std::make_unique<SplatMap>(1024, 1024);
        painter = std::make_unique<SplatPainter>();
        particleSystem = std::make_unique<ParticleSystem>();
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
        localPlayer = std::make_unique<Player>(glm::vec3(-5, 0, -5), myTeam, mapFloor.get(), mapObstacle.get(), mainCamera, hud, level.get());
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

        if (state == WorldState::PLAYING) {
            gameTimeRemaining -= dt;

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
                int myID = NetworkManager::Instance().GetMyPlayerID();
                glm::vec3 startPos = localPlayer->transform->position + glm::vec3(0, 1.5f, 0);
                glm::vec3 dir = localPlayer->transform->GetForward(); // 瞄準方向
                int myTeam = localPlayer->teamID;

                TriggerLaserBeam(startPos, dir, myTeam, myID);

                if (NetworkManager::Instance().IsConnected()) {
                    PacketSpecialLaser pkt; // 使用新的雷射封包
                    pkt.header.type = PacketType::C2S_SPECIAL_ATTACK;
                    pkt.playerID = myID;
                    pkt.teamID = myTeam;
                    pkt.origin = startPos;
                    pkt.direction = dir;

                    NetworkManager::Instance().SendToServer(&pkt, sizeof(pkt), true);
                }

                localPlayer->requestLaser = false;
            }

            if (localPlayer && localPlayer->requestBombThrow) {
                SpawnBombProjectile();
                localPlayer->requestBombThrow = false;
            }

            // B. 道具生成 (維持場上最多 3 個)
            if (items.size() < 3) {
                itemRespawnTimer += dt;
                if (itemRespawnTimer > 5.0f) {
                    SpawnRandomItem();
                    itemRespawnTimer = 0.0f;
                }
            }

            // C. 更新與拾取判定
            for (auto it = items.begin(); it != items.end(); ) {
                (*it)->Update(dt);

                // 拾取距離判定
                if (localPlayer && !localPlayer->hasBomb) {
                    float dist = glm::distance(localPlayer->transform->position, (*it)->transform->position);
                    if (dist < 1.5f) {
                        localPlayer->PickupBomb();
                        AudioManager::Instance().PlayOneShot("reload", 1.0f);
                        it = items.erase(it);
                        continue;
                    }
                }
                ++it;
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
                    auto myHP = localPlayer->GetComponent<Health>();
                    pkt.isDead = (myHP && myHP->isDead);

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
                            auto aiHP = enemyAI->GetComponent<Health>();
                            aiPkt.isDead = (aiHP && aiHP->isDead);
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
                        glm::vec2 scores = mapFloor->CalculateScore();

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
            if (particleSystem) particleSystem->Update(dt);
            UpdateProjectiles(dt);

            if (gameTimeRemaining <= 0.0f) {
                EndGame();
            }
        }
        else if (state == WorldState::FINISHED) {
            finishTimer -= dt;
            if (localPlayer) localPlayer->velocity = glm::vec3(0);
        }
    }

    void Render(Shader& shader, Camera* cam) {
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

        // 畫道具 (用普通的 Shader 設定，不需墨水)
        shader.SetInt("useInk", 0);
        for (auto& item : items) {
            item->Draw(shader);
        }
    }

    // 統一收集並生成子彈 (包含網路發送)
    void CollectProjectiles(Weapon& weapon) {
        for (const auto& info : weapon.pendingSpawns) {
            // A. 本地生成 (視覺立即回饋)
            glm::vec3 velocity = info.dir * info.speed;
            velocity.y += 2.0f;

            int myID = NetworkManager::Instance().GetMyPlayerID();
            glm::vec3 gunPos = localPlayer->transform->position + glm::vec3(0, 1.5f, 0) + localPlayer->transform->GetForward() * 0.5f;
            auto p = std::make_unique<Projectile>(gunPos, velocity, info.color, info.team, info.scale, myID, false);
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
                pkt.origin = info.pos;  // need gunPos?
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

        // A. Server Logic
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
                outPkt.header.type = PacketType::S2C_SHOOT_EVENT;

                net.Broadcast(&outPkt, sizeof(outPkt), true);

                // Server 本地生成子彈 (除非是 Server 自己發的，那就重複了，需過濾)
                if (inPkt->playerID != net.GetMyPlayerID()) {
                    SpawnRemoteProjectile(outPkt);
                }
            }
            else if (received.type == PacketType::C2S_SPECIAL_ATTACK) {
                auto* inPkt = (PacketSpecialLaser*)received.data.data();

                TriggerLaserBeam(inPkt->origin, inPkt->direction, inPkt->teamID, inPkt->playerID);

				// broadcast
                PacketSpecialLaser outPkt = *inPkt;
                outPkt.header.type = PacketType::S2C_SPECIAL_ATTACK;
                net.Broadcast(&outPkt, sizeof(outPkt), true, received.fromConnection);
            }
        }

		// B. Common Client & Server Logic
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
        else if (received.type == PacketType::S2C_SPECIAL_ATTACK) {
            auto* pkt = (PacketSpecialLaser*)received.data.data();
            TriggerLaserBeam(pkt->origin, pkt->direction, pkt->teamID, pkt->playerID);
        }
        else if (received.type == PacketType::S2C_KILL_EVENT) {
            auto* pkt = (PacketKillEvent*)received.data.data();

            // A. 顯示擊殺訊息 (UI)
            if (hudRef) {
                hudRef->AddKillLog(pkt->killerID, pkt->victimID, pkt->killerTeam, pkt->victimTeam);
            }

            // B. 檢查我是不是受害者
            int myID = NetworkManager::Instance().GetMyPlayerID();
            if (pkt->victimID == myID) {
                if (localPlayer) {
                    localPlayer->Die();
                    AudioManager::Instance().PlayOneShot("splatted_by", 1.0f);
                }
            }
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
        bool isBomb = (pkt.type == ProjectileType::BOMB);

        int team = (pkt.color.x > 0.5f) ? 1 : 2;    // red=1, green=2

        auto p = std::make_unique<Projectile>(pkt.origin, velocity, pkt.color, team, pkt.scale, pkt.playerID, isBomb);
        p->transform->position = pkt.origin;
        projectiles.push_back(std::move(p));
    }

    // 更新或建立遠端玩家
    void HandleWorldState(PacketPlayerState* pkt) {
        int id = pkt->playerID;

        if (id == NetworkManager::Instance().GetMyPlayerID()) return;
        if (id == -1) return;

        if (remotePlayers.find(id) != remotePlayers.end()) {
            remotePlayers[id]->SetTargetState(pkt->position, pkt->rotationY, pkt->isSwimming, pkt->isDead);
        }
        else {

            int guessedTeam = (id == 100) ? 2 : ((id % 2 == 0) ? 1 : 2);

            auto newGuy = std::make_unique<RemotePlayer>(id, guessedTeam, pkt->position);
            newGuy->SetTargetState(pkt->position, pkt->rotationY, pkt->isSwimming, pkt->isDead);
            remotePlayers[id] = std::move(newGuy);
            std::cout << "Spawned Remote Player: " << id << " (Team " << guessedTeam << ")" << std::endl;
        }
    }

    // 子彈物理更新迴圈
    void UpdateProjectiles(float dt) {
        float mapSize = level->mapSize;
        float inkMultiplier = 50.0f;

        for (auto it = projectiles.begin(); it != projectiles.end(); ) {
            Projectile* p = it->get();
            p->UpdatePhysics(dt);

            // --- 炸彈專用邏輯 ---
            if (p->isBomb) {
                // A. 爆炸 (時間到)
                if (p->hasExploded) {
                    // 1. 產生超大墨水 (半徑 5.0 UV)
                    // inkMultiplier 設超大
                    float uvSize = (5.0f * 10.0f) / mapSize;

                    // 雙地圖塗色 (爆炸通常會炸到所有東西)
                    auto result = SplatPhysics::WorldToUV(p->transform->position, glm::vec3(0), mapSize, mapSize);
                    if (result.hit) {
                        painter->Paint(mapFloor.get(), result.uv, uvSize, p->inkColor, 0, p->ownerTeam);
                        painter->Paint(mapObstacle.get(), result.uv, uvSize, p->inkColor, 0, p->ownerTeam);
                    }

                    // 2. 傷害判定 (半徑 5米)
                    // TODO: 遍歷 enemies 做距離判定 TakeDamage(999)

                    // 3. 音效與特效
                    AudioManager::Instance().PlayOneShot("explode", 1.0f);
                    if (particleSystem) particleSystem->Emit(p->transform->position, p->inkColor, 50, 15.0f);

                    it = projectiles.erase(it);
                    continue;
                }

                // B. 撞地反彈 (Bouncing)
                if (p->hasHitFloor) {
                    // 簡單反彈：Y 軸速度反轉並衰減
                    p->velocity.y = -p->velocity.y * 0.6f; // 0.6 是彈性係數
                    p->velocity.x *= 0.8f; // 摩擦力
                    p->velocity.z *= 0.8f;

                    // 如果彈跳太小就停止
                    if (abs(p->velocity.y) < 1.0f) p->velocity.y = 0;

                    // 修正位置
                    p->transform->position = p->hitPosition + glm::vec3(0, 0.1f, 0);
                    p->hasHitFloor = false; // 重置碰撞旗標
                }

                // 炸彈繼續存在，不刪除
                ++it;
                continue;
            }

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
                        // 擊中敵人噴墨水
                        // 產生 15 顆粒子，速度 8.0f，顏色跟子彈一樣
                        particleSystem->Emit(p->transform->position, p->inkColor, 15, 8.0f);

                        if (wasAlive && hp->isDead) {
                            if (NetworkManager::Instance().IsServer()) {
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

                                // sned kill packet
                                PacketKillEvent pkt;
                                pkt.header.type = PacketType::S2C_KILL_EVENT;
                                pkt.killerID = p->ownerID;
                                pkt.victimID = victimID;
                                pkt.killerTeam = p->ownerTeam;
                                pkt.victimTeam = hp->teamID;

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

            for (auto& box : level->colliders) {
                // 簡單判定：檢查子彈是否在 Box 內部 (或是很接近)
                glm::vec3 pos = p->transform->position;
                if (pos.x >= box.min.x && pos.x <= box.max.x &&
                    pos.y >= box.min.y && pos.y <= box.max.y &&
                    pos.z >= box.min.z && pos.z <= box.max.z) {

                    // 擊中障礙物
                    auto result = SplatPhysics::WorldToUV(pos, glm::vec3(0), mapSize, mapSize);
                    if (result.hit) {
                        float uvSize = (p->transform->scale.x * inkMultiplier) / mapSize;
                        float rot = (float)(rand() % 360);
                        painter->Paint(mapObstacle.get(), result.uv, uvSize, p->inkColor, rot, p->ownerTeam);
                        particleSystem->Emit(pos, p->inkColor, 5, 5.0f);
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

    // 處理擊殺事件：識別身分 -> 發送封包 -> 更新本地 UI
    void ProcessKillEvent(int killerID, Entity* victim, int killerTeam) {
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
                    if (NetworkManager::Instance().IsServer()) {
                        rp.second->ForceDeadByServer(2.0f); // 鎖定 2 秒
                    }
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

        std::cout << "[Server] Sent Kill Event: " << killerID << " -> " << victimID << std::endl;

        // 4. 更新 Server 本地的擊殺提示 (UI)
        if (hudRef) {
            hudRef->AddKillLog(killerID, victimID, killerTeam, victim->teamID);
        }
    }

    void TriggerLaserBeam(glm::vec3 start, glm::vec3 dir, int teamID, int attackerID) {
        float maxDist = 60.0f; // 最大射程
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

        if (!NetworkManager::Instance().IsServer()) return; // 傷害由 Server 判定

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

    // 生成隨機道具
    void SpawnRandomItem() {
        if (level->itemSpawnPoints.empty()) return;
        int idx = rand() % level->itemSpawnPoints.size();
        glm::vec3 pos = level->itemSpawnPoints[idx];

        // 避免重疊生成
        for (auto& item : items) if (glm::distance(item->transform->position, pos) < 1.0f) return;

        items.push_back(std::make_unique<Item>(pos, ItemType::BOMB));
    }

    // 生成炸彈實體
    void SpawnBombProjectile() {
        if (!localPlayer) return;
        Player* pl = localPlayer.get();
        int myID = NetworkManager::Instance().GetMyPlayerID();
        glm::vec3 spawnPos = pl->transform->position + glm::vec3(0, 2.5f, 0) + pl->transform->GetForward() * 0.5f;

        // 往上看一點
        glm::vec3 dir = pl->transform->GetForward();
        dir.y += 0.4f;
        glm::vec3 velocity = glm::normalize(dir) * 15.0f;

        // 建立炸彈
        auto bomb = std::make_unique<Projectile>(
            spawnPos, velocity, pl->weapon->inkColor, pl->teamID, 1.0f,
            NetworkManager::Instance().GetMyPlayerID(), true
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

    void EndGame() {
        if (state == WorldState::FINISHED) return;

        state = WorldState::FINISHED;
        finishTimer = 5.0f; // 停留 5 秒
        gameTimeRemaining = 0.0f;

        // 計算最終分數
        auto scores = mapFloor->CalculatePercentages();
        finalScoreTeam1 = scores.first * 100;
        finalScoreTeam2 = scores.second * 100;

        if (finalScoreTeam1 > finalScoreTeam2) winningTeam = 1;
        else if (finalScoreTeam2 > finalScoreTeam1) winningTeam = 2;
        else winningTeam = 0;

        std::cout << "GAME FINISHED! T1: " << finalScoreTeam1 << " T2: " << finalScoreTeam2 << std::endl;
        AudioManager::Instance().PlayOneShot("whistle", 1.0f);
    }
};