#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>

#include "graphics/Shader.h"
#include "graphics/InkMap.h"
#include "engine/GameObject.h"
#include "components/MeshRenderer.h"
#include "components/Camera.h"
#include "components/InkShooter.h"
#include "components/HUD.h"
#include "components/InkProjectile.h"
#include "components/PlayerController.h"
#include "components/Scoreboard.h"
#include "components/SimpleAI.h"

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

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Splatoon OpenGL Project", NULL, NULL);
    if (window == NULL) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    unsigned int brushTexID = CreateBrushTexture();
    InkMap* globalInkMap = new InkMap(1024, 1024);
    Shader shader("assets/shaders/default.vert", "assets/shaders/default.frag");

    std::vector<GameObject*> scene;
    std::vector<GameObject*> projectiles;
    std::vector<InkShooter*> activeShooters;

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

    SimpleAI* ai = enemyObj->AddComponent<SimpleAI>();
    ai->Setup(enemyShooter);
    scene.push_back(enemyObj);

    // Render Loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Input
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // TPS Camera Logic
        glm::vec3 targetPos = playerObj->transform->position;
        float camDist = 5.0f;
        float camHeight = 2.0f;
        glm::vec3 camDir = cameraObj->transform->GetForward();
        cameraObj->transform->position = targetPos - (camDir * camDist) + glm::vec3(0, camHeight, 0);

        playerObj->transform->rotation.y = cameraObj->transform->rotation.y;
        playerObj->transform->rotation.x = 0.0f;
        playerObj->transform->rotation.z = 0.0f;

        controller->ProcessInput(window, deltaTime,
            cameraObj->transform->GetForward(),
            cameraObj->transform->GetRight());

        // 玩家輸入射擊
        playerShooter->ProcessInput(window, deltaTime);

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

                // 傳入 shooter->inkColor
                bulletObj->AddComponent<InkProjectile>(velocity, shooter->inkColor, globalInkMap, brushTexID);
                projectiles.push_back(bulletObj);
            }
            shooter->pendingShots.clear();
        }

        // 子彈物理
        for (auto it = projectiles.begin(); it != projectiles.end(); ) {
            GameObject* bullet = *it;
            bullet->Update(deltaTime);
            InkProjectile* proj = bullet->GetComponent<InkProjectile>();
            if (proj && proj->isDead) {
                delete bullet;
                it = projectiles.erase(it);
            }
            else {
                ++it;
            }
        }

        // --- Update Scene ---
        for (auto go : scene) go->Update(deltaTime);
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

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}