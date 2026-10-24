#pragma once
#include <glm/glm.hpp>
#include <array>
#include <vector>
namespace restoration {
struct LobbyCamera { glm::dvec3 position{0},target{0};double fov=32,nearPlane=.1,farPlane=140; };
struct LobbySpot { glm::dvec3 position{0};double yaw=0; };
struct LobbyPath { std::vector<glm::dvec3> points; };
struct LobbyLayout {
    LobbyCamera lobbyCamera,hubCamera;
    LobbySpot hubSpot;
    std::array<LobbySpot,8> lineupSpots;
    std::array<LobbyPath,8> arrivalPaths,exitPaths;
};
}
