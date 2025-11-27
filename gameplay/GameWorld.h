#pragma once

#include <map>
#include <vector>
#include <iostream>
#include "../scene/Level.h"
#include "../splat/SplatMap.h"
#include "../splat/SplatPainter.h"
#include "../splat/SplatPhysics.h"
#include "../splat/SplatRenderer.h"
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
    Level* level;
    SplatMap* splatMap;
    SplatPainter* painter;

    // --- 實體物件 ---
    Player* localPlayer;
    Enemy* enemyAI;
    std::vector<Projectile*> projectiles;

    // 遠端玩家列表 (ID -> 物件指標)
    std::map<int, RemotePlayer*> remotePlayers;

    // 同步計時器
    float syncTimer = 0.0f;

    void Init(GameObject* mainCamera, HUD* hud) {
        // 1. 初始化場景與塗地系統
        level = new Level();
        level->Load();

        splatMap = new SplatMap(1024, 1024);
        painter = new SplatPainter();

        // 2. 建立本機玩家
        // 預設先給 Team 1 (紅)，連線後會被 Server 修正
        localPlayer = new Player(glm::vec3(-5, 0, -5), 1, splatMap, mainCamera, hud);

        // [關鍵修正] 再次確保 Server 狀態正確
        if (NetworkManager::Instance().IsServer()) {
            NetworkManager::Instance().SetMyPlayerID(0); // 強制設為 0
            localPlayer->teamID = 1;
            localPlayer->weapon->inkColor = glm::vec3(1, 0, 0);
        }

        // 3. 建立 AI (連線測試時可考慮註解掉，或是只由 Server 生成)
        enemyAI = new Enemy(glm::vec3(5, 0, 5), 2);
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

        // --- 2. 網路同步 (發送本機狀態) ---
        if (NetworkManager::Instance().IsConnected()) {
            syncTimer += dt;
            if (syncTimer > 0.05f) {
                PacketPlayerState pkt;
                // 填寫基本資料
                pkt.header.type = PacketType::C2S_PLAYER_STATE;
                pkt.playerID = NetworkManager::Instance().GetMyPlayerID();
                pkt.position = localPlayer->transform->position;
                pkt.rotationY = localPlayer->transform->rotation.y;
                pkt.isSwimming = localPlayer->isSwimming;

                // Server or Client
                if (NetworkManager::Instance().IsServer()) {
                    // 如果我是 Server，我要把「我的位置」直接變成 WorldState 廣播給大家
                    // 這樣 Client 才會收到 ID:0 的位置
                    PacketPlayerState worldStatePkt = pkt;
                    worldStatePkt.header.type = PacketType::S2C_WORLD_STATE;

                    // 廣播給所有 Client
                    NetworkManager::Instance().Broadcast(&worldStatePkt, sizeof(worldStatePkt), false);
                }
                else {
                    // 如果我是 Client，我傳給 Server
                    NetworkManager::Instance().SendToServer(&pkt, sizeof(pkt), false);
                }

                syncTimer = 0.0f;
            }
        }

        // --- 3. 處理接收到的封包 ---
        ProcessNetworkPackets();

        // --- 4. 更新遠端玩家 (插值) ---
        for (auto& pair : remotePlayers) {
            pair.second->UpdateInterp(dt);
        }

        // --- 5. 更新子彈物理與碰撞 ---
        UpdateProjectiles(dt);
    }

    void Render(Shader& shader) {
        // 1. 畫地板 (SplatMap)
        SplatRenderer::RenderFloor(shader, level->floor, splatMap);

        // 2. 畫牆壁與障礙物 (不透明)
        shader.SetInt("useInk", 0);
        shader.SetFloat("alpha", 1.0f);
        for (auto wall : level->walls) wall->Draw(shader);

        // 3. 畫實體
        for (auto p : projectiles) p->Draw(shader);

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
        DrawShadow(localPlayer);
        if (enemyAI) DrawShadow(enemyAI);
        for (auto& pair : remotePlayers) DrawShadow(pair.second);

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
            Projectile* p = new Projectile(velocity, info.color, info.team, info.scale);
            p->transform->position = info.pos;
            projectiles.push_back(p);

            // B. 網路同步 (通知其他人)
            if (NetworkManager::Instance().IsConnected()) {
                PacketShoot pkt;
                pkt.header.type = PacketType::C2S_SHOOT;
                pkt.playerID = NetworkManager::Instance().GetMyPlayerID();
                pkt.origin = info.pos;
                pkt.direction = info.dir;
                pkt.speed = info.speed;
                pkt.scale = info.scale;
                pkt.color = info.color; // 簡單傳顏色，正規應傳 teamID

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
        if (level) delete level;
        if (splatMap) delete splatMap;
        if (painter) delete painter;
        if (localPlayer) delete localPlayer;
        if (enemyAI) delete enemyAI;
        for (auto p : projectiles) delete p;
        for (auto& pair : remotePlayers) delete pair.second;
        remotePlayers.clear();
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
        }
    }

    // 生成網路傳來的子彈
    void SpawnRemoteProjectile(const PacketShoot& pkt) {
        glm::vec3 velocity = pkt.direction * pkt.speed;
        velocity.y += 2.0f;
        // 簡單判斷隊伍 (紅=1, 綠=2)
        int team = (pkt.color.x > 0.5f) ? 1 : 2;

        Projectile* p = new Projectile(velocity, pkt.color, team, pkt.scale);
        p->transform->position = pkt.origin;
        projectiles.push_back(p);
    }

    // 更新或建立遠端玩家
    void HandleWorldState(PacketPlayerState* pkt) {
        int id = pkt->playerID;
        // [關鍵] 如果收到 ID 0 的封包，且我自己就是 ID 0 (Server)，那就要忽略
        // 如果收到 ID -1，這是不合法的，也忽略
        if (id == NetworkManager::Instance().GetMyPlayerID()) return;
        if (id == -1) return;

        if (remotePlayers.find(id) == remotePlayers.end()) {
            // [修正] 根據 ID 推算隊伍 (偶數=紅=1, 奇數=綠=2)
            // Server(0) -> 紅, Client(1) -> 綠, Client(2) -> 紅...
            int guessedTeam = (id % 2 == 0) ? 1 : 2;

            RemotePlayer* newGuy = new RemotePlayer(id, guessedTeam, pkt->position);
            remotePlayers[id] = newGuy;
            std::cout << "New Remote Player: " << id << " (Team " << guessedTeam << ")" << std::endl;
        }
        else {
            remotePlayers[id]->SetTargetState(pkt->position, pkt->rotationY);
        }
    }

    // 子彈物理更新迴圈
    void UpdateProjectiles(float dt) {
        for (auto it = projectiles.begin(); it != projectiles.end(); ) {
            Projectile* p = *it;
            p->UpdatePhysics(dt);

            bool hitSomething = false;

            // 檢查碰撞 (本機 + AI + 遠端玩家)
            std::vector<Entity*> targets;
            targets.push_back(localPlayer);
            if (enemyAI) targets.push_back(enemyAI);
            for (auto& pair : remotePlayers) targets.push_back(pair.second);

            for (Entity* target : targets) {
                if (!target) continue;

                // 必須要有 Health 才能被打
                Health* hp = target->GetComponent<Health>();
                // 簡單防呆: 如果是 Projectile 自己人射的就不打 (ownerTeam)
                // 這裡假設 target 也有 teamID 屬性 (Entity 沒有，但 Player/Enemy/RemotePlayer 有)
                // 為了方便，我們 cast 一下，或者在 Entity 加 teamID
                int targetTeam = 0;
                if (auto pl = dynamic_cast<Player*>(target)) targetTeam = pl->teamID;
                else if (auto en = dynamic_cast<Enemy*>(target)) targetTeam = en->teamID;
                else if (auto rm = dynamic_cast<RemotePlayer*>(target)) targetTeam = rm->teamID;

                if (hp && targetTeam != p->ownerTeam) {
                    if (CheckCollision(p, target)) {
                        hp->TakeDamage(10.0f);
                        hitSomething = true;
                        break;
                    }
                }
            }

            if (hitSomething) {
                delete p;
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
                    float rot = (float)(rand() % 360) * 3.14159f / 180.0f;
                    float size = 0.02f + ((rand() % 100) / 4000.0f); // 縮小的墨跡
                    painter->Paint(splatMap, result.uv, size, p->inkColor, rot, p->ownerTeam);
                }
                delete p;
                it = projectiles.erase(it);
            }
            else if (p->isDead) {
                delete p;
                it = projectiles.erase(it);
            }
            else {
                ++it;
            }
        }
    }
};