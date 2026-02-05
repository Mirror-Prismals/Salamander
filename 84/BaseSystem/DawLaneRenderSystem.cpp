#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <vector>

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

namespace DawLaneRenderSystemLogic {
    namespace {
        constexpr float kLaneAlpha = 0.85f;
        constexpr float kRulerHeight = 13.0f;
        constexpr float kRulerInset = 10.0f;
        constexpr float kRulerSideInset = -15.0f;
        constexpr float kRulerLowerOffset = 0.0f;
        constexpr float kRulerGap = 6.0f;
        constexpr float kPlayheadHandleSize = 12.0f;
        constexpr float kPlayheadHandleYOffset = 14.0f;

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

        int previewSlot = -1;
        if (laneCount > 0 && daw.dragActive && daw.dragLaneType == 0) {
            previewSlot = daw.dragDropIndex;
        } else if (daw.externalDropActive && daw.externalDropType == 0) {
            previewSlot = daw.externalDropIndex;
        }

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
        }

        if (daw.clipDragActive && daw.clipDragTrack >= 0 && daw.clipDragIndex >= 0
            && daw.clipDragTrack < audioTrackCount) {
            const auto audioLaneIndex = DawLaneTimelineSystemLogic::BuildAudioLaneIndex(daw, audioTrackCount);
            DawTrack& track = daw.tracks[static_cast<size_t>(daw.clipDragTrack)];
            if (daw.clipDragIndex < static_cast<int>(track.clips.size())) {
                const DawClip& clip = track.clips[static_cast<size_t>(daw.clipDragIndex)];
                int targetTrack = daw.clipDragTargetTrack;
                if (targetTrack >= 0 && targetTrack < audioTrackCount) {
                    int laneIndex = audioLaneIndex[static_cast<size_t>(targetTrack)];
                    if (laneIndex >= 0) {
                        int displayIndex = laneIndex;
                        if (previewSlot >= 0 && laneIndex >= previewSlot) {
                            displayIndex += 1;
                        }
                        float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                        float top = centerY - laneHalfH + 6.0f;
                        float bottom = centerY + laneHalfH - 6.0f;
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
                            float x0 = laneLeft + (laneRight - laneLeft) * t0;
                            float x1 = laneLeft + (laneRight - laneLeft) * t1;
                            glm::vec3 ghostColor = glm::clamp(playheadColor + glm::vec3(0.12f), glm::vec3(0.0f), glm::vec3(1.0f));
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
            int displayIndex = daw.selectedLaneIndex;
            if (previewSlot >= 0 && daw.selectedLaneIndex >= previewSlot) {
                displayIndex += 1;
            }
            float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
            float top = centerY - laneHalfH;
            float bottom = centerY + laneHalfH;
            glm::vec3 selectedHighlight = glm::clamp(selectedColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
            glm::vec3 selectedShadow = glm::clamp(selectedColor - glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f));
            float bevelDepth = 6.0f;
            glm::vec2 frontA(laneLeft, top);
            glm::vec2 frontB(laneRight, top);
            glm::vec2 frontC(laneRight, bottom);
            glm::vec2 frontD(laneLeft, bottom);
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

        int insertSlot = daw.dragActive ? daw.dragDropIndex
            : (daw.externalDropActive && daw.externalDropType == 0 ? daw.externalDropIndex : -1);
        if ((daw.dragActive && daw.dragLaneType == 0) || (daw.externalDropActive && daw.externalDropType == 0)) {
            float ghostCenterY = daw.dragActive
                ? static_cast<float>(ui.cursorY)
                : (startY + static_cast<float>(daw.externalDropIndex) * rowSpan);
            float ghostTop = ghostCenterY - laneHalfH;
            float ghostBottom = ghostCenterY + laneHalfH;
            glm::vec3 ghostColor = glm::clamp(selectedColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
            pushQuad(vertices,
                     pixelToNDC({laneLeft, ghostTop}, layout.screenWidth, layout.screenHeight),
                     pixelToNDC({laneRight, ghostTop}, layout.screenWidth, layout.screenHeight),
                     pixelToNDC({laneRight, ghostBottom}, layout.screenWidth, layout.screenHeight),
                     pixelToNDC({laneLeft, ghostBottom}, layout.screenWidth, layout.screenHeight),
                     ghostColor);
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

        DawLaneResourceSystemLogic::SetLaneTotalVertexCount(vertices.size());
        size_t totalCount = DawLaneResourceSystemLogic::GetLaneTotalVertexCount();
        if (totalCount == 0 || !renderer.uiColorShader) return;

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendColor(0.0f, 0.0f, 0.0f, kLaneAlpha);
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
