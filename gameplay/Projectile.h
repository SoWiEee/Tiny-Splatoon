#pragma once
#include "../scene/Entity.h"
#include "../components/MeshRenderer.h"
#include <glm/glm.hpp>
#include <cstdlib>
#include <cmath>

class Projectile : public Entity {
public:
    glm::vec3 velocity;
    float gravity = 30.0f;

    int ownerTeam;
    int ownerID;
    glm::vec3 inkColor;
    bool isDead = false;

    bool hasHitFloor = false;
    glm::vec3 hitPosition;

    bool isBomb = false;
    float fuseTimer = 0.0f;     // 引信倒數
    bool hasExploded = false;   // 標記是否觸發爆炸

    // --- [新增] 壽命限制 (防止子彈飛到天荒地老) ---
    float lifeTime = 5.0f;

    Projectile(glm::vec3 startPos, glm::vec3 startVel, glm::vec3 color, int team, float scale, int owner, bool isBombItem = false)
        : Entity("Projectile"), velocity(startVel), inkColor(color), ownerTeam(team), ownerID(owner), isBomb(isBombItem)
    {
        transform->position = startPos;
        transform->scale = glm::vec3(scale);

        if (isBomb) {
            // --- 炸彈設定 ---
            gravity = 20.0f;    // 重力稍微調小一點，讓拋物線好看
            fuseTimer = 2.0f;   // 2秒後爆炸

            // 炸彈外觀：黑色方塊 (實際塗地顏色存在 inkColor)
            AddComponent<MeshRenderer>("Cube", glm::vec3(0.1f, 0.1f, 0.1f));
        }
        else {
            // --- 普通子彈設定 ---
            gravity = 30.0f;    // 下墜快
            lifeTime = 3.0f;    // 3秒後自動消失

            // 子彈外觀：球體，顏色為隊伍顏色
            AddComponent<MeshRenderer>("Sphere", color);
        }
    }

    void UpdatePhysics(float dt) {
        if (isDead) return;

        // 1. 壽命與引信管理
        if (isBomb) {
            fuseTimer -= dt;
            if (fuseTimer <= 0.0f) {
                isDead = true;
                hasExploded = true; // 觸發 GameWorld 的爆炸邏輯
                return;
            }
        }
        else {
            lifeTime -= dt;
            if (lifeTime <= 0.0f) {
                isDead = true;
                return;
            }
        }

        // 2. 物理運動
        velocity.y -= gravity * dt;
        transform->position += velocity * dt;

        // 3. 視覺旋轉
        if (isBomb) {
            // 炸彈：瘋狂旋轉
            transform->rotation.x += 720.0f * dt;
            transform->rotation.z += 360.0f * dt;
        }
        else {
            // 子彈：拉伸變形
            UpdateVisualDeformation();
        }

        // 4. 地板碰撞偵測
        // 這裡假設地板高度為 0 (或稍微高一點的判定容錯)
        if (transform->position.y <= 0.2f) { // 稍微提高判定點，避免穿模

            // 檢查是否真的在往下掉 (避免剛彈起來又被判定撞地)
            if (velocity.y < 0) {

                if (isBomb) {
                    // --- 炸彈：反彈邏輯 (Bounce) ---
                    // Y 軸速度反轉並衰減
                    velocity.y = -velocity.y * 0.5f; // 0.5 彈性係數

                    // 水平摩擦力 (讓它滾一滾停下來)
                    velocity.x *= 0.8f;
                    velocity.z *= 0.8f;

                    // 防止穿透地板
                    transform->position.y = 0.2f;

                    // 如果彈跳太微弱，就停止垂直運動，讓它在地上滾
                    if (std::abs(velocity.y) < 1.0f) velocity.y = 0.0f;

                    // 標記撞地 (GameWorld 可能需要用來播音效，但不要設 isDead)
                    hasHitFloor = true;
                    hitPosition = transform->position;
                }
                else {
                    // --- 普通子彈：死亡邏輯 ---
                    // 修正回地板位置，確保塗地準確
                    float timeOvershoot = 0.0f;
                    if (std::abs(velocity.y) > 0.001f) {
                        timeOvershoot = (transform->position.y - 0.0f) / velocity.y;
                    }
                    transform->position -= velocity * timeOvershoot;
                    transform->position.y = 0.0f;

                    hasHitFloor = true;
                    hitPosition = transform->position;
                    isDead = true; // 子彈撞地即死
                }
            }
        }

        // 5. 掉出地圖邊界
        if (transform->position.y < -10.0f) {
            isDead = true;
        }
    }

    void UpdateVisualDeformation() {
        // 只有普通子彈需要拉伸
        if (isBomb) return;

        // 1. 計算速度
        float speed = glm::length(velocity);

        // 2. 旋轉：讓子彈永遠「面朝」飛行方向
        if (speed > 0.1f) {
            // 為了防止 LookAt 出錯，我們加上現在位置
            transform->LookAt(transform->position + velocity);
        }

        // 3. 縮放：速度越快，Z 軸(前進方向)越長，XY 軸(寬度)越窄
        float baseScale = transform->scale.x;
        // 重新設定因為 Projectile 可能被重複使用或縮放
        // 這裡我們假設 baseScale 應該是建構時的值，這是一個簡化的寫法
        // 如果你的子彈大小會變，這裡可能要記住 initialScale
        // 暫時用 0.3f 當作基準，或者用 transform->scale.x (如果上一幀沒變太多的話)
        float defaultScale = 0.3f;

        float stretchFactor = 1.0f + (speed * 0.05f); // 係數改小一點，避免拉太長
        float squashFactor = 1.0f / sqrt(stretchFactor);

        transform->scale = glm::vec3(defaultScale * squashFactor, defaultScale * squashFactor, defaultScale * stretchFactor);
    }
};