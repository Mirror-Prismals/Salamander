#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "stb_easy_font.h"

namespace DawLaneTimelineSystemLogic {
    struct LaneLayout;
    bool hasDawUiWorld(const LevelContext& level);
    LaneLayout ComputeLaneLayout(const BaseSystem& baseSystem, const DawContext& daw, GLFWwindow* win);
    std::vector<int> BuildAudioLaneIndex(const DawContext& daw, int audioTrackCount);
}
namespace DawLaneResourceSystemLogic {
    using UiVertex = DawLaneTimelineSystemLogic::UiVertex;
    const std::vector<UiVertex>& GetLaneVertices();
    std::vector<UiVertex>& GetLaneVerticesMutable();
    size_t GetLaneStaticVertexCount();
    void SetLaneTotalVertexCount(size_t count);
    size_t GetLaneTotalVertexCount();
}
namespace DawIOSystemLogic {
    double BarSamples(const DawContext& daw);
    int64_t BarDisplayToSample(const DawContext& daw, int displayBar);
}

namespace DawLaneRenderSystemLogic {
    namespace {
        constexpr float kLaneAlphaDefault = 0.85f;
        constexpr float kRulerHeight = 13.0f;
        constexpr float kRulerInset = 10.0f;
        constexpr float kRulerSideInset = -15.0f;
        constexpr float kRulerLowerOffset = 0.0f;
        constexpr float kRulerGap = 6.0f;
        constexpr float kPlayheadHandleSize = 12.0f;
        constexpr float kPlayheadHandleYOffset = 14.0f;
        constexpr float kTrackHandleSize = 60.0f;
        constexpr float kTrackHandleInset = 12.0f;
        constexpr float kClipHorizontalPad = 2.0f;
        constexpr float kClipVerticalInset = 0.0f;
        constexpr float kClipMinHeight = 2.0f;
        constexpr float kClipLipMinHeight = 6.0f;
        constexpr float kClipLipMaxHeight = 12.0f;
        constexpr float kTakeRowGap = 4.0f;
        constexpr float kTakeRowSpacing = 2.0f;
        constexpr float kTakeRowMinHeight = 10.0f;
        constexpr float kTakeRowMaxHeight = 18.0f;

        glm::vec2 pixelToNDC(const glm::vec2& pixel, double width, double height) {
            float ndcX = static_cast<float>((pixel.x / width) * 2.0 - 1.0);
            float ndcY = static_cast<float>(1.0 - (pixel.y / height) * 2.0);
            return {ndcX, ndcY};
        }

        void pushQuad(std::vector<DawLaneResourceSystemLogic::UiVertex>& verts,
                      const glm::vec2& a,
                      const glm::vec2& b,
                      const glm::vec2& c,
                      const glm::vec2& d,
                      const glm::vec3& color) {
            verts.push_back({a, color});
            verts.push_back({b, color});
            verts.push_back({c, color});
            verts.push_back({a, color});
            verts.push_back({c, color});
            verts.push_back({d, color});
        }

        void pushText(std::vector<DawLaneResourceSystemLogic::UiVertex>& verts,
                      float x,
                      float y,
                      const char* text,
                      const glm::vec3& color,
                      double width,
                      double height) {
            if (!text || text[0] == '\0') return;
            char buffer[99999];
            int quads = stb_easy_font_print(x, y, const_cast<char*>(text), nullptr, buffer, sizeof(buffer));
            float* data = reinterpret_cast<float*>(buffer);
            for (int i = 0; i < quads; ++i) {
                int base = i * 16;
                glm::vec2 v0{data[base + 0], data[base + 1]};
                glm::vec2 v1{data[base + 4], data[base + 5]};
                glm::vec2 v2{data[base + 8], data[base + 9]};
                glm::vec2 v3{data[base + 12], data[base + 13]};
                pushQuad(verts,
                         pixelToNDC(v0, width, height),
                         pixelToNDC(v1, width, height),
                         pixelToNDC(v2, width, height),
                         pixelToNDC(v3, width, height),
                         color);
            }
        }

        struct Rect {
            float left = 0.0f;
            float right = 0.0f;
            float top = 0.0f;
            float bottom = 0.0f;
        };

        struct ExportDialogLayout {
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            Rect closeBtn;
            Rect startMinus;
            Rect startPlus;
            Rect endMinus;
            Rect endPlus;
            Rect folderBtn;
            Rect exportBtn;
            Rect cancelBtn;
            Rect progressBar;
            std::array<Rect, DawContext::kBusCount> stemRows{};
        };

        struct SettingsDialogLayout {
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            float listRowHeight = 24.0f;
            float listPad = 6.0f;
            Rect closeBtn;
            Rect tabTheme;
            Rect listRect;
            Rect applyBtn;
            Rect createBtn;
            Rect editBtn;
            Rect deleteBtn;
            Rect backBtn;
            Rect saveBtn;
            Rect nameField;
            Rect bgField;
            Rect panelField;
            Rect buttonField;
            Rect pianoField;
            Rect pianoAccentField;
            Rect laneField;
        };

        struct AutomationParamMenuLayout {
            bool valid = false;
            Rect buttonRect;
            Rect menuRect;
            float rowHeight = 18.0f;
            float padding = 6.0f;
        };

        Rect makeRect(float x, float y, float w, float h) {
            Rect rect;
            rect.left = x;
            rect.right = x + w;
            rect.top = y;
            rect.bottom = y + h;
            return rect;
        }

        int parseTrackIndex(const std::string& value, int trackCount) {
            if (value.empty()) return -1;
            try {
                int idx = std::stoi(value);
                if (idx < 0 || idx >= trackCount) return -1;
                return idx;
            } catch (...) {
                return -1;
            }
        }

        int parseTrackIndexFromControlId(const std::string& controlId, int trackCount) {
            constexpr const char* kPrefix = "auto_track_";
            if (controlId.rfind(kPrefix, 0) != 0) return -1;
            size_t start = std::char_traits<char>::length(kPrefix);
            size_t end = controlId.find('_', start);
            if (end == std::string::npos) return -1;
            return parseTrackIndex(controlId.substr(start, end - start), trackCount);
        }

        bool findAutomationParamButtonRect(const DawContext& daw, int trackIndex, Rect& outRect) {
            int trackCount = static_cast<int>(daw.automationTracks.size());
            if (trackIndex < 0 || trackIndex >= trackCount) return false;
            for (auto* instPtr : daw.trackInstances) {
                if (!instPtr) continue;
                const EntityInstance& inst = *instPtr;
                if (inst.actionType != "DawAutomationTrack") continue;
                if (inst.actionKey != "target_param") continue;
                int instTrack = parseTrackIndex(inst.actionValue, trackCount);
                if (instTrack < 0) {
                    instTrack = parseTrackIndexFromControlId(inst.controlId, trackCount);
                }
                if (instTrack != trackIndex) continue;
                outRect = makeRect(inst.position.x - inst.size.x,
                                   inst.position.y - inst.size.y,
                                   inst.size.x * 2.0f,
                                   inst.size.y * 2.0f);
                return true;
            }
            return false;
        }

        AutomationParamMenuLayout computeAutomationParamMenuLayout(const DawContext& daw,
                                                                   const DawLaneTimelineSystemLogic::LaneLayout& layout) {
            AutomationParamMenuLayout out;
            if (!daw.automationParamMenuOpen) return out;
            if (daw.automationParamMenuTrack < 0
                || daw.automationParamMenuTrack >= static_cast<int>(daw.automationTracks.size())) return out;
            if (daw.automationParamMenuLabels.empty()) return out;
            if (!findAutomationParamButtonRect(daw, daw.automationParamMenuTrack, out.buttonRect)) return out;

            const float menuWidth = 180.0f;
            const float rowHeight = out.rowHeight;
            const float pad = out.padding;
            const float menuHeight = static_cast<float>(daw.automationParamMenuLabels.size()) * rowHeight + pad * 2.0f;
            float x = out.buttonRect.left;
            float y = out.buttonRect.bottom + 6.0f;
            if (x + menuWidth > layout.screenWidth - 8.0f) {
                x = static_cast<float>(layout.screenWidth) - menuWidth - 8.0f;
            }
            if (x < 8.0f) x = 8.0f;
            if (y + menuHeight > layout.screenHeight - 8.0f) {
                y = out.buttonRect.top - 6.0f - menuHeight;
            }
            if (y < 8.0f) y = 8.0f;
            out.menuRect = makeRect(x, y, menuWidth, menuHeight);
            out.valid = true;
            return out;
        }

        ExportDialogLayout computeExportDialogLayout(const DawLaneTimelineSystemLogic::LaneLayout& layout) {
            ExportDialogLayout out;
            out.w = 540.0f;
            out.h = 360.0f;
            out.x = std::max(24.0f, static_cast<float>((layout.screenWidth - out.w) * 0.5));
            out.y = std::max(24.0f, static_cast<float>((layout.screenHeight - out.h) * 0.5) - 30.0f);
            const float closeSize = 24.0f;
            out.closeBtn = makeRect(out.x + out.w - closeSize - 12.0f, out.y + 10.0f, closeSize, closeSize);
            out.startMinus = makeRect(out.x + out.w - 180.0f, out.y + 58.0f, 22.0f, 22.0f);
            out.startPlus = makeRect(out.x + out.w - 52.0f, out.y + 58.0f, 22.0f, 22.0f);
            out.endMinus = makeRect(out.x + out.w - 180.0f, out.y + 90.0f, 22.0f, 22.0f);
            out.endPlus = makeRect(out.x + out.w - 52.0f, out.y + 90.0f, 22.0f, 22.0f);
            out.folderBtn = makeRect(out.x + out.w - 120.0f, out.y + 126.0f, 86.0f, 24.0f);
            for (int i = 0; i < DawContext::kBusCount; ++i) {
                out.stemRows[static_cast<size_t>(i)] = makeRect(out.x + 24.0f,
                                                                out.y + 164.0f + static_cast<float>(i) * 34.0f,
                                                                out.w - 48.0f,
                                                                26.0f);
            }
            out.progressBar = makeRect(out.x + 24.0f, out.y + out.h - 58.0f, out.w - 48.0f, 14.0f);
            out.cancelBtn = makeRect(out.x + out.w - 202.0f, out.y + out.h - 34.0f, 82.0f, 22.0f);
            out.exportBtn = makeRect(out.x + out.w - 106.0f, out.y + out.h - 34.0f, 82.0f, 22.0f);
            return out;
        }

        SettingsDialogLayout computeSettingsDialogLayout(const DawLaneTimelineSystemLogic::LaneLayout& layout) {
            SettingsDialogLayout out;
            out.w = 640.0f;
            out.h = 390.0f;
            out.x = std::max(24.0f, static_cast<float>((layout.screenWidth - out.w) * 0.5));
            out.y = std::max(24.0f, static_cast<float>((layout.screenHeight - out.h) * 0.5) - 24.0f);
            const float closeSize = 24.0f;
            out.closeBtn = makeRect(out.x + out.w - closeSize - 12.0f, out.y + 10.0f, closeSize, closeSize);
            out.tabTheme = makeRect(out.x + 18.0f, out.y + 38.0f, 92.0f, 24.0f);

            const float bodyTop = out.y + 74.0f;
            const float bodyBottom = out.y + out.h - 46.0f;
            const float bodyHeight = std::max(0.0f, bodyBottom - bodyTop);
            out.listRect = makeRect(out.x + 18.0f, bodyTop, 260.0f, bodyHeight - 38.0f);

            const float btnW = 82.0f;
            const float btnGap = 10.0f;
            const float rowY = out.y + out.h - 34.0f;
            const float right = out.x + out.w - 24.0f;
            out.deleteBtn = makeRect(right - btnW, rowY, btnW, 22.0f);
            out.editBtn = makeRect(out.deleteBtn.left - btnGap - btnW, rowY, btnW, 22.0f);
            out.createBtn = makeRect(out.editBtn.left - btnGap - btnW, rowY, btnW, 22.0f);
            out.applyBtn = makeRect(out.createBtn.left - btnGap - btnW, rowY, btnW, 22.0f);
            out.backBtn = makeRect(out.x + out.w - 214.0f, out.y + out.h - 34.0f, 88.0f, 22.0f);
            out.saveBtn = makeRect(out.x + out.w - 112.0f, out.y + out.h - 34.0f, 88.0f, 22.0f);

            float fieldX = out.x + 314.0f;
            float fieldW = out.w - 338.0f;
            out.nameField = makeRect(fieldX, bodyTop + 18.0f, fieldW, 28.0f);
            out.bgField = makeRect(fieldX, bodyTop + 56.0f, fieldW, 28.0f);
            out.panelField = makeRect(fieldX, bodyTop + 94.0f, fieldW, 28.0f);
            out.buttonField = makeRect(fieldX, bodyTop + 132.0f, fieldW, 28.0f);
            out.pianoField = makeRect(fieldX, bodyTop + 170.0f, fieldW, 28.0f);
            out.pianoAccentField = makeRect(fieldX, bodyTop + 208.0f, fieldW, 28.0f);
            out.laneField = makeRect(fieldX, bodyTop + 246.0f, fieldW, 28.0f);
            return out;
        }

        Rect settingsThemeRowRect(const SettingsDialogLayout& layout, int rowIndex) {
            float y = layout.listRect.top + layout.listPad + static_cast<float>(rowIndex) * layout.listRowHeight;
            return makeRect(layout.listRect.left + 2.0f,
                            y,
                            (layout.listRect.right - layout.listRect.left) - 4.0f,
                            layout.listRowHeight - 2.0f);
        }

        void pushRect(std::vector<DawLaneResourceSystemLogic::UiVertex>& verts,
                      const Rect& rect,
                      const glm::vec3& color,
                      double width,
                      double height) {
            pushQuad(verts,
                     pixelToNDC({rect.left, rect.top}, width, height),
                     pixelToNDC({rect.right, rect.top}, width, height),
                     pixelToNDC({rect.right, rect.bottom}, width, height),
                     pixelToNDC({rect.left, rect.bottom}, width, height),
                     color);
        }

        void pushBeveledRect(std::vector<DawLaneResourceSystemLogic::UiVertex>& verts,
                             const Rect& rect,
                             float depth,
                             const glm::vec3& front,
                             const glm::vec3& top,
                             const glm::vec3& left,
                             double width,
                             double height) {
            const glm::vec2 a{rect.left, rect.top};
            const glm::vec2 b{rect.right, rect.top};
            const glm::vec2 c{rect.right, rect.bottom};
            const glm::vec2 d{rect.left, rect.bottom};
            const glm::vec2 ta = a;
            const glm::vec2 tb = b;
            const glm::vec2 tc{b.x - depth, b.y - depth};
            const glm::vec2 td{a.x - depth, a.y - depth};
            const glm::vec2 la = a;
            const glm::vec2 lb = d;
            const glm::vec2 lc{d.x - depth, d.y - depth};
            const glm::vec2 ld{a.x - depth, a.y - depth};
            pushQuad(verts,
                     pixelToNDC(a, width, height),
                     pixelToNDC(b, width, height),
                     pixelToNDC(c, width, height),
                     pixelToNDC(d, width, height),
                     front);
            pushQuad(verts,
                     pixelToNDC(ta, width, height),
                     pixelToNDC(tb, width, height),
                     pixelToNDC(tc, width, height),
                     pixelToNDC(td, width, height),
                     top);
            pushQuad(verts,
                     pixelToNDC(la, width, height),
                     pixelToNDC(lb, width, height),
                     pixelToNDC(lc, width, height),
                     pixelToNDC(ld, width, height),
                     left);
        }

        void computeClipRect(float centerY,
                             float laneHalfH,
                             float& top,
                             float& bottom,
                             float& lipBottom) {
            top = centerY - laneHalfH + kClipVerticalInset;
            bottom = centerY + laneHalfH - kClipVerticalInset;
            if (bottom < top + kClipMinHeight) {
                float mid = (top + bottom) * 0.5f;
                top = mid - (kClipMinHeight * 0.5f);
                bottom = mid + (kClipMinHeight * 0.5f);
            }
            float lipHeight = std::clamp((bottom - top) * 0.18f, kClipLipMinHeight, kClipLipMaxHeight);
            lipBottom = std::min(bottom, top + lipHeight);
        }

        float takeRowHeight(float laneHalfH) {
            float laneHeight = laneHalfH * 2.0f;
            return std::clamp(laneHeight * 0.26f, kTakeRowMinHeight, kTakeRowMaxHeight);
        }
    }

    void UpdateDawLaneRender(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow* win) {
        if (!baseSystem.ui || !baseSystem.daw || !baseSystem.renderer || !baseSystem.world || !baseSystem.level || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        if (baseSystem.midi && baseSystem.midi->pianoRollActive) return;
        if (!DawLaneTimelineSystemLogic::hasDawUiWorld(*baseSystem.level)) return;

        DawContext& daw = *baseSystem.daw;
        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;

        auto& vertices = DawLaneResourceSystemLogic::GetLaneVerticesMutable();
        size_t staticCount = DawLaneResourceSystemLogic::GetLaneStaticVertexCount();
        if (staticCount == 0 || !renderer.uiColorShader) return;
        if (vertices.size() < staticCount) return;
        vertices.resize(staticCount);

        const auto layout = DawLaneTimelineSystemLogic::ComputeLaneLayout(baseSystem, daw, win);
        const int audioTrackCount = layout.audioTrackCount;
        const int laneCount = layout.laneCount;
        const float laneLeft = layout.laneLeft;
        const float laneRight = layout.laneRight;
        const float laneHalfH = layout.laneHalfH;
        const float rowSpan = layout.rowSpan;
        const float startY = layout.startY;
        const float topBound = layout.topBound;
        const float laneBottomBound = layout.laneBottomBound;
        const float visualBottomBound = layout.visualBottomBound;
        const double secondsPerScreen = layout.secondsPerScreen;
        const float handleY = layout.handleY;
        const float handleHalf = kPlayheadHandleSize * 0.5f;
        const float rulerTopY = layout.rulerTopY;
        const float rulerBottomY = layout.rulerBottomY;
        const float rulerLeft = layout.rulerLeft;
        const float rulerRight = layout.rulerRight;
        const float verticalRulerLeft = layout.verticalRulerLeft;
        const float verticalRulerRight = layout.verticalRulerRight;
        const float verticalRulerTop = layout.verticalRulerTop;
        const float verticalRulerBottom = layout.verticalRulerBottom;

        int previewSlot = -1;
        if (laneCount > 0 && daw.dragActive && daw.dragLaneType == 0) {
            previewSlot = daw.dragDropIndex;
        } else if (daw.externalDropActive && daw.externalDropType == 0) {
            previewSlot = daw.externalDropIndex;
        }
        const bool previewingAudioLaneDrag = (previewSlot >= 0
            && daw.dragActive
            && daw.dragLaneType == 0
            && daw.dragLaneIndex >= 0);
        const int draggedLaneIndex = daw.dragLaneIndex;
        auto computeDisplayIndex = [&](int laneIndex) -> int {
            int displayIndex = laneIndex;
            if (previewSlot < 0) return displayIndex;
            if (previewingAudioLaneDrag) {
                if (laneIndex == draggedLaneIndex) return -1;
                if (laneIndex > draggedLaneIndex) {
                    displayIndex -= 1;
                }
            }
            if (displayIndex >= previewSlot) {
                displayIndex += 1;
            }
            return displayIndex;
        };
        const auto audioLaneIndex = DawLaneTimelineSystemLogic::BuildAudioLaneIndex(daw, audioTrackCount);

        double playheadSec = static_cast<double>(daw.playheadSample.load(std::memory_order_relaxed)) / static_cast<double>(daw.sampleRate);
        double offsetSec = static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate);
        double tNorm = secondsPerScreen > 0.0 ? (playheadSec - offsetSec) / secondsPerScreen : 0.0;
        if (tNorm < 0.0) tNorm = 0.0;
        if (tNorm > 1.0) tNorm = 1.0;
        float playheadX = static_cast<float>(laneLeft + (laneRight - laneLeft) * tNorm);
        glm::vec3 playheadColor(0.2f, 0.2f, 0.2f);
        auto itText = world.colorLibrary.find("MiraText");
        if (itText != world.colorLibrary.end()) {
            playheadColor = itText->second;
        }
        float lineWidth = 0.5f;
        glm::vec2 pa(playheadX - lineWidth, topBound);
        glm::vec2 pb(playheadX + lineWidth, topBound);
        glm::vec2 pc(playheadX + lineWidth, visualBottomBound);
        glm::vec2 pd(playheadX - lineWidth, visualBottomBound);
        bool showPlayhead = true;

        {
            glm::vec3 laneHighlight(0.2f, 0.2f, 0.2f);
            glm::vec3 laneShadow(0.08f, 0.08f, 0.08f);
            auto itHighlight = world.colorLibrary.find("MiraLaneHighlight");
            if (itHighlight != world.colorLibrary.end()) {
                laneHighlight = itHighlight->second;
            }
            auto itShadow = world.colorLibrary.find("MiraLaneShadow");
            if (itShadow != world.colorLibrary.end()) {
                laneShadow = itShadow->second;
            }

            glm::vec3 rulerFront = laneHighlight;
            glm::vec3 rulerTop = glm::clamp(laneHighlight + glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f));
            glm::vec3 rulerSide = glm::clamp(laneShadow - glm::vec3(0.02f), glm::vec3(0.0f), glm::vec3(1.0f));
            auto itFront = world.colorLibrary.find("ButtonFront");
            if (itFront != world.colorLibrary.end()) {
                rulerFront = itFront->second;
            }
            auto itTop = world.colorLibrary.find("ButtonTopHighlight");
            if (itTop != world.colorLibrary.end()) {
                rulerTop = itTop->second;
            }
            auto itSide = world.colorLibrary.find("ButtonSideShadow");
            if (itSide != world.colorLibrary.end()) {
                rulerSide = itSide->second;
            }
            float rulerTopY2 = startY - laneHalfH - (kRulerHeight + kRulerInset) + kRulerLowerOffset;
            float rulerBottomY2 = rulerTopY2 + kRulerHeight;
            if (rulerTopY2 < 0.0f) {
                float shift = -rulerTopY2;
                rulerTopY2 += shift;
                rulerBottomY2 += shift;
            }
            float bevelDepth = 6.0f;
            float rulerLeft2 = laneLeft + kRulerSideInset;
            float rulerRight2 = laneRight - kRulerSideInset;
            glm::vec2 rFrontA(rulerLeft2, rulerTopY2);
            glm::vec2 rFrontB(rulerRight2, rulerTopY2);
            glm::vec2 rFrontC(rulerRight2, rulerBottomY2);
            glm::vec2 rFrontD(rulerLeft2, rulerBottomY2);
            glm::vec2 rTopA = rFrontA;
            glm::vec2 rTopB = rFrontB;
            glm::vec2 rTopC(rFrontB.x - bevelDepth, rFrontB.y - bevelDepth);
            glm::vec2 rTopD(rFrontA.x - bevelDepth, rFrontA.y - bevelDepth);
            glm::vec2 rLeftA = rFrontA;
            glm::vec2 rLeftB = rFrontD;
            glm::vec2 rLeftC(rFrontD.x - bevelDepth, rFrontD.y - bevelDepth);
            glm::vec2 rLeftD(rFrontA.x - bevelDepth, rFrontA.y - bevelDepth);
            pushQuad(vertices,
                     pixelToNDC(rFrontA, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(rFrontB, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(rFrontC, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(rFrontD, layout.screenWidth, layout.screenHeight),
                     rulerFront);
            pushQuad(vertices,
                     pixelToNDC(rTopA, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(rTopB, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(rTopC, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(rTopD, layout.screenWidth, layout.screenHeight),
                     rulerTop);
            pushQuad(vertices,
                     pixelToNDC(rLeftA, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(rLeftB, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(rLeftC, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(rLeftD, layout.screenWidth, layout.screenHeight),
                     rulerSide);

            double offsetSec2 = (daw.sampleRate > 0.0)
                ? static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate)
                : 0.0;
            double loopStartSec = (daw.sampleRate > 0.0)
                ? static_cast<double>(daw.loopStartSamples) / static_cast<double>(daw.sampleRate)
                : 0.0;
            double loopEndSec = (daw.sampleRate > 0.0)
                ? static_cast<double>(daw.loopEndSamples) / static_cast<double>(daw.sampleRate)
                : 0.0;
            float loopStartX = static_cast<float>(laneLeft + (laneRight - laneLeft) * ((loopStartSec - offsetSec2) / secondsPerScreen));
            float loopEndX = static_cast<float>(laneLeft + (laneRight - laneLeft) * ((loopEndSec - offsetSec2) / secondsPerScreen));
            if (loopEndX < loopStartX) std::swap(loopStartX, loopEndX);
            float upperBottom = rulerTopY2 - kRulerGap;
            float upperTop = upperBottom - kRulerHeight;
            float loopLeft = std::clamp(loopStartX, rulerLeft2, rulerRight2);
            float loopRight = std::clamp(loopEndX, rulerLeft2, rulerRight2);
            if (loopRight < loopLeft + 1.0f) loopRight = loopLeft + 1.0f;
            glm::vec2 uFrontA(loopLeft, upperTop);
            glm::vec2 uFrontB(loopRight, upperTop);
            glm::vec2 uFrontC(loopRight, upperBottom);
            glm::vec2 uFrontD(loopLeft, upperBottom);
            glm::vec2 uTopA = uFrontA;
            glm::vec2 uTopB = uFrontB;
            glm::vec2 uTopC(uFrontB.x - bevelDepth, uFrontB.y - bevelDepth);
            glm::vec2 uTopD(uFrontA.x - bevelDepth, uFrontA.y - bevelDepth);
            glm::vec2 uLeftA = uFrontA;
            glm::vec2 uLeftB = uFrontD;
            glm::vec2 uLeftC(uFrontD.x - bevelDepth, uFrontD.y - bevelDepth);
            glm::vec2 uLeftD(uFrontA.x - bevelDepth, uFrontA.y - bevelDepth);
            pushQuad(vertices,
                     pixelToNDC(uFrontA, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(uFrontB, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(uFrontC, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(uFrontD, layout.screenWidth, layout.screenHeight),
                     rulerFront);
            pushQuad(vertices,
                     pixelToNDC(uTopA, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(uTopB, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(uTopC, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(uTopD, layout.screenWidth, layout.screenHeight),
                     rulerTop);
            pushQuad(vertices,
                     pixelToNDC(uLeftA, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(uLeftB, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(uLeftC, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(uLeftD, layout.screenWidth, layout.screenHeight),
                     rulerSide);

            // Left vertical ruler for lane zoom/pan.
            glm::vec2 vFrontA(verticalRulerLeft, verticalRulerTop);
            glm::vec2 vFrontB(verticalRulerRight, verticalRulerTop);
            glm::vec2 vFrontC(verticalRulerRight, verticalRulerBottom);
            glm::vec2 vFrontD(verticalRulerLeft, verticalRulerBottom);
            glm::vec2 vTopA = vFrontA;
            glm::vec2 vTopB = vFrontB;
            glm::vec2 vTopC(vFrontB.x - bevelDepth, vFrontB.y - bevelDepth);
            glm::vec2 vTopD(vFrontA.x - bevelDepth, vFrontA.y - bevelDepth);
            glm::vec2 vLeftA = vFrontA;
            glm::vec2 vLeftB = vFrontD;
            glm::vec2 vLeftC(vFrontD.x - bevelDepth, vFrontD.y - bevelDepth);
            glm::vec2 vLeftD(vFrontA.x - bevelDepth, vFrontA.y - bevelDepth);
            pushQuad(vertices,
                     pixelToNDC(vFrontA, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(vFrontB, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(vFrontC, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(vFrontD, layout.screenWidth, layout.screenHeight),
                     rulerFront);
            pushQuad(vertices,
                     pixelToNDC(vTopA, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(vTopB, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(vTopC, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(vTopD, layout.screenWidth, layout.screenHeight),
                     rulerTop);
            pushQuad(vertices,
                     pixelToNDC(vLeftA, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(vLeftB, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(vLeftC, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(vLeftD, layout.screenWidth, layout.screenHeight),
                     rulerSide);
        }

        if ((daw.timelineSelectionActive || daw.timelineSelectionDragActive)
            && laneCount > 0
            && laneRight > laneLeft) {
            uint64_t selStart = std::min(daw.timelineSelectionStartSample, daw.timelineSelectionEndSample);
            uint64_t selEnd = std::max(daw.timelineSelectionStartSample, daw.timelineSelectionEndSample);
            int laneMin = std::min(daw.timelineSelectionStartLane, daw.timelineSelectionEndLane);
            int laneMax = std::max(daw.timelineSelectionStartLane, daw.timelineSelectionEndLane);
            laneMin = std::clamp(laneMin, 0, laneCount - 1);
            laneMax = std::clamp(laneMax, 0, laneCount - 1);
            if (selEnd > selStart && laneMin <= laneMax) {
                double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                if (windowSamples <= 0.0) windowSamples = 1.0;
                if (static_cast<double>(selEnd) > offsetSamples
                    && static_cast<double>(selStart) < offsetSamples + windowSamples) {
                    float t0 = static_cast<float>((static_cast<double>(selStart) - offsetSamples) / windowSamples);
                    float t1 = static_cast<float>((static_cast<double>(selEnd) - offsetSamples) / windowSamples);
                    float x0 = laneLeft + (laneRight - laneLeft) * t0;
                    float x1 = laneLeft + (laneRight - laneLeft) * t1;
                    x0 = std::clamp(x0, laneLeft, laneRight);
                    x1 = std::clamp(x1, laneLeft, laneRight);
                    if (x1 < x0) std::swap(x0, x1);
                    float topMin = 0.0f;
                    float bottomMin = 0.0f;
                    float lipBottomMin = 0.0f;
                    float topMax = 0.0f;
                    float bottomMax = 0.0f;
                    float lipBottomMax = 0.0f;
                    computeClipRect(startY + static_cast<float>(laneMin) * rowSpan, laneHalfH, topMin, bottomMin, lipBottomMin);
                    computeClipRect(startY + static_cast<float>(laneMax) * rowSpan, laneHalfH, topMax, bottomMax, lipBottomMax);
                    float y0 = lipBottomMin;
                    float y1 = bottomMax;
                    glm::vec3 fillColor(0.18f, 0.3f, 0.45f);
                    glm::vec3 edgeColor(0.34f, 0.56f, 0.82f);
                    auto itSel = world.colorLibrary.find("MiraLaneSelected");
                    if (itSel != world.colorLibrary.end()) {
                        edgeColor = itSel->second;
                        fillColor = glm::clamp(edgeColor * 0.35f, glm::vec3(0.0f), glm::vec3(1.0f));
                    }
                    pushQuad(vertices,
                             pixelToNDC({x0, y0}, layout.screenWidth, layout.screenHeight),
                             pixelToNDC({x1, y0}, layout.screenWidth, layout.screenHeight),
                             pixelToNDC({x1, y1}, layout.screenWidth, layout.screenHeight),
                             pixelToNDC({x0, y1}, layout.screenWidth, layout.screenHeight),
                             fillColor);
                    float edge = 1.0f;
                    pushQuad(vertices,
                             pixelToNDC({x0 - edge, y0}, layout.screenWidth, layout.screenHeight),
                             pixelToNDC({x0 + edge, y0}, layout.screenWidth, layout.screenHeight),
                             pixelToNDC({x0 + edge, y1}, layout.screenWidth, layout.screenHeight),
                             pixelToNDC({x0 - edge, y1}, layout.screenWidth, layout.screenHeight),
                             edgeColor);
                    pushQuad(vertices,
                             pixelToNDC({x1 - edge, y0}, layout.screenWidth, layout.screenHeight),
                             pixelToNDC({x1 + edge, y0}, layout.screenWidth, layout.screenHeight),
                             pixelToNDC({x1 + edge, y1}, layout.screenWidth, layout.screenHeight),
                             pixelToNDC({x1 - edge, y1}, layout.screenWidth, layout.screenHeight),
                             edgeColor);
                }
            }
        }

        {
            float rowHeight = takeRowHeight(laneHalfH);
            double offsetSamples3 = static_cast<double>(daw.timelineOffsetSamples);
            double windowSamples3 = secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples3 <= 0.0) windowSamples3 = 1.0;
            glm::vec3 rowBg(0.07f, 0.07f, 0.07f);
            glm::vec3 rowClip(0.17f, 0.17f, 0.17f);
            glm::vec3 rowComp(0.30f, 0.43f, 0.62f);
            glm::vec3 rowPreview(0.37f, 0.58f, 0.85f);
            auto itSel = world.colorLibrary.find("MiraLaneSelected");
            if (itSel != world.colorLibrary.end()) {
                rowComp = glm::clamp(itSel->second * 0.75f, glm::vec3(0.0f), glm::vec3(1.0f));
                rowPreview = glm::clamp(itSel->second + glm::vec3(0.04f), glm::vec3(0.0f), glm::vec3(1.0f));
            }
            for (int t = 0; t < audioTrackCount; ++t) {
                if (t < 0 || t >= static_cast<int>(daw.tracks.size())) continue;
                const DawTrack& track = daw.tracks[static_cast<size_t>(t)];
                if (!track.takeStackExpanded || track.loopTakeClips.empty()) continue;
                int laneIndex = (t >= 0 && t < static_cast<int>(audioLaneIndex.size()))
                    ? audioLaneIndex[static_cast<size_t>(t)]
                    : -1;
                if (laneIndex < 0) continue;
                int displayIndex = computeDisplayIndex(laneIndex);
                if (displayIndex < 0) continue;
                float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                float top = 0.0f;
                float bottom = 0.0f;
                float lipBottom = 0.0f;
                computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);
                (void)top;
                (void)lipBottom;
                float rowStartY = bottom + kTakeRowGap;
                for (size_t i = 0; i < track.loopTakeClips.size(); ++i) {
                    const DawClip& take = track.loopTakeClips[i];
                    if (take.length == 0) continue;
                    float y0 = rowStartY + static_cast<float>(i) * (rowHeight + kTakeRowSpacing);
                    float y1 = y0 + rowHeight;
                    if (y1 < topBound || y0 > visualBottomBound) continue;
                    pushQuad(vertices,
                             pixelToNDC({laneLeft, y0}, layout.screenWidth, layout.screenHeight),
                             pixelToNDC({laneRight, y0}, layout.screenWidth, layout.screenHeight),
                             pixelToNDC({laneRight, y1}, layout.screenWidth, layout.screenHeight),
                             pixelToNDC({laneLeft, y1}, layout.screenWidth, layout.screenHeight),
                             rowBg);
                    double takeStart = static_cast<double>(take.startSample);
                    double takeEnd = static_cast<double>(take.startSample + take.length);
                    if (takeEnd > offsetSamples3 && takeStart < offsetSamples3 + windowSamples3) {
                        double visStart = std::max(takeStart, offsetSamples3);
                        double visEnd = std::min(takeEnd, offsetSamples3 + windowSamples3);
                        float t0 = static_cast<float>((visStart - offsetSamples3) / windowSamples3);
                        float t1 = static_cast<float>((visEnd - offsetSamples3) / windowSamples3);
                        float x0 = laneLeft + (laneRight - laneLeft) * t0;
                        float x1 = laneLeft + (laneRight - laneLeft) * t1;
                        x0 = std::clamp(x0, laneLeft, laneRight);
                        x1 = std::clamp(x1, laneLeft, laneRight);
                        if (x1 > x0) {
                            pushQuad(vertices,
                                     pixelToNDC({x0, y0 + 1.0f}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({x1, y0 + 1.0f}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({x1, y1 - 1.0f}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({x0, y1 - 1.0f}, layout.screenWidth, layout.screenHeight),
                                     rowClip);
                        }
                    }
                    if (take.takeId >= 0) {
                        for (const auto& clip : track.clips) {
                            if (clip.length == 0) continue;
                            if (clip.takeId != take.takeId) continue;
                            uint64_t overlapStart = std::max(clip.startSample, take.startSample);
                            uint64_t overlapEnd = std::min(clip.startSample + clip.length, take.startSample + take.length);
                            if (overlapEnd <= overlapStart) continue;
                            double visStart = std::max<double>(static_cast<double>(overlapStart), offsetSamples3);
                            double visEnd = std::min<double>(static_cast<double>(overlapEnd), offsetSamples3 + windowSamples3);
                            if (visEnd <= visStart) continue;
                            float t0 = static_cast<float>((visStart - offsetSamples3) / windowSamples3);
                            float t1 = static_cast<float>((visEnd - offsetSamples3) / windowSamples3);
                            float x0 = laneLeft + (laneRight - laneLeft) * t0;
                            float x1 = laneLeft + (laneRight - laneLeft) * t1;
                            x0 = std::clamp(x0, laneLeft, laneRight);
                            x1 = std::clamp(x1, laneLeft, laneRight);
                            if (x1 <= x0) continue;
                            pushQuad(vertices,
                                     pixelToNDC({x0, y0 + 2.0f}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({x1, y0 + 2.0f}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({x1, y1 - 2.0f}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({x0, y1 - 2.0f}, layout.screenWidth, layout.screenHeight),
                                     rowComp);
                        }
                    }
                    if (daw.takeCompDragActive
                        && daw.takeCompLaneType == 0
                        && daw.takeCompTrack == t
                        && daw.takeCompTakeIndex == static_cast<int>(i)) {
                        uint64_t selStart = std::min(daw.takeCompStartSample, daw.takeCompEndSample);
                        uint64_t selEnd = std::max(daw.takeCompStartSample, daw.takeCompEndSample);
                        double visStart = std::max<double>(static_cast<double>(selStart), offsetSamples3);
                        double visEnd = std::min<double>(static_cast<double>(selEnd), offsetSamples3 + windowSamples3);
                        if (visEnd > visStart) {
                            float t0 = static_cast<float>((visStart - offsetSamples3) / windowSamples3);
                            float t1 = static_cast<float>((visEnd - offsetSamples3) / windowSamples3);
                            float x0 = laneLeft + (laneRight - laneLeft) * t0;
                            float x1 = laneLeft + (laneRight - laneLeft) * t1;
                            x0 = std::clamp(x0, laneLeft, laneRight);
                            x1 = std::clamp(x1, laneLeft, laneRight);
                            if (x1 > x0) {
                                pushQuad(vertices,
                                         pixelToNDC({x0, y0}, layout.screenWidth, layout.screenHeight),
                                         pixelToNDC({x1, y0}, layout.screenWidth, layout.screenHeight),
                                         pixelToNDC({x1, y1}, layout.screenWidth, layout.screenHeight),
                                         pixelToNDC({x0, y1}, layout.screenWidth, layout.screenHeight),
                                         rowPreview);
                            }
                        }
                    }
                }
            }
        }

        if (daw.clipDragActive && daw.clipDragTrack >= 0 && daw.clipDragIndex >= 0
            && daw.clipDragTrack < audioTrackCount) {
            DawTrack& track = daw.tracks[static_cast<size_t>(daw.clipDragTrack)];
            if (daw.clipDragIndex < static_cast<int>(track.clips.size())) {
                const DawClip& clip = track.clips[static_cast<size_t>(daw.clipDragIndex)];
                int targetTrack = daw.clipDragTargetTrack;
                if (targetTrack >= 0 && targetTrack < audioTrackCount) {
                    int laneIndex = audioLaneIndex[static_cast<size_t>(targetTrack)];
                    if (laneIndex >= 0) {
                        int displayIndex = computeDisplayIndex(laneIndex);
                        if (displayIndex >= 0) {
                            float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                            float top = 0.0f;
                            float bottom = 0.0f;
                            float lipBottom = 0.0f;
                            computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);
                            double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                            double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                            if (windowSamples <= 0.0) windowSamples = 1.0;
                            double clipStart = static_cast<double>(daw.clipDragTargetStart);
                            double clipEnd = static_cast<double>(daw.clipDragTargetStart + clip.length);
                            if (clipEnd > offsetSamples && clipStart < offsetSamples + windowSamples) {
                                double visibleStart = std::max(clipStart, offsetSamples);
                                double visibleEnd = std::min(clipEnd, offsetSamples + windowSamples);
                                float t0 = static_cast<float>((visibleStart - offsetSamples) / windowSamples);
                                float t1 = static_cast<float>((visibleEnd - offsetSamples) / windowSamples);
                                float x0 = laneLeft + (laneRight - laneLeft) * t0 - kClipHorizontalPad;
                                float x1 = laneLeft + (laneRight - laneLeft) * t1 + kClipHorizontalPad;
                                x0 = std::max(x0, laneLeft);
                                x1 = std::min(x1, laneRight);
                                glm::vec3 ghostColor = glm::clamp(playheadColor + glm::vec3(0.12f), glm::vec3(0.0f), glm::vec3(1.0f));
                                glm::vec3 ghostLip = glm::clamp(ghostColor - glm::vec3(0.09f), glm::vec3(0.0f), glm::vec3(1.0f));
                                glm::vec2 a(x0, top);
                                glm::vec2 b(x1, top);
                                glm::vec2 c(x1, bottom);
                                glm::vec2 d(x0, bottom);
                                pushQuad(vertices,
                                         pixelToNDC(a, layout.screenWidth, layout.screenHeight),
                                         pixelToNDC(b, layout.screenWidth, layout.screenHeight),
                                         pixelToNDC(c, layout.screenWidth, layout.screenHeight),
                                         pixelToNDC(d, layout.screenWidth, layout.screenHeight),
                                         ghostColor);
                                if (lipBottom > top + 0.5f) {
                                    glm::vec2 la(x0, top);
                                    glm::vec2 lb(x1, top);
                                    glm::vec2 lc(x1, lipBottom);
                                    glm::vec2 ld(x0, lipBottom);
                                    pushQuad(vertices,
                                             pixelToNDC(la, layout.screenWidth, layout.screenHeight),
                                             pixelToNDC(lb, layout.screenWidth, layout.screenHeight),
                                             pixelToNDC(lc, layout.screenWidth, layout.screenHeight),
                                             pixelToNDC(ld, layout.screenWidth, layout.screenHeight),
                                             ghostLip);
                                }
                            }
                        }
                    }
                }
            }
        }
        if (daw.clipTrimActive && daw.clipTrimTrack >= 0 && daw.clipTrimIndex >= 0
            && daw.clipTrimTrack < audioTrackCount
            && daw.clipTrimIndex < static_cast<int>(daw.tracks[static_cast<size_t>(daw.clipTrimTrack)].clips.size())) {
            const auto audioLaneIndex = DawLaneTimelineSystemLogic::BuildAudioLaneIndex(daw, audioTrackCount);
            int laneIndex = audioLaneIndex[static_cast<size_t>(daw.clipTrimTrack)];
            if (laneIndex >= 0) {
                int displayIndex = computeDisplayIndex(laneIndex);
                if (displayIndex >= 0) {
                    float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                    float top = 0.0f;
                    float bottom = 0.0f;
                    float lipBottom = 0.0f;
                    computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);
                    double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                    double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                    if (windowSamples <= 0.0) windowSamples = 1.0;
                    double clipStart = static_cast<double>(daw.clipTrimTargetStart);
                    double clipEnd = static_cast<double>(daw.clipTrimTargetStart + daw.clipTrimTargetLength);
                    if (clipEnd > offsetSamples && clipStart < offsetSamples + windowSamples) {
                        double visibleStart = std::max(clipStart, offsetSamples);
                        double visibleEnd = std::min(clipEnd, offsetSamples + windowSamples);
                        float t0 = static_cast<float>((visibleStart - offsetSamples) / windowSamples);
                        float t1 = static_cast<float>((visibleEnd - offsetSamples) / windowSamples);
                        float x0 = laneLeft + (laneRight - laneLeft) * t0 - kClipHorizontalPad;
                        float x1 = laneLeft + (laneRight - laneLeft) * t1 + kClipHorizontalPad;
                        x0 = std::max(x0, laneLeft);
                        x1 = std::min(x1, laneRight);
                        glm::vec3 ghostColor = glm::clamp(playheadColor + glm::vec3(0.09f), glm::vec3(0.0f), glm::vec3(1.0f));
                        glm::vec3 ghostLip = glm::clamp(ghostColor - glm::vec3(0.09f), glm::vec3(0.0f), glm::vec3(1.0f));
                        pushQuad(vertices,
                                 pixelToNDC({x0, top}, layout.screenWidth, layout.screenHeight),
                                 pixelToNDC({x1, top}, layout.screenWidth, layout.screenHeight),
                                 pixelToNDC({x1, bottom}, layout.screenWidth, layout.screenHeight),
                                 pixelToNDC({x0, bottom}, layout.screenWidth, layout.screenHeight),
                                 ghostColor);
                        if (lipBottom > top + 0.5f) {
                            pushQuad(vertices,
                                     pixelToNDC({x0, top}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({x1, top}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({x1, lipBottom}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({x0, lipBottom}, layout.screenWidth, layout.screenHeight),
                                     ghostLip);
                        }
                    }
                }
            }
        }

        {
            double offsetSec2 = (daw.sampleRate > 0.0)
                ? static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate)
                : 0.0;
            double loopStartSec = (daw.sampleRate > 0.0)
                ? static_cast<double>(daw.loopStartSamples) / static_cast<double>(daw.sampleRate)
                : 0.0;
            double loopEndSec = (daw.sampleRate > 0.0)
                ? static_cast<double>(daw.loopEndSamples) / static_cast<double>(daw.sampleRate)
                : 0.0;
            float loopStartX = static_cast<float>(laneLeft + (laneRight - laneLeft) * ((loopStartSec - offsetSec2) / secondsPerScreen));
            float loopEndX = static_cast<float>(laneLeft + (laneRight - laneLeft) * ((loopEndSec - offsetSec2) / secondsPerScreen));
            if (loopEndX < loopStartX) std::swap(loopStartX, loopEndX);
            float lineHalf = 0.5f;
            glm::vec3 loopColor = playheadColor;
            if (daw.loopEnabled.load(std::memory_order_relaxed)) {
                glm::vec2 la(loopStartX - lineHalf, topBound);
                glm::vec2 lb(loopStartX + lineHalf, topBound);
                glm::vec2 lc(loopStartX + lineHalf, visualBottomBound);
                glm::vec2 ld(loopStartX - lineHalf, visualBottomBound);
                pushQuad(vertices,
                         pixelToNDC(la, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(lb, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(lc, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(ld, layout.screenWidth, layout.screenHeight),
                         loopColor);
                glm::vec2 ra(loopEndX - lineHalf, topBound);
                glm::vec2 rb(loopEndX + lineHalf, topBound);
                glm::vec2 rc(loopEndX + lineHalf, visualBottomBound);
                glm::vec2 rd(loopEndX - lineHalf, visualBottomBound);
                pushQuad(vertices,
                         pixelToNDC(ra, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(rb, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(rc, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(rd, layout.screenWidth, layout.screenHeight),
                         loopColor);
            }
        }
        if (showPlayhead) {
            pushQuad(vertices,
                     pixelToNDC(pa, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(pb, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(pc, layout.screenWidth, layout.screenHeight),
                     pixelToNDC(pd, layout.screenWidth, layout.screenHeight),
                     playheadColor);
            glm::vec2 tA(playheadX, handleY + handleHalf);
            glm::vec2 tB(playheadX - handleHalf, handleY - handleHalf);
            glm::vec2 tC(playheadX + handleHalf, handleY - handleHalf);
            vertices.push_back({pixelToNDC(tA, layout.screenWidth, layout.screenHeight), playheadColor});
            vertices.push_back({pixelToNDC(tB, layout.screenWidth, layout.screenHeight), playheadColor});
            vertices.push_back({pixelToNDC(tC, layout.screenWidth, layout.screenHeight), playheadColor});
        }

        glm::vec3 selectedColor(0.45f, 0.72f, 1.0f);
        auto itSelected = world.colorLibrary.find("MiraLaneSelected");
        if (itSelected != world.colorLibrary.end()) {
            selectedColor = itSelected->second;
        }
        if (daw.selectedLaneType == 0 && daw.selectedLaneIndex >= 0 && daw.selectedLaneIndex < laneCount) {
            int displayIndex = computeDisplayIndex(daw.selectedLaneIndex);
            if (displayIndex >= 0) {
                float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                float handleSize = std::min(kTrackHandleSize, std::max(14.0f, layout.laneHeight));
                float handleHalf = handleSize * 0.5f;
                float centerX = laneRight + kTrackHandleInset + handleHalf;
                float minCenterX = laneLeft + 4.0f + handleHalf;
                if (centerX < minCenterX) centerX = minCenterX;
                float top = centerY - handleHalf;
                float bottom = centerY + handleHalf;
                glm::vec3 selectedHighlight = glm::clamp(selectedColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
                glm::vec3 selectedShadow = glm::clamp(selectedColor - glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f));
                float bevelDepth = std::min(6.0f, handleHalf * 0.5f);
                glm::vec2 frontA(centerX - handleHalf, top);
                glm::vec2 frontB(centerX + handleHalf, top);
                glm::vec2 frontC(centerX + handleHalf, bottom);
                glm::vec2 frontD(centerX - handleHalf, bottom);
                glm::vec2 topA = frontA;
                glm::vec2 topB = frontB;
                glm::vec2 topC(frontB.x - bevelDepth, frontB.y - bevelDepth);
                glm::vec2 topD(frontA.x - bevelDepth, frontA.y - bevelDepth);
                glm::vec2 leftA = frontA;
                glm::vec2 leftB = frontD;
                glm::vec2 leftC(frontD.x - bevelDepth, frontD.y - bevelDepth);
                glm::vec2 leftD(frontA.x - bevelDepth, frontA.y - bevelDepth);
                pushQuad(vertices,
                         pixelToNDC(frontA, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(frontB, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(frontC, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(frontD, layout.screenWidth, layout.screenHeight),
                         selectedColor);
                pushQuad(vertices,
                         pixelToNDC(topA, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(topB, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(topC, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(topD, layout.screenWidth, layout.screenHeight),
                         selectedHighlight);
                pushQuad(vertices,
                         pixelToNDC(leftA, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(leftB, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(leftC, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(leftD, layout.screenWidth, layout.screenHeight),
                         selectedShadow);
            }
        }

        int insertSlot = daw.dragActive ? daw.dragDropIndex
            : (daw.externalDropActive && daw.externalDropType == 0 ? daw.externalDropIndex : -1);
        if ((daw.dragActive && daw.dragLaneType == 0) || (daw.externalDropActive && daw.externalDropType == 0)) {
            float ghostCenterY = daw.dragActive
                ? static_cast<float>(ui.cursorY)
                : (startY + static_cast<float>(daw.externalDropIndex) * rowSpan);
            float handleSize = std::min(kTrackHandleSize, std::max(14.0f, layout.laneHeight));
            float handleHalf = handleSize * 0.5f;
            float centerX = laneRight + kTrackHandleInset + handleHalf;
            float minCenterX = laneLeft + 4.0f + handleHalf;
            if (centerX < minCenterX) centerX = minCenterX;
            float ghostTop = ghostCenterY - handleHalf;
            float ghostBottom = ghostCenterY + handleHalf;
            glm::vec3 ghostColor = glm::clamp(selectedColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
            float bevelDepth = std::min(6.0f, handleHalf * 0.5f);
            pushQuad(vertices,
                     pixelToNDC({centerX - handleHalf, ghostTop}, layout.screenWidth, layout.screenHeight),
                     pixelToNDC({centerX + handleHalf, ghostTop}, layout.screenWidth, layout.screenHeight),
                     pixelToNDC({centerX + handleHalf, ghostBottom}, layout.screenWidth, layout.screenHeight),
                     pixelToNDC({centerX - handleHalf, ghostBottom}, layout.screenWidth, layout.screenHeight),
                     ghostColor);
            pushQuad(vertices,
                     pixelToNDC({centerX - handleHalf, ghostTop}, layout.screenWidth, layout.screenHeight),
                     pixelToNDC({centerX + handleHalf, ghostTop}, layout.screenWidth, layout.screenHeight),
                     pixelToNDC({centerX + handleHalf - bevelDepth, ghostTop - bevelDepth}, layout.screenWidth, layout.screenHeight),
                     pixelToNDC({centerX - handleHalf - bevelDepth, ghostTop - bevelDepth}, layout.screenWidth, layout.screenHeight),
                     glm::clamp(ghostColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f)));
            pushQuad(vertices,
                     pixelToNDC({centerX - handleHalf, ghostTop}, layout.screenWidth, layout.screenHeight),
                     pixelToNDC({centerX - handleHalf, ghostBottom}, layout.screenWidth, layout.screenHeight),
                     pixelToNDC({centerX - handleHalf - bevelDepth, ghostBottom - bevelDepth}, layout.screenWidth, layout.screenHeight),
                     pixelToNDC({centerX - handleHalf - bevelDepth, ghostTop - bevelDepth}, layout.screenWidth, layout.screenHeight),
                     glm::clamp(ghostColor - glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f)));
            if (insertSlot >= 0) {
                float insertY = startY + (static_cast<float>(insertSlot) - 0.5f) * rowSpan;
                float lineHalf = 2.0f;
                pushQuad(vertices,
                         pixelToNDC({laneLeft, insertY - lineHalf}, layout.screenWidth, layout.screenHeight),
                         pixelToNDC({laneRight, insertY - lineHalf}, layout.screenWidth, layout.screenHeight),
                         pixelToNDC({laneRight, insertY + lineHalf}, layout.screenWidth, layout.screenHeight),
                         pixelToNDC({laneLeft, insertY + lineHalf}, layout.screenWidth, layout.screenHeight),
                         selectedColor);
            }
        }

        if (daw.automationParamMenuOpen) {
            const AutomationParamMenuLayout menuLayout = computeAutomationParamMenuLayout(daw, layout);
            if (menuLayout.valid) {
                glm::vec3 panelFront(0.20f, 0.23f, 0.24f);
                glm::vec3 panelTop(0.30f, 0.34f, 0.35f);
                glm::vec3 panelSide(0.12f, 0.13f, 0.14f);
                glm::vec3 textColor(0.88f, 0.92f, 0.93f);
                auto itFront = world.colorLibrary.find("ButtonFront");
                if (itFront != world.colorLibrary.end()) panelFront = itFront->second;
                auto itTop = world.colorLibrary.find("ButtonTopHighlight");
                if (itTop != world.colorLibrary.end()) panelTop = itTop->second;
                auto itSide = world.colorLibrary.find("ButtonSideShadow");
                if (itSide != world.colorLibrary.end()) panelSide = itSide->second;
                auto itText2 = world.colorLibrary.find("MiraText");
                if (itText2 != world.colorLibrary.end()) textColor = itText2->second;

                pushBeveledRect(vertices,
                                menuLayout.menuRect,
                                4.0f,
                                panelFront,
                                panelTop,
                                panelSide,
                                layout.screenWidth,
                                layout.screenHeight);

                int selectedSlot = -1;
                if (daw.automationParamMenuTrack >= 0
                    && daw.automationParamMenuTrack < static_cast<int>(daw.automationTracks.size())) {
                    selectedSlot = daw.automationTracks[static_cast<size_t>(daw.automationParamMenuTrack)].targetParameterSlot;
                }

                for (size_t i = 0; i < daw.automationParamMenuLabels.size(); ++i) {
                    float y = menuLayout.menuRect.top + menuLayout.padding + static_cast<float>(i) * menuLayout.rowHeight;
                    Rect rowRect = makeRect(menuLayout.menuRect.left + 2.0f,
                                            y,
                                            (menuLayout.menuRect.right - menuLayout.menuRect.left) - 4.0f,
                                            menuLayout.rowHeight);
                    if (static_cast<int>(i) == selectedSlot) {
                        pushRect(vertices, rowRect, glm::vec3(0.20f, 0.42f, 0.68f), layout.screenWidth, layout.screenHeight);
                    } else if (daw.automationParamMenuHoverIndex == static_cast<int>(i)) {
                        pushRect(vertices, rowRect, glm::vec3(0.28f, 0.33f, 0.37f), layout.screenWidth, layout.screenHeight);
                    }
                    pushText(vertices,
                             menuLayout.menuRect.left + 8.0f,
                             y + 13.0f,
                             daw.automationParamMenuLabels[i].c_str(),
                             textColor,
                             layout.screenWidth,
                             layout.screenHeight);
                }
            }
        }

        if (daw.settingsMenuOpen) {
            const SettingsDialogLayout settingsLayout = computeSettingsDialogLayout(layout);
            glm::vec3 overlayColor(0.02f, 0.03f, 0.04f);
            const Rect screenRect = makeRect(0.0f, 0.0f, static_cast<float>(layout.screenWidth), static_cast<float>(layout.screenHeight));
            pushRect(vertices, screenRect, overlayColor, layout.screenWidth, layout.screenHeight);

            glm::vec3 panelFront(0.20f, 0.23f, 0.24f);
            glm::vec3 panelTop(0.30f, 0.34f, 0.35f);
            glm::vec3 panelSide(0.12f, 0.13f, 0.14f);
            glm::vec3 textColor(0.88f, 0.92f, 0.93f);
            if (baseSystem.daw) {
                panelFront = glm::vec3(baseSystem.daw->activeThemePanel);
                panelTop = glm::clamp(panelFront + glm::vec3(0.10f), glm::vec3(0.0f), glm::vec3(1.0f));
                panelSide = glm::clamp(panelFront - glm::vec3(0.03f), glm::vec3(0.0f), glm::vec3(1.0f));
            }
            auto itText2 = world.colorLibrary.find("MiraText");
            if (itText2 != world.colorLibrary.end()) textColor = itText2->second;
            glm::vec3 buttonFront = glm::clamp(panelFront + glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f));
            if (baseSystem.daw) {
                buttonFront = glm::vec3(baseSystem.daw->activeThemeButton);
            }
            const glm::vec3 buttonTop = glm::clamp(buttonFront + glm::vec3(0.10f), glm::vec3(0.0f), glm::vec3(1.0f));
            const glm::vec3 buttonSide = glm::clamp(buttonFront - glm::vec3(0.03f), glm::vec3(0.0f), glm::vec3(1.0f));

            const Rect panelRect = makeRect(settingsLayout.x, settingsLayout.y, settingsLayout.w, settingsLayout.h);
            pushBeveledRect(vertices,
                            panelRect,
                            8.0f,
                            panelFront,
                            panelTop,
                            panelSide,
                            layout.screenWidth,
                            layout.screenHeight);

            pushRect(vertices, settingsLayout.closeBtn, glm::vec3(0.72f, 0.26f, 0.24f), layout.screenWidth, layout.screenHeight);
            pushText(vertices,
                     settingsLayout.closeBtn.left + 7.0f,
                     settingsLayout.closeBtn.top + 8.0f,
                     "X",
                     glm::vec3(0.94f, 0.94f, 0.94f),
                     layout.screenWidth,
                     layout.screenHeight);

            auto drawButton = [&](const Rect& rect, const char* label, bool active, bool disabled) {
                glm::vec3 front = active ? glm::vec3(0.28f, 0.45f, 0.68f) : buttonFront;
                glm::vec3 top = active ? glm::vec3(0.36f, 0.56f, 0.80f) : buttonTop;
                glm::vec3 side = active ? glm::vec3(0.17f, 0.30f, 0.45f) : buttonSide;
                glm::vec3 labelColor(0.06f, 0.08f, 0.08f);
                if (disabled) {
                    front = glm::clamp(front - glm::vec3(0.07f), glm::vec3(0.0f), glm::vec3(1.0f));
                    top = glm::clamp(top - glm::vec3(0.07f), glm::vec3(0.0f), glm::vec3(1.0f));
                    side = glm::clamp(side - glm::vec3(0.07f), glm::vec3(0.0f), glm::vec3(1.0f));
                    labelColor = glm::vec3(0.20f, 0.22f, 0.22f);
                }
                pushBeveledRect(vertices,
                                rect,
                                4.0f,
                                front,
                                top,
                                side,
                                layout.screenWidth,
                                layout.screenHeight);
                pushText(vertices,
                         rect.left + 8.0f,
                         rect.top + 15.0f,
                         label,
                         labelColor,
                         layout.screenWidth,
                         layout.screenHeight);
            };

            pushText(vertices,
                     settingsLayout.x + 18.0f,
                     settingsLayout.y + 24.0f,
                     "Settings",
                     textColor,
                     layout.screenWidth,
                     layout.screenHeight);
            drawButton(settingsLayout.tabTheme, "Theme", true, false);

            if (!daw.themeCreateMode) {
                pushRect(vertices,
                         settingsLayout.listRect,
                         glm::clamp(panelFront - glm::vec3(0.03f), glm::vec3(0.0f), glm::vec3(1.0f)),
                         layout.screenWidth,
                         layout.screenHeight);
                const int maxRows = std::max(0, static_cast<int>((settingsLayout.listRect.bottom - settingsLayout.listRect.top
                    - settingsLayout.listPad * 2.0f) / settingsLayout.listRowHeight));
                for (int i = 0; i < static_cast<int>(daw.themes.size()) && i < maxRows; ++i) {
                    Rect row = settingsThemeRowRect(settingsLayout, i);
                    bool selected = (i == daw.settingsSelectedTheme);
                    glm::vec3 rowColor = selected
                        ? glm::vec3(0.28f, 0.45f, 0.68f)
                        : glm::clamp(panelFront + glm::vec3(0.02f), glm::vec3(0.0f), glm::vec3(1.0f));
                    pushRect(vertices, row, rowColor, layout.screenWidth, layout.screenHeight);
                    pushText(vertices,
                             row.left + 8.0f,
                             row.top + 15.0f,
                             daw.themes[static_cast<size_t>(i)].name.c_str(),
                             textColor,
                             layout.screenWidth,
                             layout.screenHeight);
                }
                if (static_cast<int>(daw.themes.size()) > maxRows) {
                    pushText(vertices,
                             settingsLayout.listRect.left + 8.0f,
                             settingsLayout.listRect.bottom + 14.0f,
                             "...",
                             textColor,
                             layout.screenWidth,
                             layout.screenHeight);
                }
                pushText(vertices,
                         settingsLayout.x + 314.0f,
                         settingsLayout.y + 104.0f,
                         "Create custom themes with RGBA hex",
                         textColor,
                         layout.screenWidth,
                         layout.screenHeight);
                pushText(vertices,
                         settingsLayout.x + 314.0f,
                         settingsLayout.y + 124.0f,
                         "codes like FFFFFFCC.",
                         textColor,
                         layout.screenWidth,
                         layout.screenHeight);
                const DawThemePreset* selectedTheme = nullptr;
                if (daw.settingsSelectedTheme >= 0 && daw.settingsSelectedTheme < static_cast<int>(daw.themes.size())) {
                    selectedTheme = &daw.themes[static_cast<size_t>(daw.settingsSelectedTheme)];
                }
                const bool canEdit = selectedTheme && selectedTheme->name != "Default";
                const bool canDelete = selectedTheme
                    && selectedTheme->name != "Default"
                    && selectedTheme->name != "Default 2"
                    && selectedTheme->name != "Default 3";
                drawButton(settingsLayout.applyBtn, "Apply", false, false);
                drawButton(settingsLayout.createBtn, "Create", false, false);
                drawButton(settingsLayout.editBtn, "Edit", false, !canEdit);
                drawButton(settingsLayout.deleteBtn, "Delete", false, !canDelete);
            } else {
                auto drawField = [&](const Rect& rect, const char* label, const std::string& value, bool active) {
                    pushText(vertices,
                             rect.left,
                             rect.top - 6.0f,
                             label,
                             textColor,
                             layout.screenWidth,
                             layout.screenHeight);
                    glm::vec3 front = active
                        ? glm::vec3(0.28f, 0.45f, 0.68f)
                        : glm::clamp(panelFront + glm::vec3(0.02f), glm::vec3(0.0f), glm::vec3(1.0f));
                    glm::vec3 top = active
                        ? glm::vec3(0.36f, 0.56f, 0.80f)
                        : glm::clamp(panelTop + glm::vec3(0.02f), glm::vec3(0.0f), glm::vec3(1.0f));
                    glm::vec3 side = active
                        ? glm::vec3(0.17f, 0.30f, 0.45f)
                        : glm::clamp(panelSide - glm::vec3(0.01f), glm::vec3(0.0f), glm::vec3(1.0f));
                    pushBeveledRect(vertices,
                                    rect,
                                    3.0f,
                                    front,
                                    top,
                                    side,
                                    layout.screenWidth,
                                    layout.screenHeight);
                    pushText(vertices,
                             rect.left + 8.0f,
                             rect.top + 18.0f,
                             value.c_str(),
                             textColor,
                             layout.screenWidth,
                             layout.screenHeight);
                };

                drawField(settingsLayout.nameField,
                          "Theme Name",
                          daw.themeDraftName.empty() ? std::string("<name>") : daw.themeDraftName,
                          daw.themeEditField == 0);
                drawField(settingsLayout.bgField,
                          "Background RGBA",
                          daw.themeDraftBackgroundHex.empty() ? std::string("<RRGGBBAA>") : daw.themeDraftBackgroundHex,
                          daw.themeEditField == 1);
                drawField(settingsLayout.panelField,
                          "Panel RGBA",
                          daw.themeDraftPanelHex.empty() ? std::string("<RRGGBBAA>") : daw.themeDraftPanelHex,
                          daw.themeEditField == 2);
                drawField(settingsLayout.buttonField,
                          "Button RGBA",
                          daw.themeDraftButtonHex.empty() ? std::string("<RRGGBBAA>") : daw.themeDraftButtonHex,
                          daw.themeEditField == 3);
                drawField(settingsLayout.pianoField,
                          "Piano Roll RGBA",
                          daw.themeDraftPianoRollHex.empty() ? std::string("<RRGGBBAA>") : daw.themeDraftPianoRollHex,
                          daw.themeEditField == 4);
                drawField(settingsLayout.pianoAccentField,
                          "Piano Accent RGBA",
                          daw.themeDraftPianoRollAccentHex.empty() ? std::string("<RRGGBBAA>") : daw.themeDraftPianoRollAccentHex,
                          daw.themeEditField == 5);
                drawField(settingsLayout.laneField,
                          "Lane RGBA",
                          daw.themeDraftLaneHex.empty() ? std::string("<RRGGBBAA>") : daw.themeDraftLaneHex,
                          daw.themeEditField == 6);
                drawButton(settingsLayout.backBtn, "Back", false, false);
                drawButton(settingsLayout.saveBtn, "Save", false, false);
            }

            if (!daw.themeStatusMessage.empty()) {
                pushText(vertices,
                         settingsLayout.x + 18.0f,
                         settingsLayout.y + settingsLayout.h - 14.0f,
                         daw.themeStatusMessage.c_str(),
                         textColor,
                         layout.screenWidth,
                         layout.screenHeight);
            }
        }

        if (daw.exportMenuOpen || daw.exportInProgress.load(std::memory_order_relaxed)) {
            const ExportDialogLayout exportLayout = computeExportDialogLayout(layout);
            const Rect screenRect = makeRect(0.0f, 0.0f, static_cast<float>(layout.screenWidth), static_cast<float>(layout.screenHeight));
            pushRect(vertices, screenRect, glm::vec3(0.02f, 0.03f, 0.04f), layout.screenWidth, layout.screenHeight);

            glm::vec3 panelFront(0.20f, 0.23f, 0.24f);
            glm::vec3 panelTop(0.30f, 0.34f, 0.35f);
            glm::vec3 panelSide(0.12f, 0.13f, 0.14f);
            glm::vec3 textColor(0.88f, 0.92f, 0.93f);
            auto itFront = world.colorLibrary.find("ButtonFront");
            if (itFront != world.colorLibrary.end()) panelFront = itFront->second;
            auto itTop = world.colorLibrary.find("ButtonTopHighlight");
            if (itTop != world.colorLibrary.end()) panelTop = itTop->second;
            auto itSide = world.colorLibrary.find("ButtonSideShadow");
            if (itSide != world.colorLibrary.end()) panelSide = itSide->second;
            auto itText2 = world.colorLibrary.find("MiraText");
            if (itText2 != world.colorLibrary.end()) textColor = itText2->second;

            const Rect panelRect = makeRect(exportLayout.x, exportLayout.y, exportLayout.w, exportLayout.h);
            pushBeveledRect(vertices,
                            panelRect,
                            8.0f,
                            panelFront,
                            panelTop,
                            panelSide,
                            layout.screenWidth,
                            layout.screenHeight);

            pushRect(vertices, exportLayout.closeBtn, glm::vec3(0.72f, 0.26f, 0.24f), layout.screenWidth, layout.screenHeight);
            pushText(vertices,
                     exportLayout.closeBtn.left + 7.0f,
                     exportLayout.closeBtn.top + 8.0f,
                     "X",
                     glm::vec3(0.94f, 0.94f, 0.94f),
                     layout.screenWidth,
                     layout.screenHeight);

            auto drawButton = [&](const Rect& rect, const char* label) {
                pushBeveledRect(vertices,
                                rect,
                                4.0f,
                                glm::clamp(panelFront + glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f)),
                                glm::clamp(panelTop + glm::vec3(0.03f), glm::vec3(0.0f), glm::vec3(1.0f)),
                                glm::clamp(panelSide - glm::vec3(0.02f), glm::vec3(0.0f), glm::vec3(1.0f)),
                                layout.screenWidth,
                                layout.screenHeight);
                pushText(vertices,
                         rect.left + 7.0f,
                         rect.top + 15.0f,
                         label,
                         glm::vec3(0.06f, 0.08f, 0.08f),
                         layout.screenWidth,
                         layout.screenHeight);
            };

            drawButton(exportLayout.startMinus, "-");
            drawButton(exportLayout.startPlus, "+");
            drawButton(exportLayout.endMinus, "-");
            drawButton(exportLayout.endPlus, "+");
            drawButton(exportLayout.folderBtn, "Folder");
            drawButton(exportLayout.exportBtn, daw.exportInProgress.load(std::memory_order_relaxed) ? "Busy" : "Export");
            drawButton(exportLayout.cancelBtn, "Close");

            pushText(vertices,
                     exportLayout.x + 18.0f,
                     exportLayout.y + 24.0f,
                     "Export Stems",
                     textColor,
                     layout.screenWidth,
                     layout.screenHeight);

            char startLabel[64];
            char endLabel[64];
            std::snprintf(startLabel, sizeof(startLabel), "Start Bar: %d", daw.exportStartBar);
            std::snprintf(endLabel, sizeof(endLabel), "End Bar: %d", daw.exportEndBar);
            pushText(vertices, exportLayout.x + 24.0f, exportLayout.y + 72.0f, startLabel, textColor, layout.screenWidth, layout.screenHeight);
            pushText(vertices, exportLayout.x + 24.0f, exportLayout.y + 104.0f, endLabel, textColor, layout.screenWidth, layout.screenHeight);

            int64_t startSample = DawIOSystemLogic::BarDisplayToSample(daw, daw.exportStartBar);
            int64_t endSample = DawIOSystemLogic::BarDisplayToSample(daw, daw.exportEndBar);
            int64_t lengthSamples = std::max<int64_t>(0, endSample - startSample);
            double barSamples = DawIOSystemLogic::BarSamples(daw);
            const double sampleRate = (daw.sampleRate > 0.0f) ? static_cast<double>(daw.sampleRate) : 44100.0;
            double lenBars = (barSamples > 0.0) ? (static_cast<double>(lengthSamples) / barSamples) : 0.0;
            double lenSec = static_cast<double>(lengthSamples) / sampleRate;
            char lenLabel[96];
            std::snprintf(lenLabel, sizeof(lenLabel), "Length: %.2f bars (%.2f sec)", lenBars, lenSec);
            pushText(vertices, exportLayout.x + 24.0f, exportLayout.y + 136.0f, lenLabel, textColor, layout.screenWidth, layout.screenHeight);

            std::string folder = daw.exportFolderPath.empty() ? std::string("<not set>") : daw.exportFolderPath;
            if (folder.size() > 52) {
                folder = "..." + folder.substr(folder.size() - 49);
            }
            pushText(vertices, exportLayout.x + 24.0f, exportLayout.y + 152.0f, folder.c_str(), textColor, layout.screenWidth, layout.screenHeight);

            const std::array<const char*, DawContext::kBusCount> busLabels{"L", "S", "F", "R"};
            for (int i = 0; i < DawContext::kBusCount; ++i) {
                Rect row = exportLayout.stemRows[static_cast<size_t>(i)];
                glm::vec3 rowColor = glm::clamp(panelFront + glm::vec3(0.03f), glm::vec3(0.0f), glm::vec3(1.0f));
                if (daw.exportSelectedStem == i) {
                    rowColor = glm::vec3(0.28f, 0.45f, 0.68f);
                }
                pushRect(vertices, row, rowColor, layout.screenWidth, layout.screenHeight);
                char stemLabel[192];
                const std::string& name = daw.exportStemNames[static_cast<size_t>(i)];
                std::snprintf(stemLabel, sizeof(stemLabel), "%s Stem: %s.wav", busLabels[static_cast<size_t>(i)], name.c_str());
                pushText(vertices,
                         row.left + 8.0f,
                         row.top + 16.0f,
                         stemLabel,
                         textColor,
                         layout.screenWidth,
                         layout.screenHeight);
            }

            pushRect(vertices, exportLayout.progressBar, glm::vec3(0.12f, 0.14f, 0.15f), layout.screenWidth, layout.screenHeight);
            float progress = std::clamp(daw.exportProgress.load(std::memory_order_relaxed), 0.0f, 1.0f);
            Rect fill = exportLayout.progressBar;
            fill.right = fill.left + (fill.right - fill.left) * progress;
            if (fill.right > fill.left) {
                pushRect(vertices, fill, glm::vec3(0.30f, 0.58f, 0.86f), layout.screenWidth, layout.screenHeight);
            }

            std::string status = daw.exportStatusMessage;
            if (status.empty()) {
                status = daw.exportInProgress.load(std::memory_order_relaxed) ? "Exporting..." : "Ready";
            }
            pushText(vertices,
                     exportLayout.progressBar.left,
                     exportLayout.progressBar.bottom + 16.0f,
                     status.c_str(),
                     textColor,
                     layout.screenWidth,
                     layout.screenHeight);
        }

        DawLaneResourceSystemLogic::SetLaneTotalVertexCount(vertices.size());
        size_t totalCount = DawLaneResourceSystemLogic::GetLaneTotalVertexCount();
        if (totalCount == 0 || !renderer.uiColorShader) return;

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        float laneAlpha = kLaneAlphaDefault;
        if (baseSystem.daw) {
            laneAlpha = std::clamp(baseSystem.daw->activeThemeBackground.a, 0.0f, 1.0f);
        }
        glBlendColor(0.0f, 0.0f, 0.0f, laneAlpha);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        glBindVertexArray(renderer.uiLaneVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.uiLaneVBO);
        glBufferData(GL_ARRAY_BUFFER, totalCount * sizeof(DawLaneResourceSystemLogic::UiVertex), vertices.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(DawLaneResourceSystemLogic::UiVertex), (void*)offsetof(DawLaneResourceSystemLogic::UiVertex, pos));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(DawLaneResourceSystemLogic::UiVertex), (void*)offsetof(DawLaneResourceSystemLogic::UiVertex, color));
        glEnableVertexAttribArray(1);
        renderer.uiColorShader->use();
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(totalCount));
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
    }
}
