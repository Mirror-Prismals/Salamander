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
        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.renderBackend) return;
        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        auto& renderBackend = *baseSystem.renderBackend;

        glm::vec3 skyTop, skyBottom;
        getCurrentSkyColors(dayFraction, world.skyKeys, skyTop, skyBottom);

        float hour = dayFraction * 24.0f;
        glm::vec3 sunDir, moonDir;
        { float u=(hour-6)/12.f; sunDir=glm::normalize(glm::vec3(0,sin(u*3.14159f),-cos(u*3.14159f))); }
        { float aH=(hour<6)?hour+24:hour; float u=(aH-18)/12.f; moonDir=glm::normalize(glm::vec3(0,sin(u*3.14159f),-cos(u*3.14159f))); }
        outLightDir = (hour>=6 && hour<18) ? sunDir : moonDir;

        // Screen-space light position for godrays.
        // Fade rays when the light source goes outside view to avoid searchlight-like sweeping.
        glm::vec3 primaryWorldPos = playerPos + outLightDir * 500.0f;
        glm::vec4 clipPos = projection * view * glm::vec4(primaryWorldPos, 1.0f);
        glm::vec2 lightScreen(0.5f, 0.5f);
        float godrayVisibility = 0.0f;
        if (clipPos.w > 0.0001f) {
            glm::vec2 ndc = glm::vec2(clipPos.x / clipPos.w, clipPos.y / clipPos.w);
            lightScreen = glm::vec2(0.5f) + glm::vec2(0.5f) * ndc;
            // Post-process UV space has opposite vertical direction from projected NDC here.
            lightScreen.y = 1.0f - lightScreen.y;
            const float fadeRange = 0.25f;
            float offX = glm::max(glm::abs(ndc.x) - 1.0f, 0.0f);
            float offY = glm::max(glm::abs(ndc.y) - 1.0f, 0.0f);
            float visX = 1.0f - glm::clamp(offX / fadeRange, 0.0f, 1.0f);
            float visY = 1.0f - glm::clamp(offY / fadeRange, 0.0f, 1.0f);
            godrayVisibility = glm::clamp(visX * visY, 0.0f, 1.0f);
        }

        // Use camera basis so the celestial quads stay truly view-facing.
        const glm::mat3 cameraBasis = glm::transpose(glm::mat3(view));
        auto billboard = [&](const glm::vec3& center, float scale)->glm::mat4{
            glm::mat4 orient(1.0f);
            orient[0] = glm::vec4(glm::normalize(cameraBasis[0]), 0.0f);
            orient[1] = glm::vec4(glm::normalize(cameraBasis[1]), 0.0f);
            orient[2] = glm::vec4(glm::normalize(cameraBasis[2]), 0.0f);
            return glm::translate(glm::mat4(1.0f), center) * orient * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
        };

        auto beginOffscreenPass = [&](RenderHandle framebuffer, int width, int height, float r, float g, float b, float a) {
            renderBackend.beginOffscreenColorPass(framebuffer, width, height, r, g, b, a);
        };
        auto endOffscreenPass = [&]() {
            renderBackend.endOffscreenColorPass();
        };
        auto bindTexture2DUnit = [&](int unit, RenderHandle texture) {
            renderBackend.bindTexture2D(texture, unit);
        };
        auto setDepthTestEnabled = [&](bool enabled) {
            renderBackend.setDepthTestEnabled(enabled);
        };
        auto setDepthWriteEnabled = [&](bool enabled) {
            renderBackend.setDepthWriteEnabled(enabled);
        };
        auto setBlendEnabled = [&](bool enabled) {
            renderBackend.setBlendEnabled(enabled);
        };
        auto setBlendModeAlpha = [&]() {
            renderBackend.setBlendModeAlpha();
        };
        auto setBlendModeAdditive = [&]() {
            renderBackend.setBlendModeAdditive();
        };
        auto setProgramPointSizeEnabled = [&](bool enabled) {
            renderBackend.setProgramPointSizeEnabled(enabled);
        };

        // Occlusion pass (downsampled)
        beginOffscreenPass(renderer.godrayOcclusionFBO, renderer.godrayWidth, renderer.godrayHeight, 0.0f, 0.0f, 0.0f, 0.0f);
        setDepthTestEnabled(false);
        setDepthWriteEnabled(false);
        setBlendEnabled(false);
        renderer.sunMoonShader->use(); renderer.sunMoonShader->setMat4("v", view); renderer.sunMoonShader->setMat4("p", projection); renderer.sunMoonShader->setFloat("time", time);
        glm::mat4 sunM = billboard(playerPos + sunDir * 500.0f, 42.0f);
        renderer.sunMoonShader->setMat4("m",sunM); renderer.sunMoonShader->setVec3("c",glm::vec3(1,1,0.8f));
        renderBackend.bindVertexArray(renderer.sunMoonVAO); renderBackend.drawArraysTriangles(0, 6);
        glm::mat4 moonM = billboard(playerPos + moonDir * 500.0f, 42.0f);
        renderer.sunMoonShader->setMat4("m",moonM); renderer.sunMoonShader->setVec3("c",glm::vec3(0.9f,0.9f,1));
        renderBackend.drawArraysTriangles(0, 6);

        endOffscreenPass();

        // Radial blur
        beginOffscreenPass(renderer.godrayBlurFBO, renderer.godrayWidth, renderer.godrayHeight, 0.0f, 0.0f, 0.0f, 0.0f);
        renderBackend.bindVertexArray(renderer.godrayQuadVAO);
        renderer.godrayRadialShader->use();
        renderer.godrayRadialShader->setInt("occlusionTex", 0);
        renderer.godrayRadialShader->setVec2("lightPos", lightScreen);
        renderer.godrayRadialShader->setFloat("exposure", 0.6f * godrayVisibility);
        renderer.godrayRadialShader->setFloat("decay", 0.94f);
        renderer.godrayRadialShader->setFloat("density", 0.96f);
        renderer.godrayRadialShader->setFloat("weight", 0.3f);
        renderer.godrayRadialShader->setFloat("time", time);
        renderer.godrayRadialShader->setInt("samples", 64);
        bindTexture2DUnit(0, renderer.godrayOcclusionTex);
        renderBackend.drawArraysTriangles(0, 6);

        // Restore framebuffer/viewport
        endOffscreenPass();

        // Skybox
        setDepthWriteEnabled(false);
        renderer.skyboxShader->use();
        renderer.skyboxShader->setVec3("sT", skyTop);
        renderer.skyboxShader->setVec3("sB", skyBottom);
        glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
        renderer.skyboxShader->setMat4("projection", projection);
        renderer.skyboxShader->setMat4("view", viewNoTranslation);
        renderBackend.bindVertexArray(renderer.skyboxVAO);
        renderBackend.drawArraysTriangles(0, 6);

        // Stars
        setDepthTestEnabled(false);
        setDepthWriteEnabled(false);
        if (!starPositions.empty()) {
            static const std::vector<VertexAttribLayout> kStarLayout = {
                {0, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(glm::vec3)), 0, 0}
            };
            renderBackend.uploadArrayBufferData(
                renderer.starVBO,
                starPositions.data(),
                starPositions.size() * sizeof(glm::vec3),
                true
            );
            renderBackend.configureVertexArray(renderer.starVAO, renderer.starVBO, kStarLayout, 0, {});
            renderBackend.bindVertexArray(renderer.starVAO);
            renderer.starShader->use();
            renderer.starShader->setFloat("t", time);
            glm::mat4 viewNoTranslationStars = glm::mat4(glm::mat3(view));
            renderer.starShader->setMat4("v", viewNoTranslationStars);
            renderer.starShader->setMat4("p", projection);
            setProgramPointSizeEnabled(true);
            renderBackend.drawArraysPoints(0, static_cast<int>(starPositions.size()));
            setProgramPointSizeEnabled(false);
        }

        // Sun and moon main pass
        setBlendEnabled(true);
        setBlendModeAlpha();
        renderer.sunMoonShader->use(); renderer.sunMoonShader->setMat4("v", view); renderer.sunMoonShader->setMat4("p", projection); renderer.sunMoonShader->setFloat("time", time);
        renderer.sunMoonShader->setMat4("m", sunM); renderer.sunMoonShader->setVec3("c", glm::vec3(1,1,0.8f));
        renderBackend.bindVertexArray(renderer.sunMoonVAO); renderBackend.drawArraysTriangles(0, 6);
        renderer.sunMoonShader->setMat4("m", moonM); renderer.sunMoonShader->setVec3("c", glm::vec3(0.9f,0.9f,1));
        renderBackend.drawArraysTriangles(0, 6);
        setBlendEnabled(false);

        // Composite godrays additively
        setBlendEnabled(true);
        setBlendModeAdditive();
        renderBackend.bindVertexArray(renderer.godrayQuadVAO);
        renderer.godrayCompositeShader->use();
        renderer.godrayCompositeShader->setInt("godrayTex", 0);
        renderer.godrayCompositeShader->setFloat("mapZoom", 1.0f);
        renderer.godrayCompositeShader->setVec2("mapCenter", glm::vec2(0.5f, 0.5f));
        bindTexture2DUnit(0, renderer.godrayBlurTex);
        renderBackend.drawArraysTriangles(0, 6);
        setBlendEnabled(false);

        setDepthWriteEnabled(true);
        setDepthTestEnabled(true);
    }

}
