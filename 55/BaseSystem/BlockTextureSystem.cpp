#pragma once

#include "../Host.h"
#include <fstream>
#include <iostream>
#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"

namespace BlockTextureSystemLogic {
    namespace {
        FaceTextureSet parseFaceTextureSet(const json& entry) {
            FaceTextureSet set;
            if (entry.contains("all")) set.all = entry["all"].get<int>();
            if (entry.contains("top")) set.top = entry["top"].get<int>();
            if (entry.contains("bottom")) set.bottom = entry["bottom"].get<int>();
            if (entry.contains("side")) set.side = entry["side"].get<int>();
            return set;
        }
    }

    void LoadBlockTextures(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt; (void)win;
        if (!baseSystem.renderer || !baseSystem.world) { std::cerr << "BlockTextureSystem: Missing RendererContext or WorldContext.\n"; return; }
        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;

        std::ifstream mapFile("Procedures/assets/atlas.json");
        if (!mapFile.is_open()) { std::cerr << "BlockTextureSystem: Could not open Procedures/assets/atlas.json\n"; return; }

        json atlasData;
        try { atlasData = json::parse(mapFile); }
        catch (...) { std::cerr << "BlockTextureSystem: Failed to parse atlas.json\n"; return; }

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

        int width = 0, height = 0, channels = 0;
        stbi_set_flip_vertically_on_load(true);
        unsigned char* pixels = stbi_load("Procedures/assets/atlas.png", &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) { std::cerr << "BlockTextureSystem: Failed to load Procedures/assets/atlas.png\n"; return; }

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
                std::cerr << "BlockTextureSystem: Missing atlas entry for textureKey '" << proto.textureKey << "'\n";
                continue;
            }
            world.prototypeTextureSets[i] = it->second;
        }
    }
}
