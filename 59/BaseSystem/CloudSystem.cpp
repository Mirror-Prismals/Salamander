#pragma once

namespace CloudSystemLogic {

    void RenderClouds(BaseSystem& baseSystem, const glm::vec3& lightDir, float time) {
        if (!baseSystem.renderer || !baseSystem.player || !baseSystem.world) return;
        RendererContext& renderer = *baseSystem.renderer;
        PlayerContext& player = *baseSystem.player;

        if (!renderer.cloudShader) {
            if (!renderer.godrayQuadVAO) {
                float quadVerts[] = { -1,-1, 1,-1, -1,1, -1,1, 1,-1, 1,1 };
                glGenVertexArrays(1, &renderer.cloudVAO);
                glGenBuffers(1, &renderer.cloudVBO);
                glBindVertexArray(renderer.cloudVAO);
                glBindBuffer(GL_ARRAY_BUFFER, renderer.cloudVBO);
                glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
            }
            renderer.cloudShader = std::make_unique<Shader>(
                baseSystem.world->shaders["CLOUD_VERTEX_SHADER"].c_str(),
                baseSystem.world->shaders["CLOUD_FRAGMENT_SHADER"].c_str()
            );
        }

        if (!renderer.cloudShader) return;

        glm::mat4 view = player.viewMatrix;
        glm::mat4 projection = player.projectionMatrix;
        glm::mat4 invVP = glm::inverse(projection * view);
        glm::vec3 playerPos = player.cameraPosition;

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        renderer.cloudShader->use();
        renderer.cloudShader->setMat4("invVP", invVP);
        renderer.cloudShader->setMat4("view", view);
        renderer.cloudShader->setMat4("proj", projection);
        renderer.cloudShader->setVec3("cameraPos", playerPos);
        renderer.cloudShader->setVec3("sunDir", lightDir);
        renderer.cloudShader->setFloat("time", time);
        renderer.cloudShader->setFloat("cloudBase", 1600.0f);
        renderer.cloudShader->setFloat("cloudThickness", 900.0f);
        renderer.cloudShader->setFloat("cloudScale", 1.0f);
        renderer.cloudShader->setInt("steps", 32);
        renderer.cloudShader->setFloat("densityMultiplier", 0.005f);
        renderer.cloudShader->setFloat("lightMultiplier", 1.0f);
        renderer.cloudShader->setFloat("cloudRadius", 12000.0f);
        renderer.cloudShader->setFloat("fadeBand", 1000.0f);
        renderer.cloudShader->setFloat("maxSkip", 7.0f);

        glBindVertexArray(renderer.cloudVAO ? renderer.cloudVAO : renderer.godrayQuadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

}
