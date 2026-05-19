#pragma once

#include <map>
#include <vector>
#include <iostream>
#include <memory>
#include <algorithm>
#include "../engine/fx/ParticleSystem.hpp"
#include "../engine/rendering/ModelLoader.hpp"
#include "../scene/Level.hpp"
#include "../splat/SplatMap.hpp"
#include "../splat/SplatPainter.hpp"
#include "../splat/SplatPhysics.hpp"
#include "../splat/SplatRenderer.hpp"
#include "ShooterWeapon.hpp"
#include "BrushWeapon.hpp"
#include "SlosherWeapon.hpp"
#include "Player.hpp"
#include "Enemy.hpp"
#include "RemotePlayer.hpp"
#include "Projectile.hpp"
#include "Item.hpp"
#include "../components/Scoreboard.hpp"
#include "../components/Health.hpp"
#include "../network/NetworkManager.hpp"
#include "../network/NetworkProtocol.hpp"
#include "../engine/rendering/Texture.hpp"

enum class WorldState {
    PLAYING,
    FINISHED
};

class GameWorld {
public:
    static constexpr float kItemRespawnIntervalSeconds = 5.0f;
    static constexpr float kPlayerSyncIntervalSeconds = 0.05f;
    static constexpr float kScoreSyncIntervalSeconds = 0.5f;
    static constexpr float kMatchFinishDelaySeconds = 5.0f;
    static constexpr float kEntityHitRadius = 0.5f;
    static constexpr float kDefaultProjectileSpeed = 25.0f;
    static constexpr float kDefaultProjectileScale = 0.3f;
    static constexpr float kDefaultProjectileLift = 2.0f;
    static constexpr float kRocketProjectileSpeed = 35.0f;
    static constexpr float kRocketProjectileScale = 0.8f;
    static constexpr float kBombProjectileSpeed = 10.0f;
    static constexpr float kProjectileInkMultiplier = 50.0f;
    static constexpr float kProjectileHitDamage = 10.0f;
    static constexpr float kProjectileHitParticleCount = 15.0f;
    static constexpr float kProjectileHitParticleSpeed = 8.0f;
    static constexpr float kObstacleHitParticleCount = 5.0f;
    static constexpr float kObstacleHitParticleSpeed = 5.0f;
    static constexpr float kFloorHitParticleCount = 10.0f;
    static constexpr float kFloorHitParticleSpeed = 5.0f;
    static constexpr float kRocketExplosionRadius = 4.0f;
    static constexpr float kRocketBlastRadius = 8.0f;
    static constexpr float kRocketBlastDamage = 50.0f;
    static constexpr float kRocketExplosionParticleCount = 80.0f;
    static constexpr float kRocketExplosionParticleSpeed = 40.0f;
    static constexpr float kBombWarningRadius = 15.0f;
    static constexpr float kBombBounceDampingY = 0.8f;
    static constexpr float kBombBounceDampingXZ = 0.9f;
    static constexpr float kBombBounceStopVelocity = 1.0f;
    static constexpr float kBombExplosionRadius = 15.0f;
    static constexpr int kBombExplosionLayers = 5;
    static constexpr float kBombBlastRadius = 10.0f;
    static constexpr float kBombBlastDamage = 999.0f;
    static constexpr float kBombExplosionParticleCount = 50.0f;
    static constexpr float kBombExplosionParticleSpeed = 25.0f;

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
    std::vector<Entity*> frameCollisionTargets;

    std::vector<std::unique_ptr<GameObject>> visualEntities;
    std::shared_ptr<Mesh> meshRedTeam;
    std::shared_ptr<Texture> texRedTeam;
    std::shared_ptr<Mesh> meshGreenTeam;
    std::shared_ptr<Texture> texGreenTeam;
    
    float itemRespawnTimer = 0.0f;
    // 遠端玩家列表
    std::map<int, std::unique_ptr<RemotePlayer>> remotePlayers;


    // 同步計時器
    float syncTimer = 0.0f;
    float scoreSyncTimer = 0.0f;

    // 遊戲狀態變數
    WorldState state = WorldState::PLAYING;
    float gameTimeRemaining = kMatchDurationSeconds; // 3 分鐘
    float finishTimer = 0.0f;         // 結束後的 5 秒倒數

    // 最終結果緩存
    float finalScoreTeam1 = 0.0f;
    float finalScoreTeam2 = 0.0f;
    int winningTeam = 0; // 0=平手, 1=紅, 2=綠

    void Init(GameObject* mainCamera, HUD* hud, Scoreboard* scoreboard);
    void CreateProjectile(int ownerID, int teamID, glm::vec3 startPos, glm::vec3 dir, ProjectileType type);
    void SendShootPacket(glm::vec3 pos, glm::vec3 dir, ProjectileType type);
    bool CheckCollision(GameObject* bullet, GameObject* target);
    void Update(float dt);
    void Render(Shader& shader, Camera* cam);
    void CollectProjectiles(Weapon& weapon);
    void CleanUp();
    void HandlePacket(const ReceivedPacket& received);
    void ProcessKillEvent(int killerID, Entity* victim, int killerTeam);
    void CreateRemotePlayer(int playerID, int teamID, glm::vec3 startPos);
    void SpawnSharkBullets();
    void SpawnSharkExplosion();
    void TriggerLaserBeam(glm::vec3 start, glm::vec3 dir, int teamID, int attackerID);
    void SpawnRandomItem();
    void SpawnBombProjectile();
    float PointToLineSegmentDistance(glm::vec3 p, glm::vec3 a, glm::vec3 b);
    void SpawnDeathSplat(glm::vec3 pos, glm::vec3 color);
    void EndGame();

private:
    void UpdateActiveMatch(float dt);
    void UpdateFinishedMatch(float dt);
    void UpdateVisualEntities(float dt);
    void HandleLocalAbilityRequests();
    void UpdateItems(float dt);
    void UpdateRemotePlayers(float dt);
    void UpdateNetworkSync(float dt);
    void BroadcastScoreState();
    void RenderMapLayers(Shader& shader);
    void RenderEntities(Shader& shader);
    void RenderShadows(Shader& shader);
    void RenderParticles(Camera* cam);
    void HandleServerPacket(const ReceivedPacket& received, NetworkManager& net);
    void HandleSharedPacket(const ReceivedPacket& received, NetworkManager& net);
    void SyncLocalPlayerState(NetworkManager& net);
    void SyncAiState(NetworkManager& net);
    void SpawnRemoteProjectile(const PacketShoot& pkt);
    void HandleWorldState(const PacketPlayerState& pkt);
    void RefreshProjectileCollisionTargets();
    bool HandleProjectileEntityCollision(Projectile* projectile);
    bool HandleProjectileObstacleCollision(Projectile* projectile, float mapSize, float inkMultiplier);
    bool HandleRocketProjectile(Projectile* projectile, float mapSize);
    bool HandleBombProjectile(Projectile* projectile);
    void PaintRocketExplosion(Projectile* projectile, float mapSize);
    void ApplyRocketExplosionDamage(Projectile* projectile);
    void PaintBombExplosion(Projectile* projectile, float mapSize);
    void ApplyBombExplosionDamage(Projectile* projectile);
    bool HandleDefaultProjectile(Projectile* projectile, float mapSize, float inkMultiplier);
    void UpdateProjectiles(float dt);
};
