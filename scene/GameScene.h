#pragma once
#include <memory>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "../engine/scene/Scene.h"
#include "../engine/scene/SceneManager.h"
#include "../engine/GameObject.h"
#include "../engine/core/Input.h"
#include "../gameplay/GameWorld.h"
#include "../components/Camera.h"
#include "../components/HUD.h"
#include "../components/Scoreboard.h"

// «e¦V«Å§i
class LoginScene;

class GameScene : public Scene {

public:
    std::unique_ptr<GameWorld> world;
    std::unique_ptr<GameObject> cameraObj;
    std::unique_ptr<GameObject> uiObj;
    HUD* hud = nullptr;
    Scoreboard* scoreboard = nullptr;
    std::unique_ptr<Shader> shader;
    static Camera* CurrentCamera;

    GameScene() {}

    virtual ~GameScene() { OnExit(); }

    // enter scene
    void OnEnter() override;

    // exit scene
    void OnExit() override;

    // game update
    void Update(float dt) override;

    // Render loop
    void Render() override;

    void DrawUI() override;

    // packet handle
    void OnPacket(const ReceivedPacket& pkt) override;

private:
    bool isExited = false;
};