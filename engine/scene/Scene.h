#pragma once
#include <string>
#include "../../network/NetworkProtocol.h"
#include "../../network/NetworkManager.h"

class Scene {
public:
    virtual ~Scene() {}

    virtual void OnEnter() = 0;

    virtual void OnExit() = 0;

    virtual void Update(float dt) = 0;

    virtual void Render() = 0;

    virtual void DrawUI() = 0;

    virtual void OnPacket(const ReceivedPacket& pkt) {}
};