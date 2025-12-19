#pragma once
#include "../Host.h"

namespace SkyboxSystemLogic {

    // Definition for getCurrentSkyColors now lives here
    void getCurrentSkyColors(float dayFraction, const std::vector<SkyColorKey>& skyKeys, glm::vec3& top, glm::vec3& bottom) {
        if (skyKeys.empty() || skyKeys.size() < 2) return;
        size_t i = 0;
        for (; i < skyKeys.size() - 1; i++) { if (dayFraction >= skyKeys[i].time && dayFraction <= skyKeys[i + 1].time) break; }
        if (i >= skyKeys.size() - 1) i = skyKeys.size() - 2;
        float t = (dayFraction - skyKeys[i].time) / (skyKeys[i + 1].time - skyKeys[i].time);
        top = glm::mix(skyKeys[i].top, skyKeys[i + 1].top, t);
        bottom = glm::mix(skyKeys[i].bottom, skyKeys[i + 1].bottom, t);
    }

    void RenderSkyAndCelestials(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, const std::vector<glm::vec3>& starPositions, float time, float dayFraction, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& playerPos, glm::vec3& outLightDir) {
        if (!baseSystem.renderer || !baseSystem.world) return;
        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;

        glm::vec3 skyTop, skyBottom;
        getCurrentSkyColors(dayFraction, world.skyKeys, skyTop, skyBottom);

        float hour = dayFraction * 24.0f;
        glm::vec3 sunDir, moonDir;
        { float u=(hour-6)/12.f; sunDir=glm::normalize(glm::vec3(0,sin(u*3.14159f),-cos(u*3.14159f))); }
        { float aH=(hour<6)?hour+24:hour; float u=(aH-18)/12.f; moonDir=glm::normalize(glm::vec3(0,sin(u*3.14159f),-cos(u*3.14159f))); }
        outLightDir = (hour>=6 && hour<18) ? sunDir : moonDir;

        // Screen-space light position for godrays
        glm::vec3 primaryWorldPos = playerPos + outLightDir * 500.0f;
        glm::vec4 clipPos = projection * view * glm::vec4(primaryWorldPos, 1.0f);
        glm::vec2 lightScreen = glm::vec2(0.5f) + glm::vec2(0.5f) * glm::vec2(clipPos.x / clipPos.w, clipPos.y / clipPos.w);

        // Occlusion pass (downsampled)
        GLint prevViewport[4]; glGetIntegerv(GL_VIEWPORT, prevViewport);
        glViewport(0, 0, renderer.godrayWidth, renderer.godrayHeight);
        glBindFramebuffer(GL_FRAMEBUFFER, renderer.godrayOcclusionFBO);
        glClearColor(0,0,0,0);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        renderer.sunMoonShader->use(); renderer.sunMoonShader->setMat4("v", view); renderer.sunMoonShader->setMat4("p", projection); renderer.sunMoonShader->setFloat("time", time);
        glm::mat4 sunM = glm::translate(glm::mat4(1),playerPos+sunDir*500.f); sunM = glm::scale(sunM,glm::vec3(42));
        renderer.sunMoonShader->setMat4("m",sunM); renderer.sunMoonShader->setVec3("c",glm::vec3(1,1,0.8f));
        glBindVertexArray(renderer.sunMoonVAO); glDrawArrays(GL_TRIANGLES,0,6);
        glm::mat4 moonM = glm::translate(glm::mat4(1),playerPos+moonDir*500.f); moonM = glm::scale(moonM,glm::vec3(42));
        renderer.sunMoonShader->setMat4("m",moonM); renderer.sunMoonShader->setVec3("c",glm::vec3(0.9f,0.9f,1));
        glDrawArrays(GL_TRIANGLES,0,6);

        // Radial blur
        glBindFramebuffer(GL_FRAMEBUFFER, renderer.godrayBlurFBO);
        glClearColor(0,0,0,0);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindVertexArray(renderer.godrayQuadVAO);
        renderer.godrayRadialShader->use();
        renderer.godrayRadialShader->setInt("occlusionTex", 0);
        renderer.godrayRadialShader->setVec2("lightPos", lightScreen);
        renderer.godrayRadialShader->setFloat("exposure", 0.6f);
        renderer.godrayRadialShader->setFloat("decay", 0.94f);
        renderer.godrayRadialShader->setFloat("density", 0.96f);
        renderer.godrayRadialShader->setFloat("weight", 0.3f);
        renderer.godrayRadialShader->setFloat("time", time);
        renderer.godrayRadialShader->setInt("samples", 64);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, renderer.godrayOcclusionTex);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Restore framebuffer/viewport
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

        // Skybox
        glDepthMask(GL_FALSE);
        renderer.skyboxShader->use();
        renderer.skyboxShader->setVec3("sT", skyTop);
        renderer.skyboxShader->setVec3("sB", skyBottom);
        glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
        renderer.skyboxShader->setMat4("projection", projection);
        renderer.skyboxShader->setMat4("view", viewNoTranslation);
        glBindVertexArray(renderer.skyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Stars
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        if (!starPositions.empty()) {
            glBindVertexArray(renderer.starVAO);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.starVBO);
            glBufferData(GL_ARRAY_BUFFER, starPositions.size() * sizeof(glm::vec3), starPositions.data(), GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),(void*)0);glEnableVertexAttribArray(0);
            renderer.starShader->use();
            renderer.starShader->setFloat("t", time);
            glm::mat4 viewNoTranslationStars = glm::mat4(glm::mat3(view));
            renderer.starShader->setMat4("v", viewNoTranslationStars);
            renderer.starShader->setMat4("p", projection);
            glEnable(GL_PROGRAM_POINT_SIZE);
            glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(starPositions.size()));
        }

        // Sun and moon main pass
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        renderer.sunMoonShader->use(); renderer.sunMoonShader->setMat4("v", view); renderer.sunMoonShader->setMat4("p", projection); renderer.sunMoonShader->setFloat("time", time);
        renderer.sunMoonShader->setMat4("m", sunM); renderer.sunMoonShader->setVec3("c", glm::vec3(1,1,0.8f));
        glBindVertexArray(renderer.sunMoonVAO); glDrawArrays(GL_TRIANGLES,0,6);
        renderer.sunMoonShader->setMat4("m", moonM); renderer.sunMoonShader->setVec3("c", glm::vec3(0.9f,0.9f,1));
        glDrawArrays(GL_TRIANGLES,0,6);
        glDisable(GL_BLEND);

        // Composite godrays additively
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glBindVertexArray(renderer.godrayQuadVAO);
        renderer.godrayCompositeShader->use();
        renderer.godrayCompositeShader->setInt("godrayTex", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, renderer.godrayBlurTex);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_BLEND);

        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

}
