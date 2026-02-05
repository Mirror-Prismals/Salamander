#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace MidiStampingSystemLogic {

    namespace {
        int getTrackCount(const MidiContext& midi) {
            return static_cast<int>(midi.tracks.size());
        }

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
            target.actionType = replaceTrackTokens(source.actionType, trackIndex);
            target.actionKey = replaceTrackTokens(source.actionKey, trackIndex);
            target.actionValue = replaceTrackTokens(source.actionValue, trackIndex);
            target.textKey = replaceTrackTokens(source.textKey, trackIndex);
            target.text = replaceTrackTokens(source.text, trackIndex);
            target.controlId = replaceTrackTokens(source.controlId, trackIndex);
            target.controlRole = replaceTrackTokens(source.controlRole, trackIndex);
            target.styleId = replaceTrackTokens(source.styleId, trackIndex);
            target.uiState = replaceTrackTokens(source.uiState, trackIndex);
        }

        int findWorldIndex(const LevelContext& level, const std::string& name) {
            for (size_t i = 0; i < level.worlds.size(); ++i) {
                if (level.worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        void stampRow(Entity& world,
                      const MidiContext& midi,
                      int row,
                      bool preserveIds,
                      BaseSystem& baseSystem) {
            world.instances.resize(midi.stampSourceInstances.size());
            for (size_t i = 0; i < midi.stampSourceInstances.size(); ++i) {
                EntityInstance inst = midi.stampSourceInstances[i];
                if (preserveIds && i < world.instances.size()) {
                    inst.instanceID = world.instances[i].instanceID;
                    if (inst.instanceID <= 0 && baseSystem.instance) {
                        inst.instanceID = baseSystem.instance->nextInstanceID++;
                    }
                } else if (baseSystem.instance) {
                    inst.instanceID = baseSystem.instance->nextInstanceID++;
                }
                inst.position.y = midi.stampSourceBaseY[i] + static_cast<float>(row) * midi.stampRowSpacing;
                applyTokens(inst, midi.stampSourceInstances[i], row);
                world.instances[i] = std::move(inst);
            }
        }

        void updateMidiStamping(BaseSystem& baseSystem, MidiContext& midi, const DawContext& daw) {
            if (!baseSystem.level) return;
            if (!daw.laneOrder.empty()) {
                LevelContext& level = *baseSystem.level;
                for (int worldIndex : midi.stampRowWorldIndices) {
                    if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) continue;
                    level.worlds[worldIndex].instances.clear();
                }
                midi.stampRowWorldIndices.clear();
                midi.stampRowCount = 0;
                midi.stampCacheBuilt = false;
                midi.stampSourceWorldIndex = -1;
                return;
            }
            LevelContext& level = *baseSystem.level;

            if (!midi.stampCacheBuilt || midi.uiLevel != baseSystem.level.get()) {
                midi.stampSourceWorldIndex = findWorldIndex(level, midi.stampSourceWorldName);
                midi.stampSourceInstances.clear();
                midi.stampSourceBaseY.clear();
                midi.stampRowWorldIndices.clear();
                midi.stampRowCount = 0;
                if (midi.stampSourceWorldIndex >= 0) {
                    Entity& sourceWorld = level.worlds[midi.stampSourceWorldIndex];
                    midi.stampSourceInstances = sourceWorld.instances;
                    midi.stampSourceBaseY.reserve(sourceWorld.instances.size());
                    for (const auto& inst : sourceWorld.instances) {
                        midi.stampSourceBaseY.push_back(inst.position.y);
                    }
                    midi.stampRowWorldIndices.push_back(midi.stampSourceWorldIndex);
                    midi.stampRowCount = 1;
                }
                midi.stampRowSpacing = baseSystem.uiStamp ? baseSystem.uiStamp->rowSpacing : 72.0f;
                midi.stampCacheBuilt = true;
                if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
                if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
                midi.uiCacheBuilt = false;
            }

            int desiredRows = getTrackCount(midi);
            if (midi.stampSourceWorldIndex >= 0 && midi.stampSourceWorldIndex < static_cast<int>(level.worlds.size())) {
                Entity& sourceWorld = level.worlds[midi.stampSourceWorldIndex];
                if (desiredRows > 0) {
                    stampRow(sourceWorld, midi, 0, true, baseSystem);
                } else {
                    sourceWorld.instances.clear();
                }
            }

            if (desiredRows > static_cast<int>(midi.stampRowWorldIndices.size())) {
                if (midi.stampSourceWorldIndex >= 0 && midi.stampSourceWorldIndex < static_cast<int>(level.worlds.size())) {
                    Entity& sourceWorld = level.worlds[midi.stampSourceWorldIndex];
                    for (int row = static_cast<int>(midi.stampRowWorldIndices.size()); row < desiredRows; ++row) {
                        Entity newWorld = sourceWorld;
                        newWorld.name = midi.stampSourceWorldName + "_" + std::to_string(row);
                        stampRow(newWorld, midi, row, false, baseSystem);
                        level.worlds.push_back(std::move(newWorld));
                        midi.stampRowWorldIndices.push_back(static_cast<int>(level.worlds.size() - 1));
                    }
                    midi.stampRowCount = static_cast<int>(midi.stampRowWorldIndices.size());
                    if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
                    if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
                    midi.uiCacheBuilt = false;
                }
            } else if (desiredRows < static_cast<int>(midi.stampRowWorldIndices.size())) {
                for (int row = desiredRows; row < static_cast<int>(midi.stampRowWorldIndices.size()); ++row) {
                    int worldIndex = midi.stampRowWorldIndices[row];
                    if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) continue;
                    level.worlds[worldIndex].instances.clear();
                }
                if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
                if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
                midi.uiCacheBuilt = false;
            }

            float scrollY = baseSystem.uiStamp ? baseSystem.uiStamp->panelScrollY : 0.0f;
            float rowSpacing = baseSystem.uiStamp ? baseSystem.uiStamp->rowSpacing : 72.0f;
            int audioRowCount = baseSystem.uiStamp
                ? baseSystem.uiStamp->stampedRows
                : static_cast<int>(daw.tracks.size());
            float baseOffset = static_cast<float>(audioRowCount) * rowSpacing + scrollY;
            for (int row = 0; row < desiredRows; ++row) {
                if (row < 0 || row >= static_cast<int>(midi.stampRowWorldIndices.size())) continue;
                int worldIndex = midi.stampRowWorldIndices[row];
                if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) continue;
                Entity& world = level.worlds[worldIndex];
                size_t count = std::min(world.instances.size(), midi.stampSourceBaseY.size());
                float rowOffset = baseOffset + static_cast<float>(row) * rowSpacing;
                for (size_t i = 0; i < count; ++i) {
                    world.instances[i].position.y = midi.stampSourceBaseY[i] + rowOffset;
                }
            }
        }
    }

    void UpdateMidiStamping(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow*) {
        if (!baseSystem.midi || !baseSystem.daw) return;
        MidiContext& midi = *baseSystem.midi;
        DawContext& daw = *baseSystem.daw;
        updateMidiStamping(baseSystem, midi, daw);
    }
}
