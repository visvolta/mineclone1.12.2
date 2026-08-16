#include "rendering/Camera.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

Camera::Camera(glm::vec3 position) : position_(position) {
    updateVectors();
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position_, position_ + front_, up_);
}

void Camera::look(float xOffset, float yOffset) {
    yaw_ += xOffset * mouseSensitivity_;
    pitch_ += yOffset * mouseSensitivity_;
    pitch_ = std::clamp(pitch_, -89.0F, 89.0F);
    updateVectors();
}

void Camera::updateVectors() {
    const float yawRadians = glm::radians(yaw_);
    const float pitchRadians = glm::radians(pitch_);

    glm::vec3 front;
    front.x = std::cos(yawRadians) * std::cos(pitchRadians);
    front.y = std::sin(pitchRadians);
    front.z = std::sin(yawRadians) * std::cos(pitchRadians);

    front_ = glm::normalize(front);
    right_ = glm::normalize(glm::cross(front_, worldUp_));
    up_ = glm::normalize(glm::cross(right_, front_));
}
