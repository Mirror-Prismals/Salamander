#pragma once

#include "../Host.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"

namespace BlockTextureSystemLogic {
    namespace {
        std::string readRegistryString(const BaseSystem& baseSystem, const char* key, const char* fallback) {
            if (!baseSystem.registry) return std::string(fallback ? fallback : "");
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) {
                return std::string(fallback ? fallback : "");
            }
            return std::get<std::string>(it->second);
        }

        bool readRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            return fallback;
        }

        FaceTextureSet parseFaceTextureSet(const json& entry) {
            FaceTextureSet set;
            if (entry.contains("all")) set.all = entry["all"].get<int>();
            if (entry.contains("top")) set.top = entry["top"].get<int>();
            if (entry.contains("bottom")) set.bottom = entry["bottom"].get<int>();
            if (entry.contains("side")) set.side = entry["side"].get<int>();
            return set;
        }

        bool isExternalTextureKey(const std::string& key) {
            return key == "RubyOre"
                || key == "SilverOre"
                || key == "AmethystOre"
                || key == "FlouriteOre"
                || key == "DirtExternal"
                || key == "StoneExternal";
        }

        std::vector<unsigned char> resizeNearestRgba(const unsigned char* src,
                                                     int srcW,
                                                     int srcH,
                                                     int dstW,
                                                     int dstH) {
            if (!src || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return {};
            std::vector<unsigned char> out(static_cast<size_t>(dstW * dstH * 4), 0u);
            for (int y = 0; y < dstH; ++y) {
                const int srcY = std::clamp((y * srcH) / std::max(1, dstH), 0, srcH - 1);
                for (int x = 0; x < dstW; ++x) {
                    const int srcX = std::clamp((x * srcW) / std::max(1, dstW), 0, srcW - 1);
                    const size_t srcIndex = static_cast<size_t>((srcY * srcW + srcX) * 4);
                    const size_t dstIndex = static_cast<size_t>((y * dstW + x) * 4);
                    out[dstIndex + 0] = src[srcIndex + 0];
                    out[dstIndex + 1] = src[srcIndex + 1];
                    out[dstIndex + 2] = src[srcIndex + 2];
                    out[dstIndex + 3] = src[srcIndex + 3];
                }
            }
            return out;
        }

        void overrideAtlasTilePixels(IRenderBackend* renderBackend,
                                     RenderHandle atlasTexture,
                                     int tileIndex,
                                     const glm::ivec2& tileSize,
                                     int tilesPerRow,
                                     int tilesPerCol,
                                     const unsigned char* rgbaPixels) {
            if (atlasTexture == 0
                || tileIndex < 0
                || tileSize.x <= 0
                || tileSize.y <= 0
                || tilesPerRow <= 0
                || tilesPerCol <= 0
                || !renderBackend
                || !rgbaPixels) return;
            const int tileX = (tileIndex % tilesPerRow) * tileSize.x;
            // Match shader atlas addressing (tile rows are indexed from the top in tile-space).
            const int tileRowFromTop = (tileIndex / tilesPerRow);
            const int tileRowFromBottom = (tilesPerCol - 1 - tileRowFromTop);
            const int tileY = tileRowFromBottom * tileSize.y;
            const size_t pixelCount = static_cast<size_t>(tileSize.x) * static_cast<size_t>(tileSize.y) * 4u;
            std::vector<unsigned char> uploadPixels(rgbaPixels, rgbaPixels + pixelCount);
            renderBackend->uploadRgbaTextureSubImage2D(atlasTexture, tileX, tileY, tileSize.x, tileSize.y, uploadPixels);
        }

        bool loadImageResizedRgba(const char* texturePath,
                                  const glm::ivec2& targetSize,
                                  std::vector<unsigned char>& outPixels,
                                  const char* label) {
            outPixels.clear();
            if (!texturePath || targetSize.x <= 0 || targetSize.y <= 0) return false;
            int w = 0, h = 0, ch = 0;
            unsigned char* pixels = stbi_load(texturePath, &w, &h, &ch, STBI_rgb_alpha);
            if (!pixels) {
                std::cerr << "BlockTextureSystem: Failed to load " << (label ? label : "texture")
                          << " " << texturePath << "\n";
                return false;
            }
            if (w == targetSize.x && h == targetSize.y) {
                outPixels.assign(pixels, pixels + static_cast<size_t>(w * h * 4));
            } else {
                outPixels = resizeNearestRgba(pixels, w, h, targetSize.x, targetSize.y);
                std::cout << "BlockTextureSystem: Resized " << (label ? label : "texture")
                          << " from " << w << "x" << h
                          << " to " << targetSize.x << "x" << targetSize.y << ".\n";
            }
            stbi_image_free(pixels);
            return !outPixels.empty();
        }

        void overrideAtlasTile(IRenderBackend* renderBackend,
                               RenderHandle atlasTexture,
                               int tileIndex,
                               const glm::ivec2& tileSize,
                               int tilesPerRow,
                               int tilesPerCol,
                               const char* texturePath,
                               const char* label) {
            if (atlasTexture == 0
                || tileIndex < 0
                || tileSize.x <= 0
                || tileSize.y <= 0
                || tilesPerRow <= 0
                || tilesPerCol <= 0) return;
            int tw = 0, th = 0, tch = 0;
            unsigned char* tpixels = stbi_load(texturePath, &tw, &th, &tch, STBI_rgb_alpha);
            if (!tpixels) {
                std::cerr << "BlockTextureSystem: Failed to load " << label << " override texture " << texturePath << "\n";
                return;
            }

            std::vector<unsigned char> uploadBuffer;
            const unsigned char* uploadPixels = nullptr;
            if (tw == tileSize.x && th == tileSize.y) {
                uploadPixels = tpixels;
            } else {
                uploadBuffer = resizeNearestRgba(tpixels, tw, th, tileSize.x, tileSize.y);
                uploadPixels = uploadBuffer.empty() ? nullptr : uploadBuffer.data();
                std::cout << "BlockTextureSystem: Resized " << label << " override from "
                          << tw << "x" << th << " to " << tileSize.x << "x" << tileSize.y << ".\n";
            }
            overrideAtlasTilePixels(renderBackend, atlasTexture, tileIndex, tileSize, tilesPerRow, tilesPerCol, uploadPixels);
            stbi_image_free(tpixels);
        }

        bool extractAtlasTilePixels(const std::vector<unsigned char>& atlasPixels,
                                    const glm::ivec2& atlasSize,
                                    int tileIndex,
                                    const glm::ivec2& tileSize,
                                    int tilesPerRow,
                                    int tilesPerCol,
                                    std::vector<unsigned char>& outPixels) {
            outPixels.clear();
            if (atlasPixels.empty()
                || atlasSize.x <= 0
                || atlasSize.y <= 0
                || tileIndex < 0
                || tileSize.x <= 0
                || tileSize.y <= 0
                || tilesPerRow <= 0
                || tilesPerCol <= 0) return false;

            const int tileX = (tileIndex % tilesPerRow) * tileSize.x;
            const int tileRowFromTop = (tileIndex / tilesPerRow);
            const int tileRowFromBottom = (tilesPerCol - 1 - tileRowFromTop);
            const int tileY = tileRowFromBottom * tileSize.y;
            if (tileX < 0
                || tileY < 0
                || tileX + tileSize.x > atlasSize.x
                || tileY + tileSize.y > atlasSize.y) return false;

            outPixels.resize(static_cast<size_t>(tileSize.x * tileSize.y * 4), 0u);
            for (int y = 0; y < tileSize.y; ++y) {
                const size_t src = static_cast<size_t>(((tileY + y) * atlasSize.x + tileX) * 4);
                const size_t dst = static_cast<size_t>((y * tileSize.x) * 4);
                std::copy_n(&atlasPixels[src], static_cast<size_t>(tileSize.x * 4), &outPixels[dst]);
            }
            return true;
        }
    }

    void LoadBlockTextures(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt; (void)win;
        if (!baseSystem.renderer || !baseSystem.world) { std::cerr << "BlockTextureSystem: Missing RendererContext or WorldContext.\n"; return; }
        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        IRenderBackend* renderBackend = baseSystem.renderBackend;
        if (!renderBackend) {
            std::cerr << "BlockTextureSystem: Missing render backend.\n";
            return;
        }

        const std::string defaultAtlasMapPath = "Procedures/assets/atlas.json";
        const std::string defaultAtlasTexturePath = "Procedures/assets/atlas.png";
        const std::string configuredAtlasMapPath = readRegistryString(baseSystem, "AtlasMapPath", defaultAtlasMapPath.c_str());
        const std::string configuredAtlasTexturePath = readRegistryString(baseSystem, "AtlasTexturePath", defaultAtlasTexturePath.c_str());

        std::string atlasMapPath = configuredAtlasMapPath;
        std::ifstream mapFile(atlasMapPath);
        if (!mapFile.is_open() && atlasMapPath != defaultAtlasMapPath) {
            std::cerr << "BlockTextureSystem: Could not open atlas map " << atlasMapPath
                      << ", falling back to " << defaultAtlasMapPath << "\n";
            atlasMapPath = defaultAtlasMapPath;
            mapFile.open(atlasMapPath);
        }
        if (!mapFile.is_open()) {
            std::cerr << "BlockTextureSystem: Could not open atlas map " << atlasMapPath << "\n";
            return;
        }

        json atlasData;
        try { atlasData = json::parse(mapFile); }
        catch (...) { std::cerr << "BlockTextureSystem: Failed to parse atlas map " << atlasMapPath << "\n"; return; }

        glm::ivec2 tileSize = world.atlasTileSize;
        if (atlasData.contains("tileSize") && atlasData["tileSize"].is_array() && atlasData["tileSize"].size() == 2) {
            tileSize.x = atlasData["tileSize"][0].get<int>();
            tileSize.y = atlasData["tileSize"][1].get<int>();
        }

        glm::ivec2 atlasSize = world.atlasTextureSize;
        if (atlasData.contains("atlasSize") && atlasData["atlasSize"].is_array() && atlasData["atlasSize"].size() == 2) {
            atlasSize.x = atlasData["atlasSize"][0].get<int>();
            atlasSize.y = atlasData["atlasSize"][1].get<int>();
        }

        int tilesPerRow = atlasData.value("tilesPerRow", 0);
        int tilesPerCol = atlasData.value("tilesPerCol", 0);

        world.atlasTileSize = tileSize;
        world.atlasTextureSize = atlasSize;
        world.atlasTilesPerRow = tilesPerRow;
        world.atlasTilesPerCol = tilesPerCol;

        if (atlasData.contains("blocks") && atlasData["blocks"].is_object()) {
            world.atlasMappings.clear();
            for (auto& [name, entry] : atlasData["blocks"].items()) {
                world.atlasMappings[name] = parseFaceTextureSet(entry);
            }
        }

        TextureUploadParams nearestClampParams;
        nearestClampParams.minFilter = TextureFilterMode::Nearest;
        nearestClampParams.magFilter = TextureFilterMode::Nearest;
        nearestClampParams.wrapS = TextureWrapMode::ClampToEdge;
        nearestClampParams.wrapT = TextureWrapMode::ClampToEdge;
        auto uploadRgbaTexture = [&](RenderHandle& texture,
                                     int tw,
                                     int th,
                                     const std::vector<unsigned char>& pixelBuffer,
                                     const TextureUploadParams& params) -> bool {
            if (tw <= 0 || th <= 0) return false;
            const size_t expectedSize = static_cast<size_t>(tw) * static_cast<size_t>(th) * 4u;
            if (pixelBuffer.size() < expectedSize) return false;
            return renderBackend->uploadRgbaTexture2D(texture, tw, th, pixelBuffer, params);
        };
        auto destroyTexture = [&](RenderHandle& texture) {
            if (!texture) return;
            renderBackend->destroyTexture(texture);
        };

        destroyTexture(renderer.atlasTexture);
        for (RenderHandle& tex : renderer.grassTextures) {
            destroyTexture(tex);
        }
        for (RenderHandle& tex : renderer.shortGrassTextures) {
            destroyTexture(tex);
        }
        for (RenderHandle& tex : renderer.oreTextures) {
            destroyTexture(tex);
        }
        for (RenderHandle& tex : renderer.terrainTextures) {
            destroyTexture(tex);
        }
        destroyTexture(renderer.waterOverlayTexture);
        renderer.grassTextureCount = 0;
        renderer.shortGrassTextureCount = 0;
        renderer.oreTextureCount = 0;
        renderer.terrainTextureCount = 0;

        int width = 0, height = 0, channels = 0;
        stbi_set_flip_vertically_on_load(true);
        std::string atlasTexturePath = configuredAtlasTexturePath;
        unsigned char* pixels = stbi_load(atlasTexturePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels && atlasTexturePath != defaultAtlasTexturePath) {
            std::cerr << "BlockTextureSystem: Failed to load atlas texture " << atlasTexturePath
                      << ", falling back to " << defaultAtlasTexturePath << "\n";
            atlasTexturePath = defaultAtlasTexturePath;
            pixels = stbi_load(atlasTexturePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        }
        if (!pixels) {
            std::cerr << "BlockTextureSystem: Failed to load atlas texture " << atlasTexturePath << "\n";
            return;
        }

        std::vector<unsigned char> atlasPixelsCpu(
            pixels,
            pixels + static_cast<size_t>(width * height * 4)
        );
        stbi_image_free(pixels);
        if (!uploadRgbaTexture(renderer.atlasTexture, width, height, atlasPixelsCpu, nearestClampParams)) {
            std::cerr << "BlockTextureSystem: Failed to upload atlas texture " << atlasTexturePath << "\n";
            return;
        }

        renderer.atlasTextureSize = glm::ivec2(width, height);
        renderer.atlasTileSize = tileSize;
        renderer.atlasTilesPerRow = tilesPerRow > 0 ? tilesPerRow : (tileSize.x > 0 ? width / tileSize.x : 0);
        renderer.atlasTilesPerCol = tilesPerCol > 0 ? tilesPerCol : (tileSize.y > 0 ? height / tileSize.y : 0);

        auto overrideMappedAtlasTile = [&](const char* textureKey,
                                           const char* texturePath,
                                           const char* label) {
            if (!textureKey || !texturePath || !label) return;
            auto it = world.atlasMappings.find(textureKey);
            if (it == world.atlasMappings.end()) return;
            int tileIndex = -1;
            if (it->second.all >= 0) tileIndex = it->second.all;
            else if (it->second.side >= 0) tileIndex = it->second.side;
            else if (it->second.top >= 0) tileIndex = it->second.top;
            else if (it->second.bottom >= 0) tileIndex = it->second.bottom;
            if (tileIndex < 0) return;
            overrideAtlasTile(renderBackend,
                              renderer.atlasTexture,
                              tileIndex,
                              renderer.atlasTileSize,
                              renderer.atlasTilesPerRow,
                              renderer.atlasTilesPerCol,
                              texturePath,
                              label);
        };
        auto resolveMappedAtlasTileIndex = [&](const char* textureKey) -> int {
            if (!textureKey) return -1;
            auto it = world.atlasMappings.find(textureKey);
            if (it == world.atlasMappings.end()) return -1;
            if (it->second.all >= 0) return it->second.all;
            if (it->second.side >= 0) return it->second.side;
            if (it->second.top >= 0) return it->second.top;
            if (it->second.bottom >= 0) return it->second.bottom;
            return -1;
        };

        // Standalone texture overrides used for fast art iteration.
        overrideMappedAtlasTile("GemRuby", "Procedures/assets/add_these/24x24_ruby_block_texture.png", "gem-ruby");
        overrideMappedAtlasTile("GemAmethyst", "Procedures/assets/add_these/24x24_amethyst_block_texture.png", "gem-amethyst");
        overrideMappedAtlasTile("GemFlourite", "Procedures/assets/add_these/24x24_flourite_block_texture.png", "gem-flourite");
        overrideMappedAtlasTile("GemSilver", "Procedures/assets/add_these/24x24_silver_block_texture.png", "gem-silver");
        {
            const int cavePotDstTile = resolveMappedAtlasTileIndex("ZeldaCavePot");
            const int cavePotSrcTile = resolveMappedAtlasTileIndex("24x24ZeldaCavePotSidePlainRgbaV001");
            std::vector<unsigned char> cavePotPixels;
            if (extractAtlasTilePixels(
                    atlasPixelsCpu,
                    renderer.atlasTextureSize,
                    cavePotSrcTile,
                    renderer.atlasTileSize,
                    renderer.atlasTilesPerRow,
                    renderer.atlasTilesPerCol,
                    cavePotPixels)) {
                overrideAtlasTilePixels(
                    renderBackend,
                    renderer.atlasTexture,
                    cavePotDstTile,
                    renderer.atlasTileSize,
                    renderer.atlasTilesPerRow,
                    renderer.atlasTilesPerCol,
                    cavePotPixels.data()
                );
            } else {
                std::cerr << "BlockTextureSystem: Could not resolve atlas source tile for ZeldaCavePot copy.\n";
            }
        }

        std::vector<unsigned char> blueprintBase;
        const int blueprintBaseTile = resolveMappedAtlasTileIndex("24x24BlueprintsBlankTextureV001");
        const bool haveBlueprintBase = extractAtlasTilePixels(
            atlasPixelsCpu,
            renderer.atlasTextureSize,
            blueprintBaseTile,
            renderer.atlasTileSize,
            renderer.atlasTilesPerRow,
            renderer.atlasTilesPerCol,
            blueprintBase
        );
        if (!haveBlueprintBase) {
            std::cerr << "BlockTextureSystem: Missing atlas tile for blueprint base key "
                      << "'24x24BlueprintsBlankTextureV001'\n";
        }
        auto composeAndOverrideBlueprintTile = [&](const char* textureKey,
                                                   const char* stencilTextureKey,
                                                   const char* label) {
            if (!textureKey || !stencilTextureKey || !label || !haveBlueprintBase) return;
            const int tileIndex = resolveMappedAtlasTileIndex(textureKey);
            if (tileIndex < 0) {
                std::cerr << "BlockTextureSystem: Missing destination atlas key '" << textureKey << "'\n";
                return;
            }
            const int stencilTile = resolveMappedAtlasTileIndex(stencilTextureKey);
            if (stencilTile < 0) {
                std::cerr << "BlockTextureSystem: Missing stencil atlas key '" << stencilTextureKey << "'\n";
                return;
            }

            std::vector<unsigned char> stencil;
            if (!extractAtlasTilePixels(
                    atlasPixelsCpu,
                    renderer.atlasTextureSize,
                    stencilTile,
                    renderer.atlasTileSize,
                    renderer.atlasTilesPerRow,
                    renderer.atlasTilesPerCol,
                    stencil)) {
                std::cerr << "BlockTextureSystem: Failed extracting stencil tile for '" << stencilTextureKey << "'\n";
                return;
            }
            if (stencil.size() != blueprintBase.size()) return;

            std::vector<unsigned char> composed = blueprintBase;
            int minStencilAlpha = 255;
            int maxStencilAlpha = 0;
            for (size_t i = 0; i + 3 < stencil.size(); i += 4) {
                const int a = static_cast<int>(stencil[i + 3]);
                minStencilAlpha = std::min(minStencilAlpha, a);
                maxStencilAlpha = std::max(maxStencilAlpha, a);
            }
            const bool useStencilAlphaMask = (maxStencilAlpha - minStencilAlpha) > 16 && maxStencilAlpha > 0;

            const int stencilW = renderer.atlasTileSize.x;
            const int stencilH = renderer.atlasTileSize.y;
            auto readLumaAt = [&](int x, int y) -> int {
                const int sx = std::clamp(x, 0, std::max(0, stencilW - 1));
                const int sy = std::clamp(y, 0, std::max(0, stencilH - 1));
                const size_t idx = static_cast<size_t>((sy * stencilW + sx) * 4);
                return (static_cast<int>(stencil[idx + 0])
                    + static_cast<int>(stencil[idx + 1])
                    + static_cast<int>(stencil[idx + 2])) / 3;
            };
            const int backgroundLuma = (
                readLumaAt(0, 0)
                + readLumaAt(stencilW - 1, 0)
                + readLumaAt(0, stencilH - 1)
                + readLumaAt(stencilW - 1, stencilH - 1)
            ) / 4;

            for (size_t i = 0; i + 3 < composed.size(); i += 4) {
                int overlayAlpha = 0;
                if (useStencilAlphaMask) {
                    overlayAlpha = static_cast<int>(stencil[i + 3]);
                } else {
                    const int stencilLuma = (static_cast<int>(stencil[i + 0])
                        + static_cast<int>(stencil[i + 1])
                        + static_cast<int>(stencil[i + 2])) / 3;
                    const int lumaDelta = std::abs(stencilLuma - backgroundLuma);
                    overlayAlpha = std::clamp(lumaDelta * 2, 0, 255);
                }
                if (overlayAlpha < 20) overlayAlpha = 0;

                const float a = static_cast<float>(overlayAlpha) / 255.0f;
                composed[i + 0] = static_cast<unsigned char>(std::clamp(
                    static_cast<float>(composed[i + 0]) * (1.0f - a) + 255.0f * a,
                    0.0f,
                    255.0f
                ));
                composed[i + 1] = static_cast<unsigned char>(std::clamp(
                    static_cast<float>(composed[i + 1]) * (1.0f - a) + 255.0f * a,
                    0.0f,
                    255.0f
                ));
                composed[i + 2] = static_cast<unsigned char>(std::clamp(
                    static_cast<float>(composed[i + 2]) * (1.0f - a) + 255.0f * a,
                    0.0f,
                    255.0f
                ));
                composed[i + 3] = static_cast<unsigned char>(std::max(
                    static_cast<int>(composed[i + 3]),
                    overlayAlpha
                ));
            }

            overrideAtlasTilePixels(
                renderBackend,
                renderer.atlasTexture,
                tileIndex,
                renderer.atlasTileSize,
                renderer.atlasTilesPerRow,
                renderer.atlasTilesPerCol,
                composed.data()
            );
        };
        composeAndOverrideBlueprintTile("BlueprintAxehead", "24x24AxeheadStencil", "blueprint-axehead");
        composeAndOverrideBlueprintTile("BlueprintHilt", "24x24HiltStencil", "blueprint-hilt");
        composeAndOverrideBlueprintTile("BlueprintPickaxe", "24x24PickaxeStencil", "blueprint-pickaxe");
        composeAndOverrideBlueprintTile("BlueprintScythe", "24x24ScytheStencil", "blueprint-scythe");
        composeAndOverrideBlueprintTile("BlueprintSword", "24x24SwordStencil", "blueprint-sword");

        world.atlasTextureSize = renderer.atlasTextureSize;
        world.atlasTilesPerRow = renderer.atlasTilesPerRow;
        world.atlasTilesPerCol = renderer.atlasTilesPerCol;

        world.prototypeTextureSets.clear();
        world.prototypeTextureSets.resize(prototypes.size());

        for (size_t i = 0; i < prototypes.size(); ++i) {
            const Entity& proto = prototypes[i];
            if (!proto.useTexture || proto.textureKey.empty()) continue;
            auto it = world.atlasMappings.find(proto.textureKey);
            if (it == world.atlasMappings.end()) {
                if (!isExternalTextureKey(proto.textureKey)) {
                    std::cerr << "BlockTextureSystem: Missing atlas entry for textureKey '" << proto.textureKey << "'\n";
                }
                continue;
            }
            world.prototypeTextureSets[i] = it->second;
        }

        const std::array<const char*, 3> kTallGrassTexturePaths = {
            "Procedures/assets/24x24_tall_grass_side_v001.png",
            "Procedures/assets/24x24_tall_grass_side_v002.png",
            "Procedures/assets/24x24_tall_grass_side_v003.png"
        };
        const std::array<const char*, 3> kShortGrassTexturePaths = {
            "Procedures/assets/24x24_short_grass_side_v001.png",
            "Procedures/assets/24x24_short_grass_side_v002.png",
            "Procedures/assets/24x24_short_grass_side_v003.png"
        };
        const std::array<const char*, 4> kOreTexturePaths = {
            "Procedures/assets/24x24_ruby_dirt_ore_combined.png",
            "Procedures/assets/24x24_silver_dirt_ore_combined.png",
            "Procedures/assets/24x24_amethyst_dirt_ore_combined.png",
            "Procedures/assets/24x24_rainbow_fluorite_dirt_ore_combined.png"
        };
        const std::array<const char*, 2> kTerrainTexturePaths = {
            "Procedures/assets/24x24_dirt_texture.png",
            "Procedures/assets/24x24_dirt_texture_4A3621.png"
        };
        const char* kWaterOverlayTexturePath = "Procedures/assets/24x24_water_texture_with_opacity1.png";
        auto loadGrassTextureSet = [&](const std::array<const char*, 3>& paths,
                                       std::array<RenderHandle, 3>& targets,
                                       int& loadedCount,
                                       const char* label) {
            loadedCount = 0;
            for (size_t i = 0; i < paths.size(); ++i) {
                int tw = 0, th = 0, tch = 0;
                unsigned char* tpixels = stbi_load(paths[i], &tw, &th, &tch, STBI_rgb_alpha);
                if (!tpixels) {
                    std::cerr << "BlockTextureSystem: Failed to load " << label << " texture " << paths[i] << "\n";
                    continue;
                }
                std::vector<unsigned char> uploadBuffer(
                    tpixels,
                    tpixels + static_cast<size_t>(tw * th * 4)
                );
                stbi_image_free(tpixels);
                if (uploadRgbaTexture(targets[i], tw, th, uploadBuffer, nearestClampParams)) {
                    loadedCount += 1;
                } else {
                    std::cerr << "BlockTextureSystem: Failed to upload " << label << " texture " << paths[i] << "\n";
                }
            }
        };
        auto loadOreTextureSet = [&](const std::array<const char*, 4>& paths,
                                     std::array<RenderHandle, 4>& targets,
                                     int& loadedCount,
                                     const char* label) {
            loadedCount = 0;
            for (size_t i = 0; i < paths.size(); ++i) {
                int tw = 0, th = 0, tch = 0;
                unsigned char* tpixels = stbi_load(paths[i], &tw, &th, &tch, STBI_rgb_alpha);
                if (!tpixels) {
                    std::cerr << "BlockTextureSystem: Failed to load " << label << " texture " << paths[i] << "\n";
                    continue;
                }
                std::vector<unsigned char> uploadBuffer(
                    tpixels,
                    tpixels + static_cast<size_t>(tw * th * 4)
                );
                stbi_image_free(tpixels);
                if (uploadRgbaTexture(targets[i], tw, th, uploadBuffer, nearestClampParams)) {
                    loadedCount += 1;
                } else {
                    std::cerr << "BlockTextureSystem: Failed to upload " << label << " texture " << paths[i] << "\n";
                }
            }
        };
        auto loadTerrainTextureSet = [&](const std::array<const char*, 2>& paths,
                                         std::array<RenderHandle, 2>& targets,
                                         int& loadedCount,
                                         const char* label) {
            loadedCount = 0;
            for (size_t i = 0; i < paths.size(); ++i) {
                int tw = 0, th = 0, tch = 0;
                unsigned char* tpixels = stbi_load(paths[i], &tw, &th, &tch, STBI_rgb_alpha);
                if (!tpixels) {
                    std::cerr << "BlockTextureSystem: Failed to load " << label << " texture " << paths[i] << "\n";
                    continue;
                }
                std::vector<unsigned char> uploadBuffer(
                    tpixels,
                    tpixels + static_cast<size_t>(tw * th * 4)
                );
                stbi_image_free(tpixels);
                if (uploadRgbaTexture(targets[i], tw, th, uploadBuffer, nearestClampParams)) {
                    loadedCount += 1;
                } else {
                    std::cerr << "BlockTextureSystem: Failed to upload " << label << " texture " << paths[i] << "\n";
                }
            }
        };

        const bool standaloneTallGrassEnabled = readRegistryBool(baseSystem, "StandaloneTallGrassTexturesEnabled", false);
        const bool standaloneShortGrassEnabled = readRegistryBool(baseSystem, "StandaloneShortGrassTexturesEnabled", false);
        const bool standaloneOreEnabled = readRegistryBool(baseSystem, "StandaloneOreTexturesEnabled", false);
        const bool standaloneTerrainEnabled = readRegistryBool(baseSystem, "StandaloneTerrainTexturesEnabled", false);
        const bool waterOverlayEnabled = readRegistryBool(baseSystem, "WaterOverlayTextureEnabled", false);

        if (standaloneTallGrassEnabled) {
            loadGrassTextureSet(kTallGrassTexturePaths, renderer.grassTextures, renderer.grassTextureCount, "tall grass");
        }
        if (standaloneShortGrassEnabled) {
            loadGrassTextureSet(kShortGrassTexturePaths, renderer.shortGrassTextures, renderer.shortGrassTextureCount, "short grass");
        }
        if (standaloneOreEnabled) {
            loadOreTextureSet(kOreTexturePaths, renderer.oreTextures, renderer.oreTextureCount, "ore");
        }
        if (standaloneTerrainEnabled) {
            loadTerrainTextureSet(kTerrainTexturePaths, renderer.terrainTextures, renderer.terrainTextureCount, "terrain");
        }
        if (waterOverlayEnabled) {
            int tw = 0, th = 0, tch = 0;
            unsigned char* tpixels = stbi_load(kWaterOverlayTexturePath, &tw, &th, &tch, STBI_rgb_alpha);
            if (!tpixels) {
                std::cerr << "BlockTextureSystem: Failed to load water overlay texture " << kWaterOverlayTexturePath << "\n";
            } else {
                std::vector<unsigned char> uploadBuffer(
                    tpixels,
                    tpixels + static_cast<size_t>(tw * th * 4)
                );
                stbi_image_free(tpixels);
                if (!uploadRgbaTexture(renderer.waterOverlayTexture, tw, th, uploadBuffer, nearestClampParams)) {
                    std::cerr << "BlockTextureSystem: Failed to upload water overlay texture " << kWaterOverlayTexturePath << "\n";
                }
            }
        }

        if (standaloneTallGrassEnabled && renderer.grassTextureCount < 3) {
            std::cerr << "BlockTextureSystem: Tall grass texture set incomplete (" << renderer.grassTextureCount << "/3)\n";
        }
        if (standaloneShortGrassEnabled && renderer.shortGrassTextureCount < 3) {
            std::cerr << "BlockTextureSystem: Short grass texture set incomplete (" << renderer.shortGrassTextureCount << "/3)\n";
        }
        if (standaloneOreEnabled && renderer.oreTextureCount < 4) {
            std::cerr << "BlockTextureSystem: Ore texture set incomplete (" << renderer.oreTextureCount << "/4)\n";
        }
        if (standaloneTerrainEnabled && renderer.terrainTextureCount < 2) {
            std::cerr << "BlockTextureSystem: Terrain texture set incomplete (" << renderer.terrainTextureCount << "/2)\n";
        }

    }
}
