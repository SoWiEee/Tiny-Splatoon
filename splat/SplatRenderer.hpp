#pragma once
#include "SplatMap.hpp"
#include "../engine/rendering/Shader.hpp"
#include "../scene/FloorMesh.hpp"

class SplatRenderer {
public:
    static void RenderFloor(Shader& shader, FloorMesh* floor, SplatMap* map) {
        map->BindTexture(1);
        shader.SetInt("inkMap", 1);
        shader.SetInt("useInk", 1);

        floor->Draw(shader);
        shader.SetInt("useInk", 0);
    }
};