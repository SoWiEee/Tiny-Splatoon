#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include "Mesh.hpp"

class ModelLoader {
public:
    // 讀取 OBJ 檔案並回傳 Mesh
    static std::shared_ptr<Mesh> LoadOBJ(const std::string& path) {
        std::vector<glm::vec3> temp_positions;
        std::vector<glm::vec2> temp_uvs;
        std::vector<glm::vec3> temp_normals;

        std::vector<Vertex> finalVertices;
        std::vector<unsigned int> finalIndices;

        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "Failed to open model: " << path << std::endl;
            return nullptr;
        }

        std::string line;
        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string prefix;
            ss >> prefix;

            if (prefix == "v") { // 頂點位置
                glm::vec3 pos;
                ss >> pos.x >> pos.y >> pos.z;
                temp_positions.push_back(pos);
            }
            else if (prefix == "vt") { // 貼圖座標
                glm::vec2 uv;
                ss >> uv.x >> uv.y;
                temp_uvs.push_back(uv);
            }
            else if (prefix == "vn") { // 法線
                glm::vec3 norm;
                ss >> norm.x >> norm.y >> norm.z;
                temp_normals.push_back(norm);
            }
            else if (prefix == "f") { // 面 (索引)
                // OBJ 格式: f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3
                std::string vertexStr;
                while (ss >> vertexStr) {
                    unsigned int vIdx = 0, vtIdx = 0, vnIdx = 0;

                    // 解析 1/1/1 這種字串
                    size_t firstSlash = vertexStr.find('/');
                    size_t secondSlash = vertexStr.find('/', firstSlash + 1);

                    vIdx = std::stoi(vertexStr.substr(0, firstSlash)) - 1;

                    if (firstSlash != std::string::npos && secondSlash != std::string::npos) {
                        // 有 UV 也有 Normal
                        std::string uvStr = vertexStr.substr(firstSlash + 1, secondSlash - firstSlash - 1);
                        if (!uvStr.empty()) vtIdx = std::stoi(uvStr) - 1;
                        vnIdx = std::stoi(vertexStr.substr(secondSlash + 1)) - 1;
                    }

                    // 組合 Vertex
                    Vertex vert;
                    vert.Position = temp_positions[vIdx];
                    if (!temp_uvs.empty()) vert.TexCoords = temp_uvs[vtIdx];
                    if (!temp_normals.empty()) vert.Normal = temp_normals[vnIdx];

                    // 簡單處理：直接塞進去 (不做索引優化以保持程式碼簡單)
                    finalVertices.push_back(vert);
                    finalIndices.push_back(finalIndices.size());
                }
            }
        }

        std::cout << "Loaded Model: " << path << " (Verts: " << finalVertices.size() << ")" << std::endl;
        return std::make_shared<Mesh>(finalVertices, finalIndices);
    }
};