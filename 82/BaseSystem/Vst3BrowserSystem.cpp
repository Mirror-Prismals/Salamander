#pragma once

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

#include "BaseSystem/Vst3Host.h"

namespace DawStateSystemLogic { bool InsertTrackAt(BaseSystem& baseSystem, int trackIndex); }
namespace MidiStateSystemLogic { bool InsertTrackAt(BaseSystem& baseSystem, int trackIndex); }

namespace Vst3BrowserSystemLogic {
    namespace {
        struct RectF {
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
        };

        constexpr float kPadX = 46.0f;
        constexpr float kPadY = 18.0f;
        constexpr float kHeaderHeight = 26.0f;
        constexpr float kRowHeight = 22.0f;
        constexpr float kRowGap = 6.0f;
        constexpr float kSectionGap = 12.0f;
        constexpr float kFontSize = 18.0f;
        constexpr float kLaneLeftMargin = 40.0f;
        constexpr float kLaneRightMargin = 40.0f;
        constexpr float kLaneHeight = 60.0f;
        constexpr float kLaneGap = 12.0f;
        constexpr float kLaneStartY = 260.0f;
        constexpr float kGhostOffsetX = 12.0f;
        constexpr float kGhostOffsetY = 8.0f;
        constexpr float kHitPadX = 12.0f;
        constexpr int kComponentCount = 2;

        int findWorldIndex(const LevelContext& level, const std::string& name) {
            for (size_t i = 0; i < level.worlds.size(); ++i) {
                if (level.worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        EntityInstance* findInstance(LevelContext& level, int worldIndex, int instanceID) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return nullptr;
            auto& insts = level.worlds[worldIndex].instances;
            for (auto& inst : insts) {
                if (inst.instanceID == instanceID) return &inst;
            }
            return nullptr;
        }

        void removeInstances(LevelContext& level, int worldIndex, const std::vector<int>& ids) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return;
            if (ids.empty()) return;
            std::unordered_set<int> idSet(ids.begin(), ids.end());
            auto& insts = level.worlds[worldIndex].instances;
            insts.erase(std::remove_if(insts.begin(), insts.end(),
                                       [&](const EntityInstance& inst) {
                                           return idSet.count(inst.instanceID) != 0;
                                       }),
                        insts.end());
        }

        EntityInstance makeTextInstance(BaseSystem& baseSystem,
                                        const std::vector<Entity>& prototypes,
                                        const std::string& text,
                                        const std::string& controlId) {
            EntityInstance inst = HostLogic::CreateInstance(baseSystem, prototypes, "Text",
                                                           glm::vec3(0.0f), glm::vec3(1.0f));
            inst.textType = "UIOnly";
            inst.text = text;
            inst.controlRole = "label";
            inst.controlId = controlId;
            inst.colorName = "MiraText";
            inst.size = glm::vec3(kFontSize);
            return inst;
        }

        bool hitRect(const RectF& rect, const UIContext& ui) {
            float x = static_cast<float>(ui.cursorX);
            float y = static_cast<float>(ui.cursorY);
            return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
        }

        int computeDropTrackIndex(const PanelContext& panel,
                                  const UIContext& ui,
                                  const UIStampingContext* stamp,
                                  double screenWidth,
                                  int trackCount) {
            if (trackCount <= 0) return -1;
            float x = static_cast<float>(ui.cursorX);
            float y = static_cast<float>(ui.cursorY);
            if (x < panel.mainRect.x || x > panel.mainRect.x + panel.mainRect.w ||
                y < panel.mainRect.y || y > panel.mainRect.y + panel.mainRect.h) {
                return -1;
            }

            float laneLeft = kLaneLeftMargin;
            float laneRight = static_cast<float>(screenWidth) - kLaneRightMargin;
            if (laneRight < laneLeft + 200.0f) laneRight = laneLeft + 200.0f;
            if (x < laneLeft || x > laneRight) return -1;

            float scrollY = stamp ? stamp->scrollY : 0.0f;
            float startY = kLaneStartY + scrollY;
            float rowSpan = kLaneHeight + kLaneGap;
            float laneHalf = kLaneHeight * 0.5f;
            for (int i = 0; i < trackCount; ++i) {
                float centerY = startY + static_cast<float>(i) * rowSpan;
                if (y >= centerY - laneHalf && y <= centerY + laneHalf) {
                    return i;
                }
            }
            return -1;
        }

        int computeDropTrackSlot(const PanelContext& panel,
                                 const UIContext& ui,
                                 const UIStampingContext* stamp,
                                 double screenWidth,
                                 int trackCount) {
            if (trackCount < 0) trackCount = 0;
            float x = static_cast<float>(ui.cursorX);
            float y = static_cast<float>(ui.cursorY);
            if (x < panel.mainRect.x || x > panel.mainRect.x + panel.mainRect.w ||
                y < panel.mainRect.y || y > panel.mainRect.y + panel.mainRect.h) {
                return -1;
            }

            float laneLeft = kLaneLeftMargin;
            float laneRight = static_cast<float>(screenWidth) - kLaneRightMargin;
            if (laneRight < laneLeft + 200.0f) laneRight = laneLeft + 200.0f;
            if (x < laneLeft || x > laneRight) return -1;

            float scrollY = stamp ? stamp->scrollY : 0.0f;
            float startY = kLaneStartY + scrollY;
            float rowSpan = kLaneHeight + kLaneGap;
            float rel = (y - startY) / rowSpan;
            int slot = static_cast<int>(std::floor(rel + 0.5f));
            if (slot < 0) slot = 0;
            if (slot > trackCount) slot = trackCount;
            return slot;
        }

        int computeDropIndexForEmpty(const PanelContext& panel,
                                     const UIContext& ui,
                                     double screenWidth) {
            float x = static_cast<float>(ui.cursorX);
            float y = static_cast<float>(ui.cursorY);
            if (x < panel.mainRect.x || x > panel.mainRect.x + panel.mainRect.w ||
                y < panel.mainRect.y || y > panel.mainRect.y + panel.mainRect.h) {
                return -1;
            }

            float laneLeft = kLaneLeftMargin;
            float laneRight = static_cast<float>(screenWidth) - kLaneRightMargin;
            if (laneRight < laneLeft + 200.0f) laneRight = laneLeft + 200.0f;
            if (x < laneLeft || x > laneRight) return -1;
            return 0;
        }

        const char* componentLabel(int index) {
            switch (index) {
                case 0: return "Audio Track";
                case 1: return "Midi Track";
                default: return "";
            }
        }
    } // namespace

    void UpdateVst3Browser(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt;
        if (!baseSystem.vst3 || !baseSystem.panel || !baseSystem.ui || !baseSystem.level || !baseSystem.instance) return;
        Vst3Context& ctx = *baseSystem.vst3;
        PanelContext& panel = *baseSystem.panel;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;

        if (panel.leftState <= 0.01f) return;
        PanelRect leftRect = (panel.leftRenderRect.w > 0.0f) ? panel.leftRenderRect : panel.leftRect;
        if (leftRect.w <= 1.0f || leftRect.h <= 1.0f) return;

        LevelContext& level = *baseSystem.level;
        int worldIndex = (panel.panelWorldIndex >= 0) ? panel.panelWorldIndex : findWorldIndex(level, "DAWPanelWorld");
        if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return;

        if (!ctx.componentsCacheBuilt || ctx.componentsLevel != &level || ctx.componentsWorldIndex != worldIndex
            || ctx.componentsListCount != static_cast<size_t>(kComponentCount)) {
            removeInstances(level, worldIndex, ctx.componentsInstanceIds);
            ctx.componentsInstanceIds.clear();
            ctx.componentsGhostId = -1;

            EntityInstance header = makeTextInstance(baseSystem, prototypes, "DAW COMPONENTS", "daw_components_header");
            ctx.componentsInstanceIds.push_back(header.instanceID);
            level.worlds[worldIndex].instances.push_back(header);

            for (int i = 0; i < kComponentCount; ++i) {
                std::string controlId = "daw_component_item_" + std::to_string(i);
                EntityInstance item = makeTextInstance(baseSystem, prototypes, componentLabel(i), controlId);
                ctx.componentsInstanceIds.push_back(item.instanceID);
                level.worlds[worldIndex].instances.push_back(item);
            }

            EntityInstance ghost = makeTextInstance(baseSystem, prototypes, "", "daw_component_drag");
            ghost.colorName = "MiraLaneHighlight";
            ctx.componentsGhostId = ghost.instanceID;
            ctx.componentsInstanceIds.push_back(ghost.instanceID);
            level.worlds[worldIndex].instances.push_back(ghost);

            ctx.componentsCacheBuilt = true;
            ctx.componentsLevel = &level;
            ctx.componentsWorldIndex = worldIndex;
            ctx.componentsListCount = static_cast<size_t>(kComponentCount);
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
        }

        if (!ctx.browserCacheBuilt || ctx.browserLevel != &level || ctx.browserWorldIndex != worldIndex
            || ctx.browserListCount != ctx.availablePlugins.size()) {
            removeInstances(level, worldIndex, ctx.browserInstanceIds);
            ctx.browserInstanceIds.clear();
            ctx.browserGhostId = -1;

            EntityInstance header = makeTextInstance(baseSystem, prototypes, "USER_VST3", "track_vst3_header");
            ctx.browserInstanceIds.push_back(header.instanceID);
            level.worlds[worldIndex].instances.push_back(header);

            for (size_t i = 0; i < ctx.availablePlugins.size(); ++i) {
                std::string controlId = "track_vst3_item_" + std::to_string(i);
                EntityInstance item = makeTextInstance(baseSystem, prototypes, ctx.availablePlugins[i].name, controlId);
                ctx.browserInstanceIds.push_back(item.instanceID);
                level.worlds[worldIndex].instances.push_back(item);
            }

            EntityInstance ghost = makeTextInstance(baseSystem, prototypes, "", "track_vst3_drag");
            ghost.colorName = "MiraLaneHighlight";
            ctx.browserGhostId = ghost.instanceID;
            ctx.browserInstanceIds.push_back(ghost.instanceID);
            level.worlds[worldIndex].instances.push_back(ghost);

            ctx.browserCacheBuilt = true;
            ctx.browserLevel = &level;
            ctx.browserWorldIndex = worldIndex;
            ctx.browserListCount = ctx.availablePlugins.size();
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
        }

        RectF componentsHeaderRect{
            leftRect.x + kPadX,
            leftRect.y + kPadY,
            leftRect.w - 2.0f * kPadX,
            kHeaderHeight
        };
        RectF componentsListRect{
            componentsHeaderRect.x,
            componentsHeaderRect.y + componentsHeaderRect.h + kRowGap,
            componentsHeaderRect.w,
            static_cast<float>(kComponentCount) * kRowHeight
        };
        float componentsSectionHeight = kHeaderHeight + kRowGap;
        if (!ctx.componentsCollapsed) {
            componentsSectionHeight += componentsListRect.h + kRowGap;
        }
        float pluginHeaderY = componentsHeaderRect.y + componentsSectionHeight + kSectionGap;

        RectF headerRect{
            componentsHeaderRect.x,
            pluginHeaderY,
            componentsHeaderRect.w,
            kHeaderHeight
        };
        RectF listRect{
            headerRect.x,
            headerRect.y + headerRect.h + kRowGap,
            headerRect.w,
            leftRect.h - (headerRect.y - leftRect.y) - headerRect.h - kPadY - kRowGap
        };
        RectF listHitRect{
            listRect.x - kHitPadX,
            listRect.y,
            listRect.w + kHitPadX,
            listRect.h
        };

        bool overLeft = hitRect({leftRect.x, leftRect.y, leftRect.w, leftRect.h}, ui);
        if (overLeft && ui.mainScrollDelta != 0.0 && !ctx.browserCollapsed) {
            float listHeight = static_cast<float>(ctx.availablePlugins.size()) * kRowHeight;
            float visibleHeight = std::max(0.0f, listRect.h);
            float minScroll = std::min(0.0f, visibleHeight - listHeight);
            ctx.browserScroll += static_cast<float>(ui.mainScrollDelta) * 24.0f;
            ctx.browserScroll = std::clamp(ctx.browserScroll, minScroll, 0.0f);
            ui.mainScrollDelta = 0.0;
        }

        if (ui.uiLeftReleased && hitRect(componentsHeaderRect, ui)) {
            ctx.componentsCollapsed = !ctx.componentsCollapsed;
            ui.consumeClick = true;
        } else if (ui.uiLeftReleased && hitRect(headerRect, ui)) {
            ctx.browserCollapsed = !ctx.browserCollapsed;
            ui.consumeClick = true;
        }

        RectF componentsHitRect{
            componentsListRect.x - kHitPadX,
            componentsListRect.y,
            componentsListRect.w + kHitPadX,
            componentsListRect.h
        };

        if (!ctx.componentsCollapsed && ui.uiLeftPressed && hitRect(componentsHitRect, ui)) {
            float localY = static_cast<float>(ui.cursorY) - componentsListRect.y;
            int index = static_cast<int>(localY / kRowHeight);
            if (index >= 0 && index < kComponentCount) {
                ctx.componentsDragging = true;
                ctx.componentsDragIndex = index;
                ui.consumeClick = true;
            }
        } else if (!ctx.browserCollapsed && ui.uiLeftPressed && hitRect(listHitRect, ui)) {
            float localY = static_cast<float>(ui.cursorY) - listRect.y - ctx.browserScroll;
            int index = static_cast<int>(localY / kRowHeight);
            if (index >= 0 && index < static_cast<int>(ctx.availablePlugins.size())) {
                ctx.browserDragging = true;
                ctx.browserDragIndex = index;
                ui.consumeClick = true;
            }
        }

        if (ctx.componentsDragging && (ui.uiLeftReleased || !ui.uiLeftDown)) {
            int audioTrackCount = baseSystem.daw
                ? static_cast<int>(baseSystem.daw->tracks.size())
                : 0;
            int midiTrackCount = baseSystem.midi ? baseSystem.midi->trackCount : 0;
            int trackCount = audioTrackCount + midiTrackCount;
            int dropSlot = -1;
            int windowWidth = 0;
            int windowHeight = 0;
            if (win) {
                glfwGetWindowSize(win, &windowWidth, &windowHeight);
            }
            double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
            if (trackCount > 0) {
                dropSlot = computeDropTrackSlot(panel, ui, baseSystem.uiStamp.get(), screenWidth, trackCount);
            } else {
                dropSlot = computeDropIndexForEmpty(panel, ui, screenWidth) >= 0 ? 0 : -1;
            }
            if (dropSlot >= 0) {
                if (ctx.componentsDragIndex == 0) {
                    if (DawStateSystemLogic::InsertTrackAt(baseSystem, dropSlot)) {
                        ui.consumeClick = true;
                    }
                } else if (ctx.componentsDragIndex == 1) {
                    if (MidiStateSystemLogic::InsertTrackAt(baseSystem, dropSlot)) {
                        ui.consumeClick = true;
                    }
                }
            }
            ctx.componentsDragging = false;
            ctx.componentsDragIndex = -1;
            if (baseSystem.daw) {
                baseSystem.daw->externalDropActive = false;
                baseSystem.daw->externalDropIndex = -1;
                baseSystem.daw->externalDropType = -1;
            }
        }

        if (ctx.browserDragging && ui.uiLeftReleased) {
            int audioTrackCount = baseSystem.daw
                ? static_cast<int>(baseSystem.daw->tracks.size())
                : 0;
            int midiTrackCount = baseSystem.midi ? baseSystem.midi->trackCount : 0;
            int trackCount = audioTrackCount + midiTrackCount;
            int dropIndex = -1;
            int windowWidth = 0;
            int windowHeight = 0;
            if (win) {
                glfwGetWindowSize(win, &windowWidth, &windowHeight);
            }
            double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
            if (trackCount > 0) {
                dropIndex = computeDropTrackIndex(panel, ui, baseSystem.uiStamp.get(), screenWidth, trackCount);
            }
            if (dropIndex >= 0 && ctx.browserDragIndex >= 0
                && ctx.browserDragIndex < static_cast<int>(ctx.availablePlugins.size())) {
                const auto& available = ctx.availablePlugins[ctx.browserDragIndex];
                int targetIndex = dropIndex;
                if (baseSystem.daw && !baseSystem.daw->laneOrder.empty()
                    && dropIndex < static_cast<int>(baseSystem.daw->laneOrder.size())) {
                    const auto& entry = baseSystem.daw->laneOrder[static_cast<size_t>(dropIndex)];
                    if (entry.type == 0) {
                        targetIndex = entry.trackIndex;
                    } else {
                        targetIndex = audioTrackCount + entry.trackIndex;
                    }
                }
                if (Vst3SystemLogic::AddPluginToTrack(ctx, available, targetIndex, audioTrackCount)) {
                    ui.consumeClick = true;
                }
            }
            ctx.browserDragging = false;
            ctx.browserDragIndex = -1;
        }

        if (!ctx.browserDragging && !ui.uiLeftDown) {
            ctx.browserDragIndex = -1;
        }
        if (!ctx.componentsDragging && !ui.uiLeftDown) {
            ctx.componentsDragIndex = -1;
        }
        if (ctx.componentsDragging) {
            int audioTrackCount = baseSystem.daw
                ? static_cast<int>(baseSystem.daw->tracks.size())
                : 0;
            int midiTrackCount = baseSystem.midi ? baseSystem.midi->trackCount : 0;
            int trackCount = audioTrackCount + midiTrackCount;
            int windowWidth = 0;
            int windowHeight = 0;
            if (win) {
                glfwGetWindowSize(win, &windowWidth, &windowHeight);
            }
            double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
            int dropSlot = -1;
            if (trackCount > 0) {
                dropSlot = computeDropTrackSlot(panel, ui, baseSystem.uiStamp.get(), screenWidth, trackCount);
            } else {
                dropSlot = computeDropIndexForEmpty(panel, ui, screenWidth) >= 0 ? 0 : -1;
            }
            if (ctx.componentsDragIndex == 0 && baseSystem.daw) {
                baseSystem.daw->externalDropActive = dropSlot >= 0;
                baseSystem.daw->externalDropIndex = dropSlot;
                baseSystem.daw->externalDropType = 0;
            } else if (ctx.componentsDragIndex == 1 && baseSystem.midi) {
                if (baseSystem.daw) {
                    baseSystem.daw->externalDropActive = dropSlot >= 0;
                    baseSystem.daw->externalDropIndex = dropSlot;
                    baseSystem.daw->externalDropType = 1;
                }
            }
        }
        if (!ctx.componentsDragging && baseSystem.daw) {
            baseSystem.daw->externalDropActive = false;
            baseSystem.daw->externalDropIndex = -1;
            baseSystem.daw->externalDropType = -1;
        }

        for (size_t i = 0; i < ctx.componentsInstanceIds.size(); ++i) {
            EntityInstance* inst = findInstance(level, worldIndex, ctx.componentsInstanceIds[i]);
            if (!inst) continue;
            if (i == 0) {
                inst->text = ctx.componentsCollapsed ? "> DAW COMPONENTS" : "v DAW COMPONENTS";
                inst->position.x = componentsHeaderRect.x;
                inst->position.y = componentsHeaderRect.y + componentsHeaderRect.h * 0.75f;
                inst->position.z = -1.0f;
                continue;
            }
            if (ctx.componentsGhostId == inst->instanceID) {
                if (ctx.componentsDragging && ctx.componentsDragIndex >= 0
                    && ctx.componentsDragIndex < kComponentCount) {
                    inst->text = componentLabel(ctx.componentsDragIndex);
                    inst->position.x = static_cast<float>(ui.cursorX) + kGhostOffsetX;
                    inst->position.y = static_cast<float>(ui.cursorY) + kGhostOffsetY;
                    inst->position.z = -1.0f;
                } else {
                    inst->text.clear();
                    inst->position = glm::vec3(-10000.0f);
                }
                continue;
            }
            size_t itemIndex = i - 1;
            if (ctx.componentsCollapsed || itemIndex >= static_cast<size_t>(kComponentCount)) {
                inst->text.clear();
                inst->position = glm::vec3(-10000.0f);
                continue;
            }
            inst->text = componentLabel(static_cast<int>(itemIndex));
            float rowY = componentsListRect.y + static_cast<float>(itemIndex) * kRowHeight;
            inst->position.x = componentsListRect.x;
            inst->position.y = rowY + kRowHeight * 0.75f;
            inst->position.z = -1.0f;
        }

        for (size_t i = 0; i < ctx.browserInstanceIds.size(); ++i) {
            EntityInstance* inst = findInstance(level, worldIndex, ctx.browserInstanceIds[i]);
            if (!inst) continue;
            if (i == 0) {
                inst->text = ctx.browserCollapsed ? "> USER_VST3" : "v USER_VST3";
                inst->position.x = headerRect.x;
                inst->position.y = headerRect.y + headerRect.h * 0.75f;
                inst->position.z = -1.0f;
                continue;
            }
            if (ctx.browserGhostId == inst->instanceID) {
                if (ctx.browserDragging && ctx.browserDragIndex >= 0
                    && ctx.browserDragIndex < static_cast<int>(ctx.availablePlugins.size())) {
                    inst->text = ctx.availablePlugins[ctx.browserDragIndex].name;
                    inst->position.x = static_cast<float>(ui.cursorX) + kGhostOffsetX;
                    inst->position.y = static_cast<float>(ui.cursorY) + kGhostOffsetY;
                    inst->position.z = -1.0f;
                } else {
                    inst->text.clear();
                    inst->position = glm::vec3(-10000.0f);
                }
                continue;
            }
            size_t itemIndex = i - 1;
            if (ctx.browserCollapsed || itemIndex >= ctx.availablePlugins.size()) {
                inst->text.clear();
                inst->position = glm::vec3(-10000.0f);
                continue;
            }
            inst->text = ctx.availablePlugins[itemIndex].name;
            float rowY = listRect.y + ctx.browserScroll + static_cast<float>(itemIndex) * kRowHeight;
            inst->position.x = listRect.x;
            inst->position.y = rowY + kRowHeight * 0.75f;
            inst->position.z = -1.0f;
        }
    }
}
