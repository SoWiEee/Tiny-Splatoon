#pragma once
#include "Component.h"
#include <glm/gtc/matrix_transform.hpp>

class Transform : public Component {
public:
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f); // Euler Angles
    glm::vec3 scale = glm::vec3(1.0f);

    // odel Matrix
    glm::mat4 GetModelMatrix() {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        model = glm::scale(model, scale);
        return model;
    }

    glm::vec3 GetForward() {
        glm::vec3 front;
        front.x = cos(glm::radians(rotation.y)) * cos(glm::radians(rotation.x));
        front.y = sin(glm::radians(rotation.x));
        front.z = sin(glm::radians(rotation.y)) * cos(glm::radians(rotation.x));
        return glm::normalize(front);
    }

    glm::vec3 GetRight() {
        return glm::normalize(glm::cross(GetForward(), glm::vec3(0, 1, 0)));
    }
};