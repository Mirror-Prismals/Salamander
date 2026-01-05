#pragma once

namespace AuroraSystemLogic {

    struct RibbonLayer { float alpha; float scaleX; float scaleY; GLenum sf; GLenum df; };

    static float frand() { return static_cast<float>(rand()) / (static_cast<float>(RAND_MAX) + 1.0f); }

    static void initAuroraResources(BaseSystem& baseSystem) {
        if (!baseSystem.renderer) return;
        RendererContext& renderer = *baseSystem.renderer;
        if (!renderer.auroraShader) {
            renderer.auroraShader = std::make_unique<Shader>(
                baseSystem.world->shaders["AURORA_VERTEX_SHADER"].c_str(),
                baseSystem.world->shaders["AURORA_FRAGMENT_SHADER"].c_str()
            );
            // Set palettes (magenta and lime)
            renderer.auroraShader->use();
            glm::vec3 magPal[4] = {
                glm::vec3(1.00f,0.15f,0.72f),
                glm::vec3(0.98f,0.28f,0.82f),
                glm::vec3(0.88f,0.45f,0.95f),
                glm::vec3(0.64f,0.28f,0.72f)
            };
            glm::vec3 limePal[4] = {
                glm::vec3(0.11f,1.00f,0.26f),
                glm::vec3(0.10f,0.95f,0.22f),
                glm::vec3(0.06f,0.82f,0.12f),
                glm::vec3(0.02f,0.50f,0.04f)
            };
            renderer.auroraShader->setVec3("magPal[0]", magPal[0]);
            renderer.auroraShader->setVec3("magPal[1]", magPal[1]);
            renderer.auroraShader->setVec3("magPal[2]", magPal[2]);
            renderer.auroraShader->setVec3("magPal[3]", magPal[3]);
            renderer.auroraShader->setVec3("limePal[0]", limePal[0]);
            renderer.auroraShader->setVec3("limePal[1]", limePal[1]);
            renderer.auroraShader->setVec3("limePal[2]", limePal[2]);
            renderer.auroraShader->setVec3("limePal[3]", limePal[3]);
        }
        if (renderer.auroras.empty()) {
            renderer.auroras.resize(4);
            for (size_t i = 0; i < renderer.auroras.size(); ++i) {
                float ang = static_cast<float>(i) / 4.0f * 6.283185307f + 0.2f * (frand() - 0.5f);
                float dist = 220.0f + 120.0f * static_cast<float>(i % 2);
                float height = 400.0f + ((i % 2) ? 60.0f : 0.0f);
                renderer.auroras[i].pos = glm::vec3(cos(ang) * dist, height, sin(ang) * dist);
                renderer.auroras[i].yaw = ang + 1.57079632679f;
                renderer.auroras[i].width = 1200.0f + 400.0f * ((i % 2) ? 1.0f : 0.0f);
                renderer.auroras[i].height = 140.0f + 60.0f * ((i % 2) ? 0.7f : 0.0f);
                renderer.auroras[i].palette = (i < 2) ? 0 : 1;
                renderer.auroras[i].bend = 40.0f + frand() * 200.0f;
                renderer.auroras[i].seed = frand();
                glGenVertexArrays(1, &renderer.auroras[i].vao);
                glGenBuffers(1, &renderer.auroras[i].vbo);
                renderer.auroras[i].vertexCount = 0;
            }
        }
    }

    static void buildRibbonMesh(RendererContext::AuroraRibbon& a, float time) {
        const int hSeg = 256;
        const int vSeg = 20;
        std::vector<float> verts;
        verts.reserve((hSeg - 1) * (vSeg - 1) * 6 * 5);

        float A1 = a.bend * 0.0015f;
        if (A1 > 0.8f) A1 = 0.8f;
        float A2 = A1 * 0.55f;
        float freq1 = 4.0f + a.seed * 6.0f;
        float freq2 = freq1 * 1.6f;
        float phi = a.seed * 6.2831853f;

        auto s_raw_at = [&](float u)->float {
            return u + A1 * sinf(u * freq1 * 6.2831853f + phi)
                     + A2 * sinf(u * freq2 * 6.2831853f + phi * 1.37f);
        };
        float s0 = s_raw_at(0.0f);
        float s1 = s_raw_at(1.0f);
        float denom = s1 - s0;
        if (fabs(denom) < 1e-4f) denom = 1e-4f;

        auto center_at = [&](float sNorm)->glm::vec2 {
            float lowA = 0.6f * a.bend * 0.0025f;
            float lowFreq = 0.8f + a.seed * 0.7f;
            float longX = (sNorm - 0.5f)
                + lowA * ( sinf(sNorm * lowFreq * 6.2831853f + time * 0.02f + a.seed * 3.0f)
                          + 0.4f * sinf(sNorm * (lowFreq * 1.7f) * 6.2831853f + time * 0.017f) );
            float medA = 0.18f * a.bend * 0.002f;
            float med = medA * sinf(sNorm * 6.2831853f * (2.3f + a.seed * 1.8f) + time * 0.07f);
            float hi = 0.06f * sinf(sNorm * 6.2831853f * (12.0f + a.seed * 8.0f) + time * 0.12f);
            float centerX = longX + med + hi;
            float z = (0.4f * sinf(sNorm * 6.2831853f * (1.1f + a.seed * 0.6f) + time * 0.03f)
                     + 0.25f * cosf(sNorm * 6.2831853f * (0.7f + a.seed * 0.3f) + time * 0.018f))
                     * (1.0f + a.seed * 0.6f);
            return glm::vec2(centerX, z);
        };

        for (int i = 0; i < hSeg - 1; ++i) {
            float u0 = static_cast<float>(i) / static_cast<float>(hSeg - 1);
            float u1 = static_cast<float>(i + 1) / static_cast<float>(hSeg - 1);
            float sr0 = s_raw_at(u0), sN0 = (sr0 - s0) / denom;
            float sr1 = s_raw_at(u1), sN1 = (sr1 - s0) / denom;
            glm::vec2 c0 = center_at(sN0);
            glm::vec2 c1 = center_at(sN1);

            for (int j = 0; j < vSeg - 1; ++j) {
                float v0 = static_cast<float>(j) / static_cast<float>(vSeg - 1);
                float v1 = static_cast<float>(j + 1) / static_cast<float>(vSeg - 1);

                auto emit = [&](glm::vec2 center, float uCoord, float vCoord){
                    verts.push_back(center.x);
                    verts.push_back(vCoord);
                    verts.push_back(center.y);
                    verts.push_back(uCoord);
                    verts.push_back(vCoord);
                };

                emit(c0, u0, v0);
                emit(c1, u1, v0);
                emit(c1, u1, v1);

                emit(c0, u0, v0);
                emit(c1, u1, v1);
                emit(c0, u0, v1);
            }
        }

        a.vertexCount = static_cast<int>(verts.size() / 5);
        glBindVertexArray(a.vao);
        glBindBuffer(GL_ARRAY_BUFFER, a.vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(4 * sizeof(float))); glEnableVertexAttribArray(2);
    }

    void RenderAuroras(BaseSystem& baseSystem, float time, const glm::mat4& view, const glm::mat4& projection) {
        initAuroraResources(baseSystem);
        if (!baseSystem.renderer || !baseSystem.renderer->auroraShader) return;
        RendererContext& renderer = *baseSystem.renderer;

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Rebuild ribbons
        for (auto& a : renderer.auroras) {
            buildRibbonMesh(a, time * 0.7f);
        }

        renderer.auroraShader->use();
        renderer.auroraShader->setMat4("view", view);
        renderer.auroraShader->setMat4("projection", projection);
        renderer.auroraShader->setFloat("time", time);

        std::vector<RibbonLayer> layers = {
            {0.96f, 1.0f, 1.0f, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA},
            {0.44f, 1.0f, 1.0f, GL_SRC_ALPHA, GL_ONE},
            {0.20f, 1.0f, 1.0f, GL_SRC_ALPHA, GL_ONE}
        };

        for (size_t li = 0; li < layers.size(); ++li) {
            const auto& layer = layers[li];
            for (auto& a : renderer.auroras) {
                glm::mat4 model = glm::translate(glm::mat4(1.0f), a.pos);
                model = glm::rotate(model, a.yaw, glm::vec3(0,1,0));
                model = glm::scale(model, glm::vec3(a.width, a.height, a.width * 0.12f));

                glBlendFunc(layer.sf, layer.df);
                renderer.auroraShader->setMat4("model", model);
                renderer.auroraShader->setInt("paletteIndex", a.palette);
                renderer.auroraShader->setFloat("passAlpha", layer.alpha);
                glBindVertexArray(a.vao);
                glDrawArrays(GL_TRIANGLES, 0, a.vertexCount);

                if (li == 1) { // single halo on middle layer to avoid double-add brightening
                    glm::mat4 haloModel = glm::scale(model, glm::vec3(1.25f, 1.35f, 1.05f));
                    renderer.auroraShader->setMat4("model", haloModel);
                    renderer.auroraShader->setFloat("passAlpha", layer.alpha * 0.6f);
                    glDrawArrays(GL_TRIANGLES, 0, a.vertexCount);
                }
            }
        }

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

}
