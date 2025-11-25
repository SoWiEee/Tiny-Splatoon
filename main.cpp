#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>

#include "graphics/Shader.h"
#include "graphics/InkMap.h"
#include "engine/core/Window.h"
#include "engine/core/Timer.h"
#include "engine/core/Input.h"
#include "engine/core/Logger.h"
#include "engine/GameObject.h"
#include "components/MeshRenderer.h"
#include "components/Camera.h"
#include "components/InkShooter.h"
#include "components/HUD.h"
#include "components/InkProjectile.h"
#include "components/PlayerController.h"
#include "components/Scoreboard.h"
#include "components/SimpleAI.h"
#include "components/Health.h"

const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;
float deltaTime = 0.0f;
float lastFrame = 0.0f;
Camera* mainCamera = nullptr;

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (mainCamera) mainCamera->ProcessMouseMovement((float)xpos, (float)ypos);
}

unsigned int CreateBrushTexture() {
    const int SIZE = 64; // 64x64 像素
    std::vector<unsigned char> data(SIZE * SIZE * 4); // RGBA

    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            float u = (float)x / (SIZE - 1) * 2.0f - 1.0f;
            float v = (float)y / (SIZE - 1) * 2.0f - 1.0f;
            float dist = sqrt(u * u + v * v);

            float alpha = 1.0f - glm::smoothstep(0.5f, 1.0f, dist);

            int index = (y * SIZE + x) * 4;
            data[index + 0] = 255; // R
            data[index + 1] = 255; // G
            data[index + 2] = 255; // B
            data[index + 3] = (unsigned char)(alpha * 255); // A
        }
    }

    unsigned int texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SIZE, SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return texID;
}

// AABB
bool CheckCollision(GameObject* one, GameObject* two) {
    // 取得位置
    glm::vec3 pos1 = one->transform->position;
    glm::vec3 pos2 = two->transform->position;

    // 取得縮放 (假設是方塊的大小)
    glm::vec3 size1 = one->transform->scale;
    glm::vec3 size2 = two->transform->scale;

    // AABB 碰撞公式:
    // x軸重疊 && y軸重疊 && z軸重疊
    bool collisionX = pos1.x + size1.x / 2 >= pos2.x - size2.x / 2 &&
        pos2.x + size2.x / 2 >= pos1.x - size1.x / 2;

    bool collisionY = pos1.y + size1.y / 2 >= pos2.y - size2.y / 2 &&
        pos2.y + size2.y / 2 >= pos1.y - size1.y / 2;

    bool collisionZ = pos1.z + size1.z / 2 >= pos2.z - size2.z / 2 &&
        pos2.z + size2.z / 2 >= pos1.z - size1.z / 2;

    return collisionX && collisionY && collisionZ;
}

int main() {
    Window window(1280, 720, "Splatoon Engine");
    Timer timer;
    glfwSetCursorPosCallback(window.GetNativeWindow(), mouse_callback);

    glEnable(GL_DEPTH_TEST);


    unsigned int brushTexID = CreateBrushTexture();
    InkMap* globalInkMap = new InkMap(1024, 1024);
    Shader shader("assets/shaders/default.vert", "assets/shaders/default.frag");

    std::vector<GameObject*> scene;
    std::vector<GameObject*> projectiles;
    std::vector<InkShooter*> activeShooters;
    std::vector<GameObject*> damageableObjects;

    // --- 地板 (Floor) ---
    GameObject* floorObj = new GameObject("Floor");
    floorObj->transform->position = glm::vec3(0.0f, 0.0f, 0.0f);
    floorObj->transform->scale = glm::vec3(40.0f, 1.0f, 40.0f);
    floorObj->AddComponent<MeshRenderer>("Plane", glm::vec3(0.8f, 0.8f, 0.8f));
    scene.push_back(floorObj);

    // --- 四面牆壁 ---
    // 牆壁厚度 1.0，高度 5.0，長度 20.0
    struct WallConfig { glm::vec3 pos; glm::vec3 scale; };
    std::vector<WallConfig> walls = {
        {{ 0.0f, 2.5f, -20.5f}, {40.0f, 5.0f, 1.0f}}, // 北
        {{ 0.0f, 2.5f,  20.5f}, {40.0f, 5.0f, 1.0f}}, // 南
        {{-20.5f, 2.5f,  0.0f}, {1.0f, 5.0f, 40.0f}}, // 西
        {{ 20.5f, 2.5f,  0.0f}, {1.0f, 5.0f, 40.0f}}  // 東
    };

    for (const auto& w : walls) {
        GameObject* wall = new GameObject("Wall");
        wall->transform->position = w.pos;
        wall->transform->scale = w.scale;
        // 牆壁用深灰色
        wall->AddComponent<MeshRenderer>("Cube", glm::vec3(0.3f, 0.3f, 0.3f));
        scene.push_back(wall);
    }

    // --- 障礙物 (Box) ---
    GameObject* boxObj = new GameObject("Box");
    boxObj->transform->position = glm::vec3(2.0f, 1.0f, -2.0f); // 浮空一點
    boxObj->AddComponent<MeshRenderer>("Cube", glm::vec3(1.0f, 0.5f, 0.2f)); // 橘色
    scene.push_back(boxObj);

    // --- HUD ---
    GameObject* hudObj = new GameObject("HUD");
    HUD* hud = hudObj->AddComponent<HUD>((float)SCR_WIDTH, (float)SCR_HEIGHT);

    // --- [NEW] Scoreboard ---
    GameObject* scoreObj = new GameObject("Scoreboard");
    Scoreboard* scoreboard = scoreObj->AddComponent<Scoreboard>((float)SCR_WIDTH, (float)SCR_HEIGHT, globalInkMap);

    // ==========================================
    // 1. 玩家設定 (Team 1: Red)
    // ==========================================
    GameObject* playerObj = new GameObject("Player");
    playerObj->transform->position = glm::vec3(-5.0f, 2.5f, -5.0f); // 出生點 A

    // 玩家模型 (黃色)
    GameObject* playerBody = new GameObject("PlayerBody");
    playerBody->AddComponent<MeshRenderer>("Cube", glm::vec3(1.0f, 1.0f, 0.0f));
    scene.push_back(playerBody);

    PlayerController* controller = playerObj->AddComponent<PlayerController>();
    controller->Setup(globalInkMap, playerBody, 1, hud);

    InkShooter* playerShooter = playerObj->AddComponent<InkShooter>(nullptr, hud, globalInkMap, brushTexID);
    playerShooter->SetColor(glm::vec3(1.0f, 0.0f, 0.0f)); // 紅色墨水
    activeShooters.push_back(playerShooter); // 加入管理清單
    Health* playerHP = playerObj->AddComponent<Health>(1, glm::vec3(-5.0f, 2.5f, -5.0f));
    damageableObjects.push_back(playerObj);

    GameObject* cameraObj = new GameObject("MainCamera");
    mainCamera = cameraObj->AddComponent<Camera>();
    playerShooter->camera = mainCamera;

    scene.push_back(playerObj);



    // ==========================================
    // 2. AI 敵人設定 (Team 2: Green)
    // ==========================================
    GameObject* enemyObj = new GameObject("Enemy");
    enemyObj->transform->position = glm::vec3(5.0f, 2.0f, 5.0f); // 出生點 B

    // 敵人模型 (綠色)
    enemyObj->AddComponent<MeshRenderer>("Cube", glm::vec3(0.0f, 1.0f, 0.0f));

    // AI 射擊器 (沒有 Camera, 沒有 HUD)
    InkShooter* enemyShooter = enemyObj->AddComponent<InkShooter>(nullptr, nullptr, globalInkMap, brushTexID);
    enemyShooter->SetColor(glm::vec3(0.0f, 1.0f, 0.0f)); // 綠色墨水
    activeShooters.push_back(enemyShooter); // 加入管理清單
    Health* enemyHP = enemyObj->AddComponent<Health>(2, glm::vec3(5.0f, 2.0f, 5.0f));
    damageableObjects.push_back(enemyObj);

    SimpleAI* ai = enemyObj->AddComponent<SimpleAI>();
    ai->Setup(enemyShooter);
    scene.push_back(enemyObj);


    // Render Loop
    while (!window.ShouldClose()) {

        timer.Tick();
        float dt = timer.GetDeltaTime();
        if (Input::GetKey(GLFW_KEY_ESCAPE)) break;

        // TPS Camera Logic
        glm::vec3 targetPos = playerObj->transform->position;
        float camDist = 5.0f;
        float camHeight = 2.0f;
        glm::vec3 camDir = cameraObj->transform->GetForward();
        cameraObj->transform->position = targetPos - (camDir * camDist) + glm::vec3(0, camHeight, 0);

        playerObj->transform->rotation.y = cameraObj->transform->rotation.y;
        playerObj->transform->rotation.x = 0.0f;
        playerObj->transform->rotation.z = 0.0f;

        controller->ProcessInput(dt,
            cameraObj->transform->GetForward(),
            cameraObj->transform->GetRight());

        // 玩家輸入射擊
        playerShooter->ProcessInput(dt);

        // ==========================================
        // 統一處理所有射擊器的子彈生成
        // ==========================================
        for (InkShooter* shooter : activeShooters) {
            for (const auto& req : shooter->pendingShots) {
                GameObject* bulletObj = new GameObject("Bullet");

                bulletObj->transform->position = req.position;
                bulletObj->transform->scale = glm::vec3(0.1f);

                // 子彈顏色跟墨水顏色一樣
                bulletObj->AddComponent<MeshRenderer>("Cube", shooter->inkColor);

                glm::vec3 velocity = req.direction * 20.0f;
                velocity.y += 2.0f;

                int teamID = (shooter->inkColor.x > 0.5f) ? 1 : 2;
                bulletObj->AddComponent<InkProjectile>(velocity, shooter->inkColor, globalInkMap, brushTexID, teamID);
                projectiles.push_back(bulletObj);
            }
            shooter->pendingShots.clear();
        }

        // 子彈物理
        for (auto it = projectiles.begin(); it != projectiles.end(); ) {
            GameObject* bullet = *it;
            bullet->Update(dt);
            InkProjectile* proj = bullet->GetComponent<InkProjectile>();

            bool hitSomething = false;

            // [NEW] 檢查是否撞到人
            if (proj && !proj->isDead) {
                for (GameObject* target : damageableObjects) {
                    // 1. 取得目標血量組件
                    Health* hp = target->GetComponent<Health>();
                    if (!hp) continue;

                    // 2. 隊伍檢查 (不能打隊友)
                    if (hp->teamID == proj->ownerTeam) continue;

                    // 3. 碰撞檢測
                    if (CheckCollision(bullet, target)) {
                        // 擊中！
                        std::cout << "Hit! Bullet Team " << proj->ownerTeam << " hit Team " << hp->teamID << std::endl;

                        // 扣血 (例如一發 20 分，5發死)
                        hp->TakeDamage(20.0f);

                        // 子彈銷毀
                        proj->isDead = true;
                        hitSomething = true;

                        // [視覺效果建議] 可以在這裡生成一個"爆炸特效"物件
                        break; // 一顆子彈只能打一個人
                    }
                }
            }

            // 清理子彈 (撞地板 or 撞人)
            if (proj && proj->isDead) {
                delete bullet;
                it = projectiles.erase(it);
            }
            else {
                ++it;
            }
        }

        // Update Scene
        for (auto go : scene) go->Update(dt);
        scoreboard->Update(deltaTime);
        hudObj->Update(deltaTime);
        scoreObj->Update(deltaTime);

        // --- Render ---
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();
        shader.setMat4("view", mainCamera->GetViewMatrix());
        shader.setMat4("projection", mainCamera->projection);
        shader.setInt("inkMap", 1);
        globalInkMap->BindTexture(1);

        // 畫場景
        for (auto go : scene) {
            if (go->name == "Floor") shader.setInt("useInk", 1);
            else shader.setInt("useInk", 0);
            go->Draw(shader);
        }

        shader.setInt("useInk", 0);
        for (auto bullet : projectiles) bullet->Draw(shader);

        hudObj->Draw(shader);
        scoreObj->Draw(shader);

        window.SwapBuffers();
        window.PollEvents();
    }

    glfwTerminate();
    return 0;
}