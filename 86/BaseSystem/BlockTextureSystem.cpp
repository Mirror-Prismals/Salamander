#pragma once

#include "../Host.h"
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

        void overrideAtlasTile(GLuint atlasTexture,
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
            if (tw != tileSize.x || th != tileSize.y) {
                std::cerr << "BlockTextureSystem: Skipping " << label << " override due to size mismatch ("
                          << tw << "x" << th << ", expected " << tileSize.x << "x" << tileSize.y << ")\n";
                stbi_image_free(tpixels);
                return;
            }

            const int tileX = (tileIndex % tilesPerRow) * tileSize.x;
            // Match shader atlas addressing (tile rows are indexed from the top in tile-space).
            const int tileRowFromTop = (tileIndex / tilesPerRow);
            const int tileRowFromBottom = (tilesPerCol - 1 - tileRowFromTop);
            const int tileY = tileRowFromBottom * tileSize.y;
            glBindTexture(GL_TEXTURE_2D, atlasTexture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, tileX, tileY, tileSize.x, tileSize.y, GL_RGBA, GL_UNSIGNED_BYTE, tpixels);
            stbi_image_free(tpixels);
        }
    }

    void LoadBlockTextures(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt; (void)win;
        if (!baseSystem.renderer || !baseSystem.world) { std::cerr << "BlockTextureSystem: Missing RendererContext or WorldContext.\n"; return; }
        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;

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

        if (renderer.atlasTexture != 0) {
            glDeleteTextures(1, &renderer.atlasTexture);
            renderer.atlasTexture = 0;
        }
        for (GLuint& tex : renderer.grassTextures) {
            if (tex != 0) {
                glDeleteTextures(1, &tex);
                tex = 0;
            }
        }
        for (GLuint& tex : renderer.shortGrassTextures) {
            if (tex != 0) {
                glDeleteTextures(1, &tex);
                tex = 0;
            }
        }
        for (GLuint& tex : renderer.oreTextures) {
            if (tex != 0) {
                glDeleteTextures(1, &tex);
                tex = 0;
            }
        }
        for (GLuint& tex : renderer.terrainTextures) {
            if (tex != 0) {
                glDeleteTextures(1, &tex);
                tex = 0;
            }
        }
        if (renderer.waterOverlayTexture != 0) {
            glDeleteTextures(1, &renderer.waterOverlayTexture);
            renderer.waterOverlayTexture = 0;
        }
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

        glGenTextures(1, &renderer.atlasTexture);
        glBindTexture(GL_TEXTURE_2D, renderer.atlasTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(pixels);

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
            overrideAtlasTile(renderer.atlasTexture,
                              tileIndex,
                              renderer.atlasTileSize,
                              renderer.atlasTilesPerRow,
                              renderer.atlasTilesPerCol,
                              texturePath,
                              label);
        };

        // Standalone texture overrides used for fast art iteration.
        overrideMappedAtlasTile("Grass", "Procedures/assets/24x24_grass_block_top_v007.png", "grass");
        overrideMappedAtlasTile("GrassTopV007", "Procedures/assets/24x24_grass_block_top_v007.png", "grass-top-v007");
        overrideMappedAtlasTile("GemRuby", "Procedures/assets/24x24_ruby_block_texture.png", "gem-ruby");
        overrideMappedAtlasTile("GemAmethyst", "Procedures/assets/24x24_amethyst_block_texture.png", "gem-amethyst");
        overrideMappedAtlasTile("GemFlourite", "Procedures/assets/24x24_flourite_block_texture.png", "gem-flourite");
        overrideMappedAtlasTile("GemSilver", "Procedures/assets/24x24_silver_block_texture.png", "gem-silver");

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
                                       std::array<GLuint, 3>& targets,
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
                glGenTextures(1, &targets[i]);
                glBindTexture(GL_TEXTURE_2D, targets[i]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, tpixels);
                stbi_image_free(tpixels);
                loadedCount += 1;
            }
        };
        auto loadOreTextureSet = [&](const std::array<const char*, 4>& paths,
                                     std::array<GLuint, 4>& targets,
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
                glGenTextures(1, &targets[i]);
                glBindTexture(GL_TEXTURE_2D, targets[i]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, tpixels);
                stbi_image_free(tpixels);
                loadedCount += 1;
            }
        };
        auto loadTerrainTextureSet = [&](const std::array<const char*, 2>& paths,
                                         std::array<GLuint, 2>& targets,
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
                glGenTextures(1, &targets[i]);
                glBindTexture(GL_TEXTURE_2D, targets[i]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, tpixels);
                stbi_image_free(tpixels);
                loadedCount += 1;
            }
        };

        const bool standaloneTallGrassEnabled = readRegistryBool(baseSystem, "StandaloneTallGrassTexturesEnabled", true);
        const bool standaloneShortGrassEnabled = readRegistryBool(baseSystem, "StandaloneShortGrassTexturesEnabled", true);
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
                glGenTextures(1, &renderer.waterOverlayTexture);
                glBindTexture(GL_TEXTURE_2D, renderer.waterOverlayTexture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, tpixels);
                stbi_image_free(tpixels);
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

        glBindTexture(GL_TEXTURE_2D, 0);
    }
}
