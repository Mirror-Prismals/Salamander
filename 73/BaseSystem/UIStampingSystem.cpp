#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace UIStampingSystemLogic {
    namespace {
        constexpr float kRowSpacing = 72.0f;
        constexpr float kScrollSpeed = 24.0f;

        void replaceAll(std::string& value, const std::string& token, const std::string& replacement) {
            if (value.empty() || token.empty()) return;
            size_t pos = 0;
            while ((pos = value.find(token, pos)) != std::string::npos) {
                value.replace(pos, token.size(), replacement);
                pos += replacement.size();
            }
        }

        std::string replaceTrackTokens(const std::string& value, int trackIndex) {
            std::string result = value;
            replaceAll(result, "{track+1}", std::to_string(trackIndex + 1));
            replaceAll(result, "{track}", std::to_string(trackIndex));
            return result;
        }

        void applyTokens(EntityInstance& target, const EntityInstance& source, int trackIndex) {
            target.actionValue = replaceTrackTokens(source.actionValue, trackIndex);
            target.textKey = replaceTrackTokens(source.textKey, trackIndex);
            target.text = replaceTrackTokens(source.text, trackIndex);
        }

        int findWorldIndex(const LevelContext& level, const std::string& name) {
            for (size_t i = 0; i < level.worlds.size(); ++i) {
                if (level.worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        float clampScroll(float scrollY, int rowCount) {
            int clampedRows = std::max(0, rowCount - 1);
            float minScroll = -kRowSpacing * static_cast<float>(clampedRows);
            if (scrollY < minScroll) return minScroll;
            if (scrollY > 0.0f) return 0.0f;
            return scrollY;
        }
    }

    void UpdateUIStamping(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt; (void)win;
        if (!baseSystem.level || !baseSystem.daw || !baseSystem.instance || !baseSystem.uiStamp) return;

        LevelContext& level = *baseSystem.level;
        DawContext& daw = *baseSystem.daw;
        UIStampingContext& stamp = *baseSystem.uiStamp;

        if (stamp.level != &level) {
            stamp = UIStampingContext{};
            stamp.level = &level;
        }

        if (!stamp.cacheBuilt) {
            stamp.sourceWorldIndex = findWorldIndex(level, stamp.sourceWorldName);
            if (stamp.sourceWorldIndex < 0) {
                return;
            }
            Entity& sourceWorld = level.worlds[stamp.sourceWorldIndex];
            stamp.sourceInstances = sourceWorld.instances;
            stamp.sourceBaseY.clear();
            stamp.sourceBaseY.reserve(stamp.sourceInstances.size());
            for (const auto& inst : stamp.sourceInstances) {
                stamp.sourceBaseY.push_back(inst.position.y);
            }
            stamp.rowSpacing = kRowSpacing;
            stamp.rowWorldIndices.clear();
            stamp.rowWorldIndices.push_back(stamp.sourceWorldIndex);
            stamp.stampedRows = 1;
            for (size_t i = 0; i < sourceWorld.instances.size() && i < stamp.sourceInstances.size(); ++i) {
                applyTokens(sourceWorld.instances[i], stamp.sourceInstances[i], 0);
            }
            stamp.cacheBuilt = true;
        }

        int desiredRows = daw.trackCount > 0 ? daw.trackCount : 1;
        if (desiredRows > stamp.stampedRows) {
            Entity& sourceWorld = level.worlds[stamp.sourceWorldIndex];
            for (int row = stamp.stampedRows; row < desiredRows; ++row) {
                Entity newWorld = sourceWorld;
                newWorld.name = stamp.sourceWorldName + "_" + std::to_string(row);
                newWorld.instances.clear();
                newWorld.instances.reserve(stamp.sourceInstances.size());
                for (size_t i = 0; i < stamp.sourceInstances.size(); ++i) {
                    EntityInstance inst = stamp.sourceInstances[i];
                    inst.instanceID = baseSystem.instance->nextInstanceID++;
                    inst.position.y = stamp.sourceBaseY[i] + static_cast<float>(row) * stamp.rowSpacing;
                    applyTokens(inst, stamp.sourceInstances[i], row);
                    newWorld.instances.push_back(inst);
                }
                level.worlds.push_back(std::move(newWorld));
                stamp.rowWorldIndices.push_back(static_cast<int>(level.worlds.size() - 1));
                stamp.stampedRows += 1;
            }
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            if (baseSystem.daw) baseSystem.daw->uiCacheBuilt = false;
        }

        if (baseSystem.ui && baseSystem.player) {
            UIContext& ui = *baseSystem.ui;
            PlayerContext& player = *baseSystem.player;
            if (ui.active && !ui.loadingActive && player.scrollYOffset != 0.0) {
                stamp.scrollY += static_cast<float>(player.scrollYOffset) * kScrollSpeed;
                stamp.scrollY = clampScroll(stamp.scrollY, stamp.stampedRows);
            }
        }

        for (int row = 0; row < stamp.stampedRows; ++row) {
            if (row < 0 || row >= static_cast<int>(stamp.rowWorldIndices.size())) continue;
            int worldIndex = stamp.rowWorldIndices[row];
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) continue;
            Entity& world = level.worlds[worldIndex];
            size_t count = std::min(world.instances.size(), stamp.sourceBaseY.size());
            float rowOffset = static_cast<float>(row) * stamp.rowSpacing + stamp.scrollY;
            for (size_t i = 0; i < count; ++i) {
                world.instances[i].position.y = stamp.sourceBaseY[i] + rowOffset;
            }
        }
    }
}
