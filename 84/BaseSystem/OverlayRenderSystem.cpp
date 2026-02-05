#pragma once

#include <vector>

namespace OverlayRenderSystemLogic {

    void RenderOverlays(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt; (void)win;
        if (!baseSystem.renderer || !baseSystem.player) return;
        RendererContext& renderer = *baseSystem.renderer;
        PlayerContext& player = *baseSystem.player;

        float time = static_cast<float>(glfwGetTime());
        glm::mat4 view = player.viewMatrix;
        glm::mat4 projection = player.projectionMatrix;
        glm::vec3 playerPos = player.cameraPosition;

        if (player.hasBlockTarget && renderer.selectionShader && renderer.selectionVAO && renderer.selectionVertexCount > 0) {
            renderer.selectionShader->use();
            glm::mat4 selectionModel = glm::translate(glm::mat4(1.0f), player.targetedBlockPosition);
            selectionModel = glm::scale(selectionModel, glm::vec3(1.02f));
            renderer.selectionShader->setMat4("model", selectionModel);
            renderer.selectionShader->setMat4("view", view);
            renderer.selectionShader->setMat4("projection", projection);
            renderer.selectionShader->setVec3("cameraPos", playerPos);
            renderer.selectionShader->setFloat("time", time);
            glBindVertexArray(renderer.selectionVAO);
            glDrawArrays(GL_LINES, 0, renderer.selectionVertexCount);
        }

        if (renderer.audioRayShader && renderer.audioRayVAO && renderer.audioRayVertexCount > 0) {
            glEnable(GL_BLEND);
            renderer.audioRayShader->use();
            renderer.audioRayShader->setMat4("view", view);
            renderer.audioRayShader->setMat4("projection", projection);
            glBindVertexArray(renderer.audioRayVAO);
            glLineWidth(1.6f);
            glDrawArrays(GL_LINES, 0, renderer.audioRayVertexCount);
            glLineWidth(1.0f);
        }

        bool crosshairEnabled = true;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("CrosshairEnabled");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                crosshairEnabled = std::get<bool>(it->second);
            }
        }
        if (crosshairEnabled && renderer.crosshairShader && renderer.crosshairVAO && renderer.crosshairVertexCount > 0) {
            glDisable(GL_DEPTH_TEST);
            renderer.crosshairShader->use();
            glBindVertexArray(renderer.crosshairVAO);
            glLineWidth(1.0f);
            glDrawArrays(GL_LINES, 0, renderer.crosshairVertexCount);
            glLineWidth(1.0f);
            glEnable(GL_DEPTH_TEST);
        }

        if (baseSystem.hud && renderer.hudShader && renderer.hudVAO) {
            HUDContext& hud = *baseSystem.hud;
            if (hud.showCharge) {
                glDisable(GL_DEPTH_TEST);
                renderer.hudShader->use();
                renderer.hudShader->setFloat("fillAmount", glm::clamp(hud.chargeValue, 0.0f, 1.0f));
                renderer.hudShader->setInt("ready", hud.chargeReady ? 1 : 0);
                renderer.hudShader->setInt("buildModeType", hud.buildModeType);
                renderer.hudShader->setVec3("previewColor", hud.buildPreviewColor);
                renderer.hudShader->setInt("channelIndex", hud.buildChannel);
                renderer.hudShader->setInt("previewTileIndex", hud.buildPreviewTileIndex);
                renderer.hudShader->setInt("atlasEnabled", (renderer.atlasTexture != 0 && renderer.atlasTilesPerRow > 0 && renderer.atlasTilesPerCol > 0) ? 1 : 0);
                renderer.hudShader->setVec2("atlasTileSize", glm::vec2(renderer.atlasTileSize));
                renderer.hudShader->setVec2("atlasTextureSize", glm::vec2(renderer.atlasTextureSize));
                renderer.hudShader->setInt("tilesPerRow", renderer.atlasTilesPerRow);
                renderer.hudShader->setInt("tilesPerCol", renderer.atlasTilesPerCol);
                renderer.hudShader->setInt("atlasTexture", 0);
                if (renderer.atlasTexture != 0) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, renderer.atlasTexture);
                }
                glBindVertexArray(renderer.hudVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glEnable(GL_DEPTH_TEST);
            }
        }
    }
}
