#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace DawStateSystemLogic { bool MoveTrack(BaseSystem& baseSystem, int fromIndex, int toIndex); }

namespace DawLaneSystemLogic {
    namespace {
        constexpr float kLaneAlpha = 0.85f;
        constexpr float kLaneLeftMargin = 40.0f;
        constexpr float kLaneRightMargin = 40.0f;

        struct UiVertex { glm::vec2 pos; glm::vec3 color; };
        static std::vector<UiVertex> g_laneVertices;
        static size_t g_staticVertexCount = 0;
        static size_t g_totalVertexCount = 0;
        static int g_cachedWidth = 0;
        static int g_cachedHeight = 0;
        static float g_cachedScrollY = 0.0f;
        static int g_cachedTrackCount = 0;
        static int64_t g_cachedTimelineOffset = 0;
        static double g_cachedSecondsPerScreen = 10.0;
        static std::vector<int> g_cachedLaneSignature;
        static std::vector<uint64_t> g_waveVersions;
        constexpr size_t kWaveformBlockSize = 256;

        bool hasDawUiWorld(const LevelContext& level) {
            for (const auto& world : level.worlds) {
                if (world.name == "DAWScreenWorld") return true;
                if (world.name == "TrackRowWorld") return true;
                if (world.name.rfind("TrackRowWorld_", 0) == 0) return true;
            }
            return false;
        }

        glm::vec2 pixelToNDC(const glm::vec2& pixel, double width, double height) {
            float ndcX = static_cast<float>((pixel.x / width) * 2.0 - 1.0);
            float ndcY = static_cast<float>(1.0 - (pixel.y / height) * 2.0);
            return {ndcX, ndcY};
        }

        bool cursorInLaneRect(const UIContext& ui, float laneLeft, float laneRight, float top, float bottom) {
            float x = static_cast<float>(ui.cursorX);
            float y = static_cast<float>(ui.cursorY);
            return x >= laneLeft && x <= laneRight && y >= top && y <= bottom;
        }

        int laneIndexFromCursorY(float y, float startY, float laneHalfH, float rowSpan, int trackCount) {
            for (int i = 0; i < trackCount; ++i) {
                float centerY = startY + static_cast<float>(i) * rowSpan;
                if (y >= centerY - laneHalfH && y <= centerY + laneHalfH) {
                    return i;
                }
            }
            return -1;
        }

        int dropSlotFromCursorY(float y, float startY, float rowSpan, int trackCount) {
            float rel = (y - startY) / rowSpan;
            int slot = static_cast<int>(std::floor(rel + 0.5f));
            if (slot < 0) slot = 0;
            if (slot > trackCount) slot = trackCount;
            return slot;
        }

        void pushQuad(std::vector<UiVertex>& verts,
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

        void ensureResources(RendererContext& renderer, WorldContext& world) {
            if (!renderer.uiColorShader) {
                renderer.uiColorShader = std::make_unique<Shader>(world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(), world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.uiLaneVAO == 0) {
                glGenVertexArrays(1, &renderer.uiLaneVAO);
                glGenBuffers(1, &renderer.uiLaneVBO);
            }
        }
    }

    void UpdateDawLanes(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt;
        if (!baseSystem.ui || !baseSystem.daw || !baseSystem.renderer || !baseSystem.world || !baseSystem.level || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        if (!hasDawUiWorld(*baseSystem.level)) return;
        bool allowLaneInput = true;
        if (baseSystem.panel && baseSystem.panel->rightState > 0.01f) {
            PanelRect rightRect = (baseSystem.panel->rightRenderRect.w > 0.0f)
                ? baseSystem.panel->rightRenderRect
                : baseSystem.panel->rightRect;
            float cx = static_cast<float>(ui.cursorX);
            float cy = static_cast<float>(ui.cursorY);
            if (cx >= rightRect.x && cx <= rightRect.x + rightRect.w
                && cy >= rightRect.y && cy <= rightRect.y + rightRect.h) {
                allowLaneInput = false;
            }
        }

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureResources(renderer, world);

        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowSize(win, &windowWidth, &windowHeight);
        double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
        double screenHeight = windowHeight > 0 ? static_cast<double>(windowHeight) : 1080.0;

        DawContext& daw = *baseSystem.daw;
        int audioTrackCount = static_cast<int>(daw.tracks.size());
        int laneCount = static_cast<int>(daw.laneOrder.size());
        if (laneCount == 0) {
            laneCount = audioTrackCount;
        }
        if (laneCount <= 0) return;
        const float laneHeight = 60.0f;
        const float laneGap = 12.0f;
        const float laneHalfH = laneHeight * 0.5f;
        float laneLeft = kLaneLeftMargin;
        float laneRight = static_cast<float>(screenWidth) - kLaneRightMargin;
        if (laneRight < laneLeft + 200.0f) {
            laneRight = laneLeft + 200.0f;
        }
        float scrollY = 0.0f;
        if (baseSystem.uiStamp) {
            scrollY = baseSystem.uiStamp->scrollY;
        }
        const float startY = 260.0f + scrollY;
        double secondsPerScreen = (daw.timelineSecondsPerScreen > 0.0) ? daw.timelineSecondsPerScreen : 10.0;

        glm::vec3 laneColor(0.14f, 0.14f, 0.14f);
        glm::vec3 laneHighlight(0.2f, 0.2f, 0.2f);
        glm::vec3 laneShadow(0.08f, 0.08f, 0.08f);
        glm::vec3 waveformBaseColor(0.05f, 0.05f, 0.05f);
        auto itBase = world.colorLibrary.find("MiraLaneBase");
        if (itBase != world.colorLibrary.end()) {
            laneColor = itBase->second;
            auto itHighlight = world.colorLibrary.find("MiraLaneHighlight");
            if (itHighlight != world.colorLibrary.end()) {
                laneHighlight = itHighlight->second;
            } else {
                laneHighlight = glm::clamp(laneColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
            }
            auto itShadow = world.colorLibrary.find("MiraLaneShadow");
            if (itShadow != world.colorLibrary.end()) {
                laneShadow = itShadow->second;
            } else {
                laneShadow = glm::clamp(laneColor - glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
            }
            auto itWave = world.colorLibrary.find("MiraWaveform");
            if (itWave != world.colorLibrary.end()) {
                waveformBaseColor = itWave->second;
            } else {
                waveformBaseColor = glm::clamp(laneShadow - glm::vec3(0.2f), glm::vec3(0.0f), glm::vec3(1.0f));
            }
        } else {
            auto it = world.colorLibrary.find("DarkGray");
            if (it != world.colorLibrary.end()) {
                laneColor = glm::clamp(it->second + glm::vec3(0.02f), glm::vec3(0.0f), glm::vec3(1.0f));
                laneHighlight = glm::clamp(it->second + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
                laneShadow = glm::clamp(it->second - glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f));
                waveformBaseColor = glm::clamp(it->second - glm::vec3(0.2f), glm::vec3(0.0f), glm::vec3(1.0f));
            }
        }

        bool rebuildStatic = (windowWidth != g_cachedWidth) || (windowHeight != g_cachedHeight);
        if (std::abs(scrollY - g_cachedScrollY) > 0.01f) {
            rebuildStatic = true;
        }
        if (audioTrackCount != g_cachedTrackCount) {
            rebuildStatic = true;
        }
        if (std::abs(secondsPerScreen - g_cachedSecondsPerScreen) > 0.0001) {
            rebuildStatic = true;
        }
        if (daw.timelineOffsetSamples != g_cachedTimelineOffset) {
            rebuildStatic = true;
        }
        if (g_waveVersions.size() != static_cast<size_t>(audioTrackCount)) {
            g_waveVersions.assign(static_cast<size_t>(audioTrackCount), 0);
            rebuildStatic = true;
        }
        for (int t = 0; t < audioTrackCount; ++t) {
            if (daw.tracks[t].waveformVersion != g_waveVersions[static_cast<size_t>(t)]) {
                rebuildStatic = true;
                break;
            }
        }

        std::vector<int> audioLaneIndex(static_cast<size_t>(audioTrackCount), -1);
        if (!daw.laneOrder.empty()) {
            for (size_t laneIdx = 0; laneIdx < daw.laneOrder.size(); ++laneIdx) {
                const auto& entry = daw.laneOrder[laneIdx];
                if (entry.type == 0 && entry.trackIndex >= 0 && entry.trackIndex < audioTrackCount) {
                    audioLaneIndex[static_cast<size_t>(entry.trackIndex)] = static_cast<int>(laneIdx);
                }
            }
        } else {
            for (int i = 0; i < audioTrackCount; ++i) {
                audioLaneIndex[static_cast<size_t>(i)] = i;
            }
        }

        std::vector<int> laneSignature;
        laneSignature.reserve(daw.laneOrder.size() * 2);
        for (const auto& entry : daw.laneOrder) {
            laneSignature.push_back(entry.type);
            laneSignature.push_back(entry.trackIndex);
        }
        if (laneSignature != g_cachedLaneSignature) {
            rebuildStatic = true;
            g_cachedLaneSignature = laneSignature;
        }

        float rowSpan = laneHeight + laneGap;
        auto buildLanes = [&](int previewSlot) {
            g_cachedWidth = windowWidth;
            g_cachedHeight = windowHeight;
            g_cachedScrollY = scrollY;
            g_cachedTrackCount = audioTrackCount;
            g_cachedTimelineOffset = daw.timelineOffsetSamples;
            g_cachedSecondsPerScreen = secondsPerScreen;
            g_laneVertices.clear();
            g_laneVertices.reserve(static_cast<size_t>(audioTrackCount) * 18);

            for (int i = 0; i < audioTrackCount; ++i) {
                int laneIndex = audioLaneIndex[static_cast<size_t>(i)];
                if (laneIndex < 0) continue;
                int displayIndex = laneIndex;
                if (previewSlot >= 0 && laneIndex >= previewSlot) {
                    displayIndex += 1;
                }
                float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                float top = centerY - laneHalfH;
                float bottom = centerY + laneHalfH;

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

                pushQuad(g_laneVertices,
                         pixelToNDC(frontA, screenWidth, screenHeight),
                         pixelToNDC(frontB, screenWidth, screenHeight),
                         pixelToNDC(frontC, screenWidth, screenHeight),
                         pixelToNDC(frontD, screenWidth, screenHeight),
                         laneColor);
                pushQuad(g_laneVertices,
                         pixelToNDC(topA, screenWidth, screenHeight),
                         pixelToNDC(topB, screenWidth, screenHeight),
                         pixelToNDC(topC, screenWidth, screenHeight),
                         pixelToNDC(topD, screenWidth, screenHeight),
                         laneHighlight);
                pushQuad(g_laneVertices,
                         pixelToNDC(leftA, screenWidth, screenHeight),
                         pixelToNDC(leftB, screenWidth, screenHeight),
                         pixelToNDC(leftC, screenWidth, screenHeight),
                         pixelToNDC(leftD, screenWidth, screenHeight),
                         laneShadow);
            }

            float waveHeight = laneHeight * 0.8f;
            float ampScale = waveHeight * 0.5f;
            int pixelWidth = static_cast<int>(laneRight - laneLeft);
            if (pixelWidth > 0) {
                double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                if (windowSamples < 0.0) windowSamples = 0.0;
                for (int t = 0; t < audioTrackCount; ++t) {
                    const DawTrack& track = daw.tracks[t];
                    if (track.waveformMin.empty() || track.waveformMax.empty()) continue;
                    size_t blockCount = track.waveformMin.size();
                    if (blockCount == 0) continue;
                    int laneIndex = audioLaneIndex[static_cast<size_t>(t)];
                    if (laneIndex < 0) continue;
                    int displayIndex = laneIndex;
                    if (previewSlot >= 0 && laneIndex >= previewSlot) {
                        displayIndex += 1;
                    }
                    float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                    float top = centerY - laneHalfH;
                    float bottom = centerY + laneHalfH;
                    for (int x = 0; x < pixelWidth; ++x) {
                        double samplePos = offsetSamples + (static_cast<double>(x) / static_cast<double>(pixelWidth)) * windowSamples;
                        if (samplePos < 0.0) continue;
                        size_t idx = static_cast<size_t>(samplePos) / kWaveformBlockSize;
                        if (idx >= blockCount) continue;
                        float minVal = track.waveformMin[idx];
                        float maxVal = track.waveformMax[idx];
                        float yTop = centerY - maxVal * ampScale;
                        float yBottom = centerY - minVal * ampScale;
                        yTop = std::max(yTop, top);
                        yBottom = std::min(yBottom, bottom);
                        float xPos = laneLeft + static_cast<float>(x);
                        float lineWidth = 1.0f;
                        glm::vec3 blockColor = waveformBaseColor;
                        if (idx < track.waveformColor.size()) {
                            blockColor = track.waveformColor[idx];
                        }
                        glm::vec2 a(xPos - lineWidth, yTop);
                        glm::vec2 b(xPos + lineWidth, yTop);
                        glm::vec2 c(xPos + lineWidth, yBottom);
                        glm::vec2 d(xPos - lineWidth, yBottom);
                        pushQuad(g_laneVertices,
                                 pixelToNDC(a, screenWidth, screenHeight),
                                 pixelToNDC(b, screenWidth, screenHeight),
                                 pixelToNDC(c, screenWidth, screenHeight),
                                 pixelToNDC(d, screenWidth, screenHeight),
                                 blockColor);
                    }
                }
            }

            g_staticVertexCount = g_laneVertices.size();
            g_laneVertices.resize(g_staticVertexCount + 6);
            for (int t = 0; t < audioTrackCount; ++t) {
                g_waveVersions[static_cast<size_t>(t)] = daw.tracks[t].waveformVersion;
            }
        };

        if (rebuildStatic) {
            buildLanes(-1);
        }

        float topBound = startY - laneHalfH;
        float bottomBound = startY + (laneCount - 1) * rowSpan + laneHalfH;
        if (allowLaneInput && ui.uiLeftPressed && !ui.consumeClick) {
            if (cursorInLaneRect(ui, laneLeft, laneRight, topBound, bottomBound)) {
                int laneIdx = laneIndexFromCursorY(static_cast<float>(ui.cursorY), startY, laneHalfH, rowSpan, laneCount);
                if (laneIdx >= 0) {
                    daw.selectedLaneIndex = laneIdx;
                    if (!daw.laneOrder.empty() && laneIdx < static_cast<int>(daw.laneOrder.size())) {
                        const auto& entry = daw.laneOrder[static_cast<size_t>(laneIdx)];
                        daw.selectedLaneType = entry.type;
                        daw.selectedLaneTrack = entry.trackIndex;
                        daw.dragLaneType = entry.type;
                        daw.dragLaneTrack = entry.trackIndex;
                    } else {
                        daw.selectedLaneType = 0;
                        daw.selectedLaneTrack = laneIdx;
                        daw.dragLaneType = 0;
                        daw.dragLaneTrack = laneIdx;
                    }
                    daw.dragLaneIndex = laneIdx;
                    daw.dragStartY = static_cast<float>(ui.cursorY);
                    daw.dragPending = true;
                    daw.dragActive = false;
                    ui.consumeClick = true;
                }
            }
        }

        if (allowLaneInput && daw.dragPending && ui.uiLeftDown) {
            float dy = std::abs(static_cast<float>(ui.cursorY) - daw.dragStartY);
            if (!daw.dragActive && dy > 4.0f) {
                daw.dragActive = true;
            }
        }

        if (allowLaneInput && daw.dragActive) {
            if (cursorInLaneRect(ui, laneLeft, laneRight, topBound - laneGap, bottomBound + laneGap)) {
                daw.dragDropIndex = dropSlotFromCursorY(static_cast<float>(ui.cursorY), startY, rowSpan, laneCount);
            } else {
                daw.dragDropIndex = -1;
            }
        }

        if (!ui.uiLeftDown && (daw.dragPending || daw.dragActive)) {
            if (daw.dragActive && daw.dragDropIndex >= 0 && daw.dragLaneIndex >= 0) {
                int fromIndex = daw.dragLaneIndex;
                int dropSlot = daw.dragDropIndex;
                int toIndex = dropSlot;
                if (dropSlot > fromIndex) {
                    toIndex = dropSlot - 1;
                }
                if (toIndex != fromIndex) {
                    if (DawStateSystemLogic::MoveTrack(baseSystem, fromIndex, toIndex)) {
                        daw.selectedLaneIndex = toIndex;
                    }
                }
            }
            daw.dragPending = false;
            daw.dragActive = false;
            daw.dragLaneIndex = -1;
            daw.dragLaneType = -1;
            daw.dragLaneTrack = -1;
            daw.dragDropIndex = -1;
        }

        int previewSlot = -1;
        if (daw.dragActive && daw.dragLaneType == 0) {
            previewSlot = daw.dragDropIndex;
        } else if (daw.externalDropActive && daw.externalDropType == 0) {
            previewSlot = daw.externalDropIndex;
        }
        if (previewSlot >= 0) {
            buildLanes(previewSlot);
        }

        double playheadSec = static_cast<double>(daw.playheadSample.load(std::memory_order_relaxed)) / static_cast<double>(daw.sampleRate);
        double offsetSec = static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate);
        double tNorm = secondsPerScreen > 0.0 ? (playheadSec - offsetSec) / secondsPerScreen : 0.0;
        bool clamped = false;
        if (tNorm < 0.0) { tNorm = 0.0; clamped = true; }
        if (tNorm > 1.0) { tNorm = 1.0; clamped = true; }
        float playheadX = static_cast<float>(laneLeft + (laneRight - laneLeft) * tNorm);
        glm::vec3 playheadColor(0.2f, 0.2f, 0.2f);
        auto itText = world.colorLibrary.find("MiraText");
        if (itText != world.colorLibrary.end()) {
            playheadColor = itText->second;
        }
        float lineWidth = 0.5f;
        glm::vec2 pa(playheadX - lineWidth, startY - laneHalfH);
        glm::vec2 pb(playheadX + lineWidth, startY - laneHalfH);
        glm::vec2 pc(playheadX + lineWidth, startY + (laneCount - 1) * (laneHeight + laneGap) + laneHalfH);
        glm::vec2 pd(playheadX - lineWidth, startY + (laneCount - 1) * (laneHeight + laneGap) + laneHalfH);
        bool showPlayhead = daw.playheadSample.load(std::memory_order_relaxed) > 0;
        g_laneVertices.resize(g_staticVertexCount);
        if (showPlayhead) {
            pushQuad(g_laneVertices,
                     pixelToNDC(pa, screenWidth, screenHeight),
                     pixelToNDC(pb, screenWidth, screenHeight),
                     pixelToNDC(pc, screenWidth, screenHeight),
                     pixelToNDC(pd, screenWidth, screenHeight),
                     playheadColor);
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
            pushQuad(g_laneVertices,
                     pixelToNDC(frontA, screenWidth, screenHeight),
                     pixelToNDC(frontB, screenWidth, screenHeight),
                     pixelToNDC(frontC, screenWidth, screenHeight),
                     pixelToNDC(frontD, screenWidth, screenHeight),
                     selectedColor);
            pushQuad(g_laneVertices,
                     pixelToNDC(topA, screenWidth, screenHeight),
                     pixelToNDC(topB, screenWidth, screenHeight),
                     pixelToNDC(topC, screenWidth, screenHeight),
                     pixelToNDC(topD, screenWidth, screenHeight),
                     selectedHighlight);
            pushQuad(g_laneVertices,
                     pixelToNDC(leftA, screenWidth, screenHeight),
                     pixelToNDC(leftB, screenWidth, screenHeight),
                     pixelToNDC(leftC, screenWidth, screenHeight),
                     pixelToNDC(leftD, screenWidth, screenHeight),
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
            pushQuad(g_laneVertices,
                     pixelToNDC({laneLeft, ghostTop}, screenWidth, screenHeight),
                     pixelToNDC({laneRight, ghostTop}, screenWidth, screenHeight),
                     pixelToNDC({laneRight, ghostBottom}, screenWidth, screenHeight),
                     pixelToNDC({laneLeft, ghostBottom}, screenWidth, screenHeight),
                     ghostColor);
            if (insertSlot >= 0) {
                float insertY = startY + (static_cast<float>(insertSlot) - 0.5f) * rowSpan;
                float lineHalf = 2.0f;
                pushQuad(g_laneVertices,
                         pixelToNDC({laneLeft, insertY - lineHalf}, screenWidth, screenHeight),
                         pixelToNDC({laneRight, insertY - lineHalf}, screenWidth, screenHeight),
                         pixelToNDC({laneRight, insertY + lineHalf}, screenWidth, screenHeight),
                         pixelToNDC({laneLeft, insertY + lineHalf}, screenWidth, screenHeight),
                         selectedColor);
            }
        }

        g_totalVertexCount = g_laneVertices.size();
        if (g_totalVertexCount == 0 || !renderer.uiColorShader) return;

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendColor(0.0f, 0.0f, 0.0f, kLaneAlpha);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        glBindVertexArray(renderer.uiLaneVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.uiLaneVBO);
        glBufferData(GL_ARRAY_BUFFER, g_totalVertexCount * sizeof(UiVertex), g_laneVertices.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, pos));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, color));
        glEnableVertexAttribArray(1);
        renderer.uiColorShader->use();
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(g_totalVertexCount));
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
    }
}
