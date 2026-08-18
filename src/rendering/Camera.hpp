#pragma once

#include <glm/glm.hpp>

class Camera {
public:
    explicit Camera(glm::vec3 position = glm::vec3(0.0F, 0.0F, 3.0F));

    [[nodiscard]] glm::mat4 viewMatrix() const;
    [[nodiscard]] const glm::vec3& position() const { return position_; }
    [[nodiscard]] const glm::vec3& front() const { return front_; }
    [[nodiscard]] float yaw() const { return yaw_; }
    [[nodiscard]] float pitch() const { return pitch_; }

    void setPosition(const glm::vec3& position) { position_ = position; }
    void setHurtEffect(float strengthDegrees, float attackedAtYaw) { hurtStrengthDegrees_ = strengthDegrees; attackedAtYaw_ = attackedAtYaw; }
    void look(float xOffset, float yOffset);

private:
    void updateVectors();

    glm::vec3 position_;
    glm::vec3 front_{0.0F, 0.0F, -1.0F};
    glm::vec3 up_{0.0F, 1.0F, 0.0F};
    glm::vec3 right_{1.0F, 0.0F, 0.0F};
    const glm::vec3 worldUp_{0.0F, 1.0F, 0.0F};

    float yaw_ = -90.0F;
    float pitch_ = 0.0F;
    float mouseSensitivity_ = 0.10F;
    float hurtStrengthDegrees_ = 0.0F;
    float attackedAtYaw_ = 0.0F;
};
