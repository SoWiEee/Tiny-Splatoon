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

const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Mouse Callback
Camera* mainCamera = nullptr;
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (mainCamera) mainCamera->ProcessMouseMovement((float)xpos, (float)ypos);
}

// 產生一個簡單的圓形筆刷貼圖
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

    // Create Shader
    Shader shader("assets/shaders/default.vert", "assets/shaders/default.frag");

    // Scene Graph
    std::vector<GameObject*> scene;
    std::vector<GameObject*> projectiles;

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

    // --- 玩家 (Camera) ---
    GameObject* playerObj = new GameObject("Player");
    playerObj->transform->position = glm::vec3(0.0f, 2.5f, 5.0f);
    GameObject* playerBody = new GameObject("PlayerBody");
    playerBody->AddComponent<MeshRenderer>("Cube", glm::vec3(1.0f, 1.0f, 0.0f)); // 黃色的人
    scene.push_back(playerBody);
    PlayerController* controller = playerObj->AddComponent<PlayerController>();
    controller->Setup(globalInkMap, playerBody, 1); // 紅隊

    InkShooter* shooter = playerObj->AddComponent<InkShooter>(nullptr, hud, globalInkMap, brushTexID);

    GameObject* cameraObj = new GameObject("MainCamera");
    mainCamera = cameraObj->AddComponent<Camera>();
    shooter->camera = mainCamera;
    scene.push_back(playerObj);

    // Render Loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        glm::vec3 targetPos = playerObj->transform->position;

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // 簡單 TPS 算法
        float camDist = 5.0f;
        float camHeight = 2.0f;
        glm::vec3 camDir = cameraObj->transform->GetForward();
        cameraObj->transform->position = targetPos - (camDir * camDist) + glm::vec3(0, camHeight, 0);
        playerObj->transform->rotation.y = cameraObj->transform->rotation.y;
        playerObj->transform->rotation.x = 0.0f; // 保持直立
        playerObj->transform->rotation.z = 0.0f;
        controller->ProcessInput(window, deltaTime,
            cameraObj->transform->GetForward(),
            cameraObj->transform->GetRight());

        shooter->ProcessInput(window, deltaTime);

        // 生成新子彈
        for (const auto& req : shooter->pendingShots) {
            GameObject* bulletObj = new GameObject("Bullet");

            // 設定位置：從槍口出發
            bulletObj->transform->position = req.position;
            bulletObj->transform->scale = glm::vec3(0.1f);

            // 加入紅色方塊
            bulletObj->AddComponent<MeshRenderer>("Cube", glm::vec3(1.0f, 0.0f, 0.0f));

            glm::vec3 velocity = req.direction * 20.0f;
            velocity.y += 2.0f;

            bulletObj->AddComponent<InkProjectile>(velocity, glm::vec3(1, 0, 0), globalInkMap, brushTexID);
            projectiles.push_back(bulletObj);
        }
        shooter->pendingShots.clear();

        // 2. 更新子彈位置 & 刪除死掉的子彈
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

        // --- Render ---
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();
        shader.setMat4("view", mainCamera->GetViewMatrix());
        shader.setMat4("projection", mainCamera->projection);
        shader.setInt("inkMap", 1);
        globalInkMap->BindTexture(1);

        for (auto go : scene) {
            if (go->name == "Floor") {
                shader.setInt("useInk", 1);
            }
            else {
                shader.setInt("useInk", 0);
            }
            go->Draw(shader);
        }
        shader.setInt("useInk", 0);
        for (auto bullet : projectiles) bullet->Draw(shader);

        hudObj->Draw(shader);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}