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
        if (!baseSystem.tesseract) return;
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
        if (!baseSystem.tesseract || !baseSystem.level || baseSystem.level->worlds.empty()) return;
        TesseractContext& tesseract = *baseSystem.tesseract;
        Entity& activeWorld = baseSystem.level->worlds[baseSystem.level->activeWorldIndex];

        const float rotationAmount = glm::radians(90.0f);
        
        for (const auto& inst : activeWorld.instances) {
            const auto& proto = prototypes[inst.prototypeID];
            if (proto.audicleType == "true") {
                if (proto.name == "TESS_FORWARD")  tesseract.targetAngleXW += rotationAmount;
                if (proto.name == "TESS_BACKWARD") tesseract.targetAngleXW -= rotationAmount;
                if (proto.name == "TESS_LEFT")     tesseract.targetAngleZW += rotationAmount;
                if (proto.name == "TESS_RIGHT")    tesseract.targetAngleZW -= rotationAmount;
                if (proto.name == "TESS_UP")       tesseract.targetAngleYW += rotationAmount;
                if (proto.name == "TESS_DOWN")     tesseract.targetAngleYW -= rotationAmount;
            }
        }

        const float animSpeed = 5.0f;
        tesseract.angleXW += (tesseract.targetAngleXW - tesseract.angleXW) * animSpeed * dt;
        tesseract.angleYW += (tesseract.targetAngleYW - tesseract.angleYW) * animSpeed * dt;
        tesseract.angleZW += (tesseract.targetAngleZW - tesseract.angleZW) * animSpeed * dt;
    }

    void RenderTesseract(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.tesseract || !baseSystem.player || !baseSystem.level || baseSystem.level->worlds.empty()) return;
        TesseractContext& tesseract = *baseSystem.tesseract; 
        PlayerContext& player = *baseSystem.player;
        Entity& activeWorld = baseSystem.level->worlds[baseSystem.level->activeWorldIndex];
        
        bool tesseractIsActive = false;
        for(const auto& inst : activeWorld.instances) {
            if(prototypes[inst.prototypeID].isTesseract) {
                tesseractIsActive = true;
                break;
            }
        }
        if(!tesseractIsActive) return;

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
        glm::mat4 model = glm::inverse(player.viewMatrix);
        model = glm::scale(model, glm::vec3(20.0f * aspectRatio, 20.0f, 20.0f));

        tesseract.shader->use();
        tesseract.shader->setMat4("model", model);
        tesseract.shader->setMat4("view", player.viewMatrix);
        tesseract.shader->setMat4("projection", player.projectionMatrix);
        tesseract.shader->setVec3("color", {0.8f, 0.1f, 0.3f});
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
