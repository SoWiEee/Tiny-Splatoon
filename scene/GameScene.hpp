#pragma once
#include <memory>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "../engine/scene/Scene.hpp"
#include "../engine/scene/SceneManager.hpp"
#include "../engine/GameObject.hpp"
#include "../engine/core/Input.hpp"
#include "../gameplay/GameWorld.hpp"
#include "../components/Camera.hpp"
#include "../components/HUD.hpp"
#include "../components/Scoreboard.hpp"
#include "../gui/GUIManager.hpp"

// �e�V�ŧi
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
    GUIManager* gui = nullptr;

    explicit GameScene(GUIManager* guiManager) : gui(guiManager) {}

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
