#pragma once

#include "Host/PlatformInput.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <fstream>
#include <optional>
#include <cmath>
#include <iostream>
#include <algorithm>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace FontSystemLogic {
    struct FontAtlas {
        std::vector<unsigned char> fontBuffer;
        std::vector<unsigned char> bitmap;
        stbtt_packedchar cdata[96]{};
        RenderHandle texture = 0;
        int width = 512;
        int height = 512;
        float size = 24.0f;
        float ascent = 0.0f;
        float descent = 0.0f;
        float lineGap = 0.0f;
        float lineHeight = 24.0f;
        bool loaded = false;
    };

    struct FontVertex {
        glm::vec2 pos;
        glm::vec2 uv;
        glm::vec3 color;
    };

    struct FontBatch {
        FontAtlas* atlas = nullptr;
        std::vector<FontVertex> vertices;
    };

    static std::unordered_map<std::string, FontAtlas> g_atlases;

    enum class FontRenderPass {
        Timeline,
        SideButtons,
        TopButtons,
        All
    };

    namespace {
        constexpr bool kDebugRelayoutTrackLabels = false;
        constexpr int kFirstChar = 32;
        constexpr int kCharCount = 96;
        constexpr float kDefaultFontSize = 24.0f;
        const char* kDefaultFontName = "AlegreyaSans-Regular.ttf";
        constexpr int kAtlasPadding = 1;
        constexpr int kSmallFontOversample = 2;
        constexpr float kSmallFontThreshold = 18.0f;

        EntityInstance* findInstanceById(LevelContext& level, int worldIndex, int instanceId) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return nullptr;
            Entity& world = level.worlds[worldIndex];
            for (auto& inst : world.instances) {
                if (inst.instanceID == instanceId) return &inst;
            }
            return nullptr;
        }

        void buildTextCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, FontContext& fontCtx) {
            if (!baseSystem.level) return;
            fontCtx.textInstances.clear();
            for (size_t wi = 0; wi < baseSystem.level->worlds.size(); ++wi) {
                const auto& world = baseSystem.level->worlds[wi];
                for (const auto& inst : world.instances) {
                    if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                    if (prototypes[inst.prototypeID].name != "Text") continue;
                    fontCtx.textInstances.emplace_back(static_cast<int>(wi), inst.instanceID);
                }
            }
            fontCtx.textCacheBuilt = true;
        }

        glm::vec2 pixelToNDC(const glm::vec2& pixel, double width, double height) {
            float ndcX = static_cast<float>((pixel.x / width) * 2.0 - 1.0);
            float ndcY = static_cast<float>(1.0 - (pixel.y / height) * 2.0);
            return {ndcX, ndcY};
        }

        glm::vec3 resolveColor(const WorldContext* world, const std::string& name, const glm::vec3& fallback) {
            if (world && !name.empty()) {
                auto it = world->colorLibrary.find(name);
                if (it != world->colorLibrary.end()) return it->second;
            }
            return fallback;
        }

        bool isTrackLabel(const EntityInstance& inst) {
            if (inst.controlRole != "label") return false;
            return inst.controlId.rfind("track_", 0) == 0 || inst.controlId.rfind("midi_track_", 0) == 0;
        }

        bool isTopButtonLabel(const EntityInstance& inst) {
            if (inst.controlRole != "label") return false;
            if (inst.controlId.rfind("track_", 0) == 0 || inst.controlId.rfind("midi_track_", 0) == 0) return false;
            return true;
        }

        bool shouldRenderText(const EntityInstance& inst, FontRenderPass pass) {
            bool isTimeline = (inst.controlRole == "timeline_label");
            bool isLabel = (inst.controlRole == "label");
            if (pass == FontRenderPass::All) return true;
            if (pass == FontRenderPass::Timeline) {
                return isTimeline || !isLabel;
            }
            if (pass == FontRenderPass::SideButtons) {
                return isTrackLabel(inst);
            }
            return isTopButtonLabel(inst);
        }

        void ensureResources(RendererContext& renderer, WorldContext& world, IRenderBackend& renderBackend) {
            if (!renderer.fontShader) {
                renderer.fontShader = std::make_unique<Shader>(world.shaders["FONT_VERTEX_SHADER"].c_str(), world.shaders["FONT_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.fontVAO == 0) {
                static const std::vector<VertexAttribLayout> kFontVertexLayout = {
                    {0, 2, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FontVertex)), offsetof(FontVertex, pos), 0},
                    {1, 2, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FontVertex)), offsetof(FontVertex, uv), 0},
                    {2, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FontVertex)), offsetof(FontVertex, color), 0}
                };
                renderBackend.ensureVertexArray(renderer.fontVAO);
                renderBackend.ensureArrayBuffer(renderer.fontVBO);
                renderBackend.uploadArrayBufferData(renderer.fontVBO, nullptr, 0, true);
                renderBackend.configureVertexArray(renderer.fontVAO, renderer.fontVBO, kFontVertexLayout, 0, {});
            }
        }

        std::optional<FontAtlas*> loadAtlas(const std::string& fontName,
                                            float size,
                                            IRenderBackend* renderBackend) {
            if (!renderBackend) return std::nullopt;
            int pixelSize = static_cast<int>(std::round(size <= 0.0f ? kDefaultFontSize : size));
            std::string key = fontName + "|" + std::to_string(pixelSize);
            auto it = g_atlases.find(key);
            if (it != g_atlases.end()) return &it->second;

            FontAtlas atlas;
            atlas.size = static_cast<float>(pixelSize);
            std::string path = std::string("Procedures/Fonts/") + fontName;
            std::ifstream f(path, std::ios::binary);
            if (!f.is_open()) {
                std::cerr << "FontSystem: missing font file " << path << "\n";
                return std::nullopt;
            }
            f.seekg(0, std::ios::end);
            std::streamoff len = f.tellg();
            f.seekg(0, std::ios::beg);
            atlas.fontBuffer.resize(static_cast<size_t>(len));
            if (len > 0) {
                f.read(reinterpret_cast<char*>(atlas.fontBuffer.data()), len);
            }

            atlas.bitmap.resize(static_cast<size_t>(atlas.width * atlas.height));
            stbtt_pack_context packContext;
            if (!stbtt_PackBegin(&packContext, atlas.bitmap.data(), atlas.width, atlas.height, 0, kAtlasPadding, nullptr)) {
                std::cerr << "FontSystem: failed to init font packer " << path << "\n";
                return std::nullopt;
            }
            int oversample = (atlas.size <= kSmallFontThreshold) ? kSmallFontOversample : 1;
            stbtt_PackSetOversampling(&packContext, oversample, oversample);
            if (!stbtt_PackFontRange(&packContext,
                                     atlas.fontBuffer.data(),
                                     0,
                                     atlas.size,
                                     kFirstChar,
                                     kCharCount,
                                     atlas.cdata)) {
                stbtt_PackEnd(&packContext);
                std::cerr << "FontSystem: failed to pack font " << path << "\n";
                return std::nullopt;
            }
            stbtt_PackEnd(&packContext);

            stbtt_fontinfo info;
            if (stbtt_InitFont(&info, atlas.fontBuffer.data(), 0)) {
                int ascent = 0, descent = 0, lineGap = 0;
                stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
                float scale = stbtt_ScaleForPixelHeight(&info, atlas.size);
                atlas.ascent = ascent * scale;
                atlas.descent = descent * scale;
                atlas.lineGap = lineGap * scale;
                atlas.lineHeight = (ascent - descent + lineGap) * scale;
            } else {
                atlas.lineHeight = atlas.size;
            }

            std::vector<unsigned char> atlasRgba(static_cast<size_t>(atlas.width * atlas.height * 4), 0u);
            for (size_t i = 0; i < atlas.bitmap.size(); ++i) {
                const unsigned char v = atlas.bitmap[i];
                const size_t o = i * 4u;
                atlasRgba[o + 0] = v;
                atlasRgba[o + 1] = v;
                atlasRgba[o + 2] = v;
                atlasRgba[o + 3] = v;
            }

            TextureUploadParams uploadParams;
            uploadParams.minFilter = TextureFilterMode::Linear;
            uploadParams.magFilter = TextureFilterMode::Linear;
            uploadParams.wrapS = TextureWrapMode::ClampToEdge;
            uploadParams.wrapT = TextureWrapMode::ClampToEdge;
            const bool uploaded = renderBackend->uploadRgbaTexture2D(
                atlas.texture,
                atlas.width,
                atlas.height,
                atlasRgba,
                uploadParams
            );
            if (!uploaded || !atlas.texture) {
                std::cerr << "FontSystem: failed to upload atlas texture for " << path << "\n";
                return std::nullopt;
            }
            atlas.loaded = true;

            auto [newIt, _] = g_atlases.emplace(key, std::move(atlas));
            return &newIt->second;
        }

        void measureText(const std::string& text, const FontAtlas& atlas, float& outWidth, float& outHeight) {
            float lineHeight = (atlas.lineHeight > 0.0f) ? atlas.lineHeight : atlas.size;
            float lineWidth = 0.0f;
            float maxWidth = 0.0f;
            int lineCount = 1;

            for (char c : text) {
                if (c == '\n') {
                    maxWidth = std::max(maxWidth, lineWidth);
                    lineWidth = 0.0f;
                    ++lineCount;
                    continue;
                }
                unsigned char uc = static_cast<unsigned char>(c);
                if (uc < kFirstChar || uc >= kFirstChar + kCharCount) continue;
                const stbtt_packedchar& bc = atlas.cdata[uc - kFirstChar];
                lineWidth += bc.xadvance;
            }

            maxWidth = std::max(maxWidth, lineWidth);
            outWidth = maxWidth;
            outHeight = lineHeight * lineCount;
        }

        void appendTextVertices(std::vector<FontVertex>& verts,
                                const FontAtlas& atlas,
                                const std::string& text,
                                const glm::vec3& color,
                                const glm::vec2& position,
                                double screenWidth,
                                double screenHeight) {
            if (text.empty()) return;

            float textWidth = 0.0f;
            float textHeight = 0.0f;
            measureText(text, atlas, textWidth, textHeight);
            float lineHeight = (atlas.lineHeight > 0.0f) ? atlas.lineHeight : atlas.size;

            float startX = position.x - textWidth * 0.5f;
            float startY = position.y - textHeight * 0.5f;
            float x = startX;
            float y = startY + atlas.ascent;

            int lineIndex = 0;
            for (char c : text) {
                if (c == '\n') {
                    ++lineIndex;
                    x = startX;
                    y = startY + atlas.ascent + lineIndex * lineHeight;
                    continue;
                }
                unsigned char uc = static_cast<unsigned char>(c);
                if (uc < kFirstChar || uc >= kFirstChar + kCharCount) continue;

                stbtt_aligned_quad q;
                float xCursor = x;
                float yCursor = y;
                stbtt_GetPackedQuad(atlas.cdata, atlas.width, atlas.height, uc - kFirstChar, &xCursor, &yCursor, &q, 1);
                x = xCursor;

                glm::vec2 p0 = pixelToNDC({q.x0, q.y0}, screenWidth, screenHeight);
                glm::vec2 p1 = pixelToNDC({q.x1, q.y0}, screenWidth, screenHeight);
                glm::vec2 p2 = pixelToNDC({q.x1, q.y1}, screenWidth, screenHeight);
                glm::vec2 p3 = pixelToNDC({q.x0, q.y1}, screenWidth, screenHeight);

                verts.push_back({p0, {q.s0, q.t0}, color});
                verts.push_back({p1, {q.s1, q.t0}, color});
                verts.push_back({p2, {q.s1, q.t1}, color});
                verts.push_back({p0, {q.s0, q.t0}, color});
                verts.push_back({p2, {q.s1, q.t1}, color});
                verts.push_back({p3, {q.s0, q.t1}, color});
            }
        }
    }

    namespace {
        void renderFontsPass(BaseSystem& baseSystem, std::vector<Entity>& prototypes, PlatformWindowHandle win, FontRenderPass pass) {
            if (!baseSystem.renderer || !baseSystem.world || !baseSystem.level || !baseSystem.font || !baseSystem.ui || !baseSystem.renderBackend || !win) return;
            RendererContext& renderer = *baseSystem.renderer;
            WorldContext& world = *baseSystem.world;
            FontContext& fontCtx = *baseSystem.font;
            UIContext& ui = *baseSystem.ui;
            IRenderBackend& renderBackend = *baseSystem.renderBackend;

            ensureResources(renderer, world, renderBackend);

            int windowWidth = 0, windowHeight = 0;
            PlatformInput::GetWindowSize(win, windowWidth, windowHeight);
            double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
            double screenHeight = windowHeight > 0 ? static_cast<double>(windowHeight) : 1080.0;

            std::vector<FontBatch> batches;
            std::unordered_map<std::string, size_t> batchIndex;

            if (!fontCtx.textCacheBuilt) {
                buildTextCache(baseSystem, prototypes, fontCtx);
            }
            bool cacheValid = true;
            for (const auto& ref : fontCtx.textInstances) {
                EntityInstance* inst = findInstanceById(*baseSystem.level, ref.first, ref.second);
                if (!inst) { cacheValid = false; continue; }
                if (!shouldRenderText(*inst, pass)) continue;

                bool uiOnly = (inst->textType == "UIOnly" || inst->textType == "VariableUI");
                if (uiOnly && !ui.active) continue;

                std::string textValue;
                bool isVariable = (inst->textType == "VariableText" || inst->textType == "VariableUI");
                if (isVariable) {
                    auto it = fontCtx.variables.find(inst->textKey);
                    if (it == fontCtx.variables.end()) continue;
                    textValue = it->second;
                } else {
                    textValue = inst->text;
                }
                if (textValue.empty()) continue;

                std::string fontName = inst->font.empty() ? kDefaultFontName : inst->font;
                float fontSize = inst->size.x > 0.0f ? inst->size.x : kDefaultFontSize;
                auto atlasOpt = loadAtlas(fontName, fontSize, baseSystem.renderBackend);
                if (!atlasOpt.has_value()) continue;
                FontAtlas* atlas = *atlasOpt;

                glm::vec3 baseColor = resolveColor(&world, inst->colorName, inst->color);

                int pixelSize = static_cast<int>(std::round(fontSize));
                std::string key = fontName + "|" + std::to_string(pixelSize);
                size_t index = 0;
                auto idxIt = batchIndex.find(key);
                if (idxIt == batchIndex.end()) {
                    index = batches.size();
                    batchIndex[key] = index;
                    batches.push_back(FontBatch{atlas, {}});
                } else {
                    index = idxIt->second;
                }

                glm::vec2 pos(inst->position.x, inst->position.y);
                appendTextVertices(batches[index].vertices,
                                   *atlas,
                                   textValue,
                                   baseColor,
                                   pos,
                                   screenWidth,
                                   screenHeight);
            }
            if (!cacheValid) {
                fontCtx.textCacheBuilt = false;
            }

            if (batches.empty() || !renderer.fontShader) return;

            auto bindTexture2D = [&](RenderHandle texture, int unit) {
                renderBackend.bindTexture2D(texture, unit);
            };
            auto setDepthTestEnabled = [&](bool enabled) {
                renderBackend.setDepthTestEnabled(enabled);
            };
            auto setBlendEnabled = [&](bool enabled) {
                renderBackend.setBlendEnabled(enabled);
            };
            auto setBlendModeAlpha = [&]() {
                renderBackend.setBlendModeAlpha();
            };

            setDepthTestEnabled(false);
            setBlendEnabled(true);
            setBlendModeAlpha();
            renderer.fontShader->use();
            renderer.fontShader->setInt("fontTex", 0);
            renderBackend.bindVertexArray(renderer.fontVAO);

            for (auto& batch : batches) {
                if (!batch.atlas || batch.vertices.empty()) continue;
                bindTexture2D(batch.atlas->texture, 0);
                renderBackend.uploadArrayBufferData(
                    renderer.fontVBO,
                    batch.vertices.data(),
                    batch.vertices.size() * sizeof(FontVertex),
                    true
                );
                renderBackend.drawArraysTriangles(0, static_cast<int>(batch.vertices.size()));
            }

            renderBackend.unbindVertexArray();
            bindTexture2D(0, 0);
            setBlendEnabled(false);
            setDepthTestEnabled(true);
        }
    }

    void UpdateFonts(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt;
        renderFontsPass(baseSystem, prototypes, win, FontRenderPass::All);
    }

    void RenderFontsTimeline(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt;
        renderFontsPass(baseSystem, prototypes, win, FontRenderPass::Timeline);
    }

    void RenderFontsSideButtons(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt;
        renderFontsPass(baseSystem, prototypes, win, FontRenderPass::SideButtons);
    }

    void RenderFontsTopButtons(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt;
        renderFontsPass(baseSystem, prototypes, win, FontRenderPass::TopButtons);
    }

    bool RasterizeBookTextBitmap(const BaseSystem& baseSystem,
                                 const std::string& fontName,
                                 float pixelHeight,
                                 const std::string& text,
                                 int maxCharsPerLine,
                                 int maxLines,
                                 std::vector<unsigned char>& outAlpha,
                                 int& outWidth,
                                 int& outHeight,
                                 int& outWrappedLineCount) {
        outAlpha.clear();
        outWidth = 0;
        outHeight = 0;
        outWrappedLineCount = 0;
        if (!baseSystem.renderBackend) return false;

        const std::string effectiveFont = fontName.empty() ? kDefaultFontName : fontName;
        const float clampedPixelHeight = std::clamp(pixelHeight, 8.0f, 96.0f);
        const int wrappedChars = std::clamp(maxCharsPerLine, 8, 64);
        const int wrappedMaxLines = std::clamp(maxLines, 1, 64);
        auto atlasOpt = loadAtlas(effectiveFont, clampedPixelHeight, baseSystem.renderBackend);
        if (!atlasOpt.has_value()) {
            atlasOpt = loadAtlas(kDefaultFontName, clampedPixelHeight, baseSystem.renderBackend);
            if (!atlasOpt.has_value()) return false;
        }
        FontAtlas* atlas = *atlasOpt;
        if (!atlas || atlas->bitmap.empty() || atlas->width <= 0 || atlas->height <= 0) return false;

        std::vector<std::string> rawLines;
        {
            std::stringstream ss(text);
            std::string line;
            while (std::getline(ss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                rawLines.push_back(line);
            }
        }
        if (rawLines.empty()) rawLines.emplace_back();

        std::vector<std::string> wrappedLines;
        wrappedLines.reserve(static_cast<size_t>(wrappedMaxLines));
        for (const std::string& raw : rawLines) {
            if (static_cast<int>(wrappedLines.size()) >= wrappedMaxLines) break;
            if (raw.empty()) {
                wrappedLines.emplace_back();
                continue;
            }
            size_t start = 0;
            while (start < raw.size() && static_cast<int>(wrappedLines.size()) < wrappedMaxLines) {
                const size_t len = std::min(static_cast<size_t>(wrappedChars), raw.size() - start);
                wrappedLines.push_back(raw.substr(start, len));
                start += len;
            }
        }
        if (wrappedLines.empty()) wrappedLines.emplace_back();

        const float lineHeightF = (atlas->lineHeight > 0.0f) ? atlas->lineHeight : atlas->size;
        const int lineHeightPx = std::max(1, static_cast<int>(std::ceil(lineHeightF)));
        const int baselinePx = std::max(0, static_cast<int>(std::ceil(atlas->ascent)));
        constexpr int kPad = 2;

        int maxAdvancePx = 1;
        for (const std::string& row : wrappedLines) {
            float advance = 0.0f;
            for (char c : row) {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (uc < kFirstChar || uc >= kFirstChar + kCharCount) continue;
                const stbtt_packedchar& glyph = atlas->cdata[uc - kFirstChar];
                advance += glyph.xadvance;
            }
            maxAdvancePx = std::max(maxAdvancePx, static_cast<int>(std::ceil(advance)));
        }

        outWrappedLineCount = static_cast<int>(wrappedLines.size());
        outWidth = std::max(1, maxAdvancePx + kPad * 2);
        outHeight = std::max(1, lineHeightPx * outWrappedLineCount + kPad * 2);
        outAlpha.assign(static_cast<size_t>(outWidth * outHeight), 0u);

        for (int lineIndex = 0; lineIndex < outWrappedLineCount; ++lineIndex) {
            const std::string& row = wrappedLines[static_cast<size_t>(lineIndex)];
            float penX = static_cast<float>(kPad);
            const float penY = static_cast<float>(kPad + baselinePx + lineIndex * lineHeightPx);

            for (char c : row) {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (uc < kFirstChar || uc >= kFirstChar + kCharCount) continue;
                const stbtt_packedchar& glyph = atlas->cdata[uc - kFirstChar];
                const int gw = glyph.x1 - glyph.x0;
                const int gh = glyph.y1 - glyph.y0;
                if (gw > 0 && gh > 0) {
                    const int dstX0 = static_cast<int>(std::floor(penX + glyph.xoff));
                    const int dstY0 = static_cast<int>(std::floor(penY + glyph.yoff));
                    for (int gy = 0; gy < gh; ++gy) {
                        const int sy = glyph.y0 + gy;
                        const int dy = dstY0 + gy;
                        if (sy < 0 || sy >= atlas->height || dy < 0 || dy >= outHeight) continue;
                        for (int gx = 0; gx < gw; ++gx) {
                            const int sx = glyph.x0 + gx;
                            const int dx = dstX0 + gx;
                            if (sx < 0 || sx >= atlas->width || dx < 0 || dx >= outWidth) continue;
                            const unsigned char srcAlpha = atlas->bitmap[static_cast<size_t>(sy * atlas->width + sx)];
                            unsigned char& dstAlpha = outAlpha[static_cast<size_t>(dy * outWidth + dx)];
                            if (srcAlpha > dstAlpha) dstAlpha = srcAlpha;
                        }
                    }
                }
                penX += glyph.xadvance;
            }
        }

        return true;
    }

    void CleanupFonts(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes;
        (void)dt;
        (void)win;
        if (!baseSystem.renderBackend) {
            g_atlases.clear();
            return;
        }
        for (auto& [_, atlas] : g_atlases) {
            if (atlas.texture) {
                baseSystem.renderBackend->destroyTexture(atlas.texture);
            }
        }
        g_atlases.clear();
    }
}
