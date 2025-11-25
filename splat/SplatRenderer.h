#pragma once
#include "SplatMap.h"
#include "../engine/rendering/Shader.h"
#include "../scene/FloorMesh.h"

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