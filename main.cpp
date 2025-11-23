#include "engine/GameObject.h"
#include "components/MeshRenderer.h"
#include "components/Camera.h"

int main() {
    // 1. 初始化 Window & OpenGL
    Window window(1280, 720, "Splatoon OpenGL");

    // 2. 建立 Shader
    Shader defaultShader("assets/shaders/default.vert", "assets/shaders/default.frag");

    // 3. 建立場景物件 (Scene Graph)
    std::vector<GameObject*> scene;

    // --- 地板 ---
    GameObject* floor = new GameObject();
    floor->AddComponent<MeshRenderer>("Plane");
    floor->transform->scale = glm::vec3(20.0f, 1.0f, 20.0f); // 放大 20 倍
    scene.push_back(floor);

    // --- 障礙物 ---
    GameObject* box = new GameObject();
    box->AddComponent<MeshRenderer>("Cube");
    box->transform->position = glm::vec3(0.0f, 0.5f, 0.0f); // 放在地上
    scene.push_back(box);

    // --- 玩家 (攝影機) ---
    GameObject* player = new GameObject();
    player->transform->position = glm::vec3(0, 2, 5);
    player->AddComponent<Camera>();
    // player->AddComponent<PlayerController>(); // 之後實作
    scene.push_back(player);

    // 4. Game Loop
    while (!window.ShouldClose()) {
        float dt = Time::GetDeltaTime();

        // Update Logic
        for (auto go : scene) go->Update(dt);

        // Render
        window.Clear();
        defaultShader.Use();

        // 設定 View/Projection Matrix (從 Camera 取得)
        Camera* cam = player->GetComponent<Camera>();
        defaultShader.SetMat4("view", cam->GetViewMatrix());
        defaultShader.SetMat4("projection", cam->GetProjectionMatrix());

        for (auto go : scene) go->Draw(defaultShader);

        window.SwapBuffers();
    }
}