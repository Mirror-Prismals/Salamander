#pragma once
#include <glm/gtx/string_cast.hpp>

namespace TesseractSystemLogic {

    glm::mat4 rotationMatrix4D(float angle, int axis1, int axis2) {
        glm::mat4 R = glm::mat4(1.0f);
        float c = cos(angle); float s = sin(angle);
        R[axis1][axis1] = c; R[axis1][axis2] = -s;
        R[axis2][axis1] = s; R[axis2][axis2] = c;
        return R;
    }

    void InitializeTesseract(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.tesseract || !baseSystem.world) return;
        TesseractContext& tesseract = *baseSystem.tesseract;
        for (int i = 0; i < 16; ++i) { tesseract.vertices4D.push_back({ (i&1)?1.f:-1.f, (i&2)?1.f:-1.f, (i&4)?1.f:-1.f, (i&8)?1.f:-1.f }); }
        tesseract.indices = { 0,1,1,3,3,2,2,0, 4,5,5,7,7,6,6,4, 0,4,1,5,2,6,3,7, 8,9,9,11,11,10,10,8, 12,13,13,15,15,14,14,12, 8,12,9,13,10,14,11,15, 0,8,1,9,2,10,3,11, 4,12,5,13,6,14,7,15 };
        const char* vs = "#version 330 core\nlayout (location = 0) in vec3 aPos; uniform mat4 model, view, projection; void main() { gl_Position = projection * view * model * vec4(aPos, 1.0); }";
        const char* fs = "#version 330 core\nout vec4 FragColor; uniform vec3 color; void main() { FragColor = vec4(color, 0.75); }";
        tesseract.shader = std::make_unique<Shader>(vs, fs);
        glGenVertexArrays(1, &tesseract.VAO); glGenBuffers(1, &tesseract.VBO); glGenBuffers(1, &tesseract.EBO);
        glBindVertexArray(tesseract.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, tesseract.VBO); glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(glm::vec3), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tesseract.EBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, tesseract.indices.size() * sizeof(unsigned int), tesseract.indices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0); glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0); glBindVertexArray(0);
    }

    void UpdateTesseract(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.tesseract || !baseSystem.player) return;
        TesseractContext& tesseract = *baseSystem.tesseract;
        PlayerContext& player = *baseSystem.player;
        const float rotationAmount = glm::radians(90.0f);
        const float animSpeed = 5.0f;
        static bool keys_pressed_last_frame[6] = {false}; 
        bool current_keys[6] = {
            glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS && !(glfwGetKey(win, GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS),
            glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS && !(glfwGetKey(win, GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS),
            glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS && !(glfwGetKey(win, GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS),
            glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS && !(glfwGetKey(win, GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS),
            glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS && (glfwGetKey(win, GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS),
            glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS && (glfwGetKey(win, GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS)
        };
        if (current_keys[0] && !keys_pressed_last_frame[0]) tesseract.targetAngleXW += rotationAmount;
        if (current_keys[1] && !keys_pressed_last_frame[1]) tesseract.targetAngleXW -= rotationAmount;
        if (current_keys[2] && !keys_pressed_last_frame[2]) tesseract.targetAngleZW += rotationAmount;
        if (current_keys[3] && !keys_pressed_last_frame[3]) tesseract.targetAngleZW -= rotationAmount;
        if (current_keys[4] && !keys_pressed_last_frame[4]) tesseract.targetAngleYW += rotationAmount;
        if (current_keys[5] && !keys_pressed_last_frame[5]) tesseract.targetAngleYW -= rotationAmount;
        for(int i=0; i<6; ++i) keys_pressed_last_frame[i] = current_keys[i];
        tesseract.angleXW += (tesseract.targetAngleXW - tesseract.angleXW) * animSpeed * dt;
        tesseract.angleYW += (tesseract.targetAngleYW - tesseract.angleYW) * animSpeed * dt;
        tesseract.angleZW += (tesseract.targetAngleZW - tesseract.angleZW) * animSpeed * dt;
    }

    void RenderTesseract(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.tesseract || !baseSystem.player || !baseSystem.world) return;
        TesseractContext& tesseract = *baseSystem.tesseract; 
        PlayerContext& player = *baseSystem.player;
        
        glm::mat4 rotMatrix = glm::mat4(1.0f);
        rotMatrix *= rotationMatrix4D(tesseract.angleXW, 0, 3);
        rotMatrix *= rotationMatrix4D(tesseract.angleYW, 1, 3);
        rotMatrix *= rotationMatrix4D(tesseract.angleZW, 2, 3);
       
        std::vector<glm::vec3> vertices3D;
        float distance = 4.0f;
        for (const auto& v4 : tesseract.vertices4D) {
            vec4 rotated = rotMatrix * v4;
            float w = 1.0f / (distance - rotated.w);
            vertices3D.push_back({ rotated.x * w, rotated.y * w, rotated.z * w });
        }
        glBindBuffer(GL_ARRAY_BUFFER, tesseract.VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, vertices3D.size() * sizeof(glm::vec3), vertices3D.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        const float aspectRatio = 16.0f / 9.0f;
        
        // --- THIS IS THE CORRECT LOGIC FOR A COCKPIT VIEW ---
        // 1. Start with the camera's world transformation matrix (the inverse of the view matrix).
        // This places the tesseract exactly where the camera is and orients it the same way.
        glm::mat4 model = glm::inverse(player.viewMatrix);

        // 2. Apply a large scale to make it feel like you are inside it.
        model = glm::scale(model, glm::vec3(20.0f * aspectRatio, 20.0f, 20.0f));

        tesseract.shader->use();
        tesseract.shader->setMat4("model", model);
        tesseract.shader->setMat4("view", player.viewMatrix); // Use the NORMAL view matrix
        tesseract.shader->setMat4("projection", player.projectionMatrix);
        
        glm::vec3 rubyColor = {0.8f, 0.1f, 0.3f}; 
        tesseract.shader->setVec3("color", rubyColor);

        glLineWidth(2.0f);
        glBindVertexArray(tesseract.VAO);
        glDrawElements(GL_LINES, tesseract.indices.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
    
    void CleanupTesseract(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.tesseract) return;
        TesseractContext& tesseract = *baseSystem.tesseract;
        glDeleteVertexArrays(1, &tesseract.VAO); glDeleteBuffers(1, &tesseract.VBO); glDeleteBuffers(1, &tesseract.EBO);
    }
}
