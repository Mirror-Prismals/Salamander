#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "stb_easy_font.h"

namespace PianoRollResourceSystemLogic {
    struct UiVertex;
    struct Key;
    struct PianoRollState;
    struct PianoRollConfig;
    enum class ScaleType;
    PianoRollState& State();
    const PianoRollConfig& Config();
    const std::array<const char*, 12>& NoteNames();
    const std::array<const char*, 7>& ModeNames();
    const std::array<const char*, 7>& ScaleNames();
    const std::vector<std::string>& SnapOptions();
    void NoteColor(int noteIndex, float& r, float& g, float& b);
    std::string FormatButtonValue(const std::string& value);
}

namespace PianoRollRenderSystemLogic {
    namespace {
        std::vector<PianoRollResourceSystemLogic::UiVertex> g_vertices;

        glm::vec2 pixelToNDC(const glm::vec2& pixel, double width, double height) {
            float ndcX = static_cast<float>((pixel.x / width) * 2.0 - 1.0);
            float ndcY = static_cast<float>(1.0 - (pixel.y / height) * 2.0);
            return {ndcX, ndcY};
        }

        void pushQuad(std::vector<PianoRollResourceSystemLogic::UiVertex>& verts,
                      const glm::vec2& a,
                      const glm::vec2& b,
                      const glm::vec2& c,
                      const glm::vec2& d,
                      const glm::vec3& color,
                      double width,
                      double height) {
            verts.push_back({pixelToNDC(a, width, height), color});
            verts.push_back({pixelToNDC(b, width, height), color});
            verts.push_back({pixelToNDC(c, width, height), color});
            verts.push_back({pixelToNDC(a, width, height), color});
            verts.push_back({pixelToNDC(c, width, height), color});
            verts.push_back({pixelToNDC(d, width, height), color});
        }

        void pushRect(std::vector<PianoRollResourceSystemLogic::UiVertex>& verts, float x, float y, float w, float h, const glm::vec3& color, double width, double height) {
            pushQuad(verts, {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, color, width, height);
        }

        void pushLine(std::vector<PianoRollResourceSystemLogic::UiVertex>& verts, float x0, float y0, float x1, float y1, float thickness, const glm::vec3& color, double width, double height) {
            float dx = x1 - x0;
            float dy = y1 - y0;
            float len = std::sqrt(dx * dx + dy * dy);
            if (len <= 0.0f) return;
            float nx = -dy / len;
            float ny = dx / len;
            float hx = nx * (thickness * 0.5f);
            float hy = ny * (thickness * 0.5f);
            glm::vec2 a{x0 - hx, y0 - hy};
            glm::vec2 b{x1 - hx, y1 - hy};
            glm::vec2 c{x1 + hx, y1 + hy};
            glm::vec2 d{x0 + hx, y0 + hy};
            pushQuad(verts, a, b, c, d, color, width, height);
        }

        void pushRectOutline(std::vector<PianoRollResourceSystemLogic::UiVertex>& verts, float x, float y, float w, float h, float thickness, const glm::vec3& color, double width, double height) {
            pushLine(verts, x, y, x + w, y, thickness, color, width, height);
            pushLine(verts, x + w, y, x + w, y + h, thickness, color, width, height);
            pushLine(verts, x + w, y + h, x, y + h, thickness, color, width, height);
            pushLine(verts, x, y + h, x, y, thickness, color, width, height);
        }

        void pushText(std::vector<PianoRollResourceSystemLogic::UiVertex>& verts, float x, float y, const char* text, const glm::vec3& color, double width, double height) {
            if (!text || text[0] == '\0') return;
            char buffer[99999];
            int numQuads = stb_easy_font_print(x, y, const_cast<char*>(text), nullptr, buffer, sizeof(buffer));
            float* vertsRaw = reinterpret_cast<float*>(buffer);
            for (int i = 0; i < numQuads; ++i) {
                int base = i * 16;
                glm::vec2 v0{vertsRaw[base + 0], vertsRaw[base + 1]};
                glm::vec2 v1{vertsRaw[base + 4], vertsRaw[base + 5]};
                glm::vec2 v2{vertsRaw[base + 8], vertsRaw[base + 9]};
                glm::vec2 v3{vertsRaw[base + 12], vertsRaw[base + 13]};
                pushQuad(verts, v0, v1, v2, v3, color, width, height);
            }
        }

        void pushMultilineText(std::vector<PianoRollResourceSystemLogic::UiVertex>& verts, float x, float y, const std::string& text, float lineHeight, const glm::vec3& color, double width, double height) {
            size_t start = 0;
            float lineY = y;
            while (start <= text.size()) {
                size_t end = text.find('\n', start);
                std::string line = (end == std::string::npos) ? text.substr(start) : text.substr(start, end - start);
                if (!line.empty()) {
                    pushText(verts, x, lineY, line.c_str(), color, width, height);
                }
                if (end == std::string::npos) break;
                start = end + 1;
                lineY += lineHeight;
            }
        }

        void drawBeveledQuad(const PianoRollResourceSystemLogic::Key& key, float offsetY, const glm::vec3& baseColor, std::vector<PianoRollResourceSystemLogic::UiVertex>& verts, double width, double height) {
            glm::vec3 lightColor = glm::clamp(baseColor + glm::vec3(0.10f), glm::vec3(0.0f), glm::vec3(1.0f));
            glm::vec3 sideColor = glm::clamp(baseColor - glm::vec3(0.12f), glm::vec3(0.0f), glm::vec3(1.0f));

            float shiftRight = 4.0f * key.pressAnim;
            float bevel = key.depth * (1.0f - 0.5f * key.pressAnim);
            float x = key.x + shiftRight;
            float y = key.y + offsetY;
            float w = key.w;
            float h = key.h;

            glm::vec2 frontA{x, y};
            glm::vec2 frontB{x + w, y};
            glm::vec2 frontC{x + w, y + h};
            glm::vec2 frontD{x, y + h};
            glm::vec2 topA = frontA;
            glm::vec2 topB = frontB;
            glm::vec2 topC{frontB.x + bevel, frontB.y - bevel};
            glm::vec2 topD{frontA.x + bevel, frontA.y - bevel};
            glm::vec2 rightA = frontB;
            glm::vec2 rightB = frontC;
            glm::vec2 rightC{frontC.x + bevel, frontC.y - bevel};
            glm::vec2 rightD{frontB.x + bevel, frontB.y - bevel};

            pushQuad(verts, frontA, frontB, frontC, frontD, baseColor, width, height);
            pushQuad(verts, topA, topB, topC, topD, lightColor, width, height);
            pushQuad(verts, rightA, rightB, rightC, rightD, sideColor, width, height);
        }

        void drawBeveledQuadTint(const PianoRollResourceSystemLogic::Key& key, float offsetY, float fr, float fg, float fb, std::vector<PianoRollResourceSystemLogic::UiVertex>& verts, double width, double height) {
            glm::vec3 baseColor(fr, fg, fb);
            drawBeveledQuad(key, offsetY, baseColor, verts, width, height);
        }
    }

    void UpdatePianoRollRender(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow* win) {
        PianoRollResourceSystemLogic::PianoRollState& state = PianoRollResourceSystemLogic::State();
        if (!state.active || !state.layoutReady) return;
        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.midi || !win) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        MidiContext& midi = *baseSystem.midi;

        if (!renderer.uiColorShader) return;

        int trackIndex = state.layout.trackIndex;
        int clipIndex = state.layout.clipIndex;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(midi.tracks.size())) return;
        if (clipIndex < 0 || clipIndex >= static_cast<int>(midi.tracks[trackIndex].clips.size())) return;
        const MidiTrack& track = midi.tracks[trackIndex];

        const auto& cfg = PianoRollResourceSystemLogic::Config();
        const auto& layout = state.layout;

        double screenWidth = layout.screenWidth;
        double screenHeight = layout.screenHeight;
        float gridLeft = layout.gridLeft;
        float gridRight = layout.gridRight;
        float viewTop = layout.viewTop;
        float viewBottom = layout.viewBottom;
        float gridOrigin = layout.gridOrigin + state.scrollOffsetY;
        float gridStep = layout.gridStep;
        int totalRows = cfg.totalRows;
        double pxPerSample = layout.pxPerSample;

        g_vertices.clear();
        g_vertices.reserve(4096);

        auto clampColorOffset = [](const glm::vec3& color, float offset) -> glm::vec3 {
            return glm::clamp(color + glm::vec3(offset), glm::vec3(0.0f), glm::vec3(1.0f));
        };
        glm::vec3 pianoBase(0.0f, 0.12f, 0.12f);
        glm::vec3 pianoAccent(0.0f, 0.20f, 0.20f);
        auto itPianoBase = world.colorLibrary.find("MiraPianoRollBase");
        if (itPianoBase != world.colorLibrary.end()) {
            pianoBase = itPianoBase->second;
        }
        auto itPianoAccent = world.colorLibrary.find("MiraPianoRollAccent");
        if (itPianoAccent != world.colorLibrary.end()) {
            pianoAccent = itPianoAccent->second;
        }
        const glm::vec3 backdrop = pianoBase;
        const glm::vec3 gridWhite = pianoAccent;
        const glm::vec3 gridBlack = clampColorOffset(pianoBase, 0.03f);
        const glm::vec3 gridLine = clampColorOffset(pianoAccent, 0.03f);
        const glm::vec3 majorBeatLine = clampColorOffset(pianoAccent, 0.05f);
        const glm::vec3 beatLine = clampColorOffset(pianoAccent, 0.02f);
        const glm::vec3 snapLine = clampColorOffset(pianoBase, 0.06f);
        const glm::vec3 borderColor = clampColorOffset(pianoAccent, -0.012f);
        pushRect(g_vertices, 0.0f, 0.0f, static_cast<float>(screenWidth), static_cast<float>(screenHeight), backdrop, screenWidth, screenHeight);

        int startRow = static_cast<int>(std::floor((gridOrigin - viewBottom) / gridStep));
        int endRow = static_cast<int>(std::ceil((gridOrigin - viewTop) / gridStep));
        if (startRow < 0) startRow = 0;
        if (endRow > totalRows - 1) endRow = totalRows - 1;

        auto isBlackRow = [](int row) {
            int noteIndex = (row + 24) % 12;
            if (noteIndex < 0) noteIndex += 12;
            return noteIndex == 1 || noteIndex == 3 || noteIndex == 6 || noteIndex == 8 || noteIndex == 10;
        };
        for (int row = startRow; row <= endRow; ++row) {
            float y0 = gridOrigin - (row + 1) * gridStep;
            float y1 = gridOrigin - row * gridStep;
            const glm::vec3& rowColor = isBlackRow(row) ? gridBlack : gridWhite;
            pushRect(g_vertices, gridLeft, y0, gridRight - gridLeft, y1 - y0, rowColor, screenWidth, screenHeight);
        }
        std::vector<std::pair<float, float>> editableRanges;
        editableRanges.reserve(track.clips.size());
        for (const auto& laneClip : track.clips) {
            if (laneClip.length == 0) continue;
            float x0 = gridLeft + state.scrollOffsetX + static_cast<float>(laneClip.startSample * pxPerSample);
            float x1 = x0 + static_cast<float>(laneClip.length * pxPerSample);
            x0 = std::max(x0, gridLeft);
            x1 = std::min(x1, gridRight);
            if (x1 <= x0) continue;
            editableRanges.push_back({x0, x1});
        }
        std::sort(editableRanges.begin(), editableRanges.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<std::pair<float, float>> mergedRanges;
        mergedRanges.reserve(editableRanges.size());
        for (const auto& range : editableRanges) {
            if (mergedRanges.empty() || range.first > mergedRanges.back().second + 0.5f) {
                mergedRanges.push_back(range);
            } else {
                mergedRanges.back().second = std::max(mergedRanges.back().second, range.second);
            }
        }
        const glm::vec3 offLimitsColor = backdrop;
        float darkStartX = gridLeft;
        if (mergedRanges.empty()) {
            pushRect(g_vertices, gridLeft, viewTop, gridRight - gridLeft, viewBottom - viewTop, offLimitsColor, screenWidth, screenHeight);
        } else {
            for (const auto& range : mergedRanges) {
                if (range.first > darkStartX) {
                    pushRect(g_vertices, darkStartX, viewTop, range.first - darkStartX, viewBottom - viewTop, offLimitsColor, screenWidth, screenHeight);
                }
                darkStartX = std::max(darkStartX, range.second);
            }
            if (darkStartX < gridRight) {
                pushRect(g_vertices, darkStartX, viewTop, gridRight - darkStartX, viewBottom - viewTop, offLimitsColor, screenWidth, screenHeight);
            }
        }
        for (int row = startRow; row <= endRow; ++row) {
            float y = gridOrigin - row * gridStep;
            pushLine(g_vertices, gridLeft, y, gridRight, y, 1.0f, gridLine, screenWidth, screenHeight);
        }
        pushLine(g_vertices, gridLeft, viewTop, gridLeft, viewBottom, 1.0f, gridLine, screenWidth, screenHeight);

        float barStartPx = gridLeft + state.scrollOffsetX;
        double barSamples = layout.barSamples;
        double beatSamples = layout.beatSamples;
        double zeroSample = 0.0;
        if (baseSystem.daw) {
            zeroSample = static_cast<double>(baseSystem.daw->timelineZeroSample);
        }
        float barStepPx = static_cast<float>(barSamples * pxPerSample);
        float beatStepPx = static_cast<float>(beatSamples * pxPerSample);
        // Convert viewport X offset back to sample-space using the actual scale.
        // pxPerSample is typically < 1, so clamping to 1.0 collapses bar indexing.
        double pxPerSampleSafe = std::max(1e-9, pxPerSample);
        double visibleStartSample = (-state.scrollOffsetX) / pxPerSampleSafe;
        if (visibleStartSample < 0.0) visibleStartSample = 0.0;
        double visibleEndSample = visibleStartSample + layout.samplesPerScreen;
        int barIndexStart = static_cast<int>(std::floor(visibleStartSample / std::max(1.0, barSamples))) - 1;
        int barIndexEnd = static_cast<int>(std::ceil(visibleEndSample / std::max(1.0, barSamples))) + 1;
        for (int i = barIndexStart; i <= barIndexEnd; ++i) {
            float x = barStartPx + static_cast<float>(i) * barStepPx;
            if (x < gridLeft - 2.0f || x > gridRight + 2.0f) continue;
            pushLine(g_vertices, x, viewTop, x, viewBottom, 1.0f, majorBeatLine, screenWidth, screenHeight);
        }
        for (int i = barIndexStart; i <= barIndexEnd; ++i) {
            float x = barStartPx + static_cast<float>(i) * barStepPx;
            if (x < gridLeft - 40.0f || x > gridRight - 8.0f) continue;
            double storageBarSample = static_cast<double>(i) * barSamples;
            int64_t barZeroBased = static_cast<int64_t>(std::floor((storageBarSample - zeroSample) / std::max(1.0, barSamples)));
            int64_t barLabelValue = (barZeroBased >= 0) ? (barZeroBased + 1) : barZeroBased;
            char label[16];
            std::snprintf(label, sizeof(label), "%lld", static_cast<long long>(barLabelValue));
            pushText(g_vertices, x + 4.0f, viewTop + 14.0f, label, glm::vec3(0.62f, 0.72f, 0.72f), screenWidth, screenHeight);
        }
        int beatIndexStart = static_cast<int>(std::floor(visibleStartSample / std::max(1.0, beatSamples))) - 1;
        int beatIndexEnd = static_cast<int>(std::ceil(visibleEndSample / std::max(1.0, beatSamples))) + 1;
        for (int i = beatIndexStart; i <= beatIndexEnd; ++i) {
            float x = barStartPx + static_cast<float>(i) * beatStepPx;
            if (x < gridLeft - 2.0f || x > gridRight + 2.0f) continue;
            pushLine(g_vertices, x, viewTop, x, viewBottom, 1.0f, beatLine, screenWidth, screenHeight);
        }
        if (layout.snapSamples > 0.0) {
            float snapStepPx = static_cast<float>(layout.snapSamples * pxPerSample);
            int snapIndexStart = static_cast<int>(std::floor(visibleStartSample / std::max(1.0, layout.snapSamples))) - 1;
            int snapIndexEnd = static_cast<int>(std::ceil(visibleEndSample / std::max(1.0, layout.snapSamples))) + 1;
            for (int i = snapIndexStart; i <= snapIndexEnd; ++i) {
                float x = barStartPx + static_cast<float>(i) * snapStepPx;
                if (x < gridLeft - 2.0f || x > gridRight + 2.0f) continue;
                pushLine(g_vertices, x, viewTop, x, viewBottom, 1.0f, snapLine, screenWidth, screenHeight);
            }
        }

        const auto& noteNames = PianoRollResourceSystemLogic::NoteNames();
        for (int row = startRow; row <= endRow; ++row) {
            if (row < 0 || row >= totalRows) continue;
            int noteIndex = row % 12;
            if (noteIndex < 0) noteIndex += 12;
            int octave = 1 + row / 12;
            char label[16];
            std::snprintf(label, sizeof(label), "%s%d", noteNames[noteIndex], octave);
            float y0 = gridOrigin - (row + 1) * gridStep;
            float y1 = gridOrigin - row * gridStep;
            if (y1 < viewTop || y0 > viewBottom) continue;
            float labelY = y0 + gridStep * 0.65f;
            pushText(g_vertices, gridLeft + 6.0f, labelY, label, glm::vec3(0.6f, 0.7f, 0.7f), screenWidth, screenHeight);
        }

        for (size_t laneClipIndex = 0; laneClipIndex < track.clips.size(); ++laneClipIndex) {
            const MidiClip& laneClip = track.clips[laneClipIndex];
            if (laneClip.length == 0) continue;
            float laneClipStartX = gridLeft + state.scrollOffsetX + static_cast<float>(laneClip.startSample * pxPerSample);
            float laneClipEndX = laneClipStartX + static_cast<float>(laneClip.length * pxPerSample);
            if (laneClipEndX < gridLeft - 4.0f || laneClipStartX > gridRight + 4.0f) {
                continue;
            }
            glm::vec3 clipEdge(0.24f, 0.40f, 0.44f);
            pushLine(g_vertices, laneClipStartX, viewTop, laneClipStartX, viewBottom, 1.0f, clipEdge, screenWidth, screenHeight);
            pushLine(g_vertices, laneClipEndX, viewTop, laneClipEndX, viewBottom, 1.0f, clipEdge, screenWidth, screenHeight);
            for (const auto& note : laneClip.notes) {
                int row = note.pitch - 24;
                if (row < startRow || row > endRow) continue;
                float nx = laneClipStartX + static_cast<float>(note.startSample * pxPerSample);
                float nw = std::max(2.0f, static_cast<float>(note.length * pxPerSample));
                if (nx + nw < gridLeft || nx > gridRight) continue;
                float ny = gridOrigin - (row + 1) * gridStep;
                int noteIndex = note.pitch % 12;
                if (noteIndex < 0) noteIndex += 12;
                float nr = 0.75f, ng = 0.85f, nb = 0.9f;
                PianoRollResourceSystemLogic::NoteColor(noteIndex, nr, ng, nb);
                PianoRollResourceSystemLogic::Key noteKey{nx, ny, nw, gridStep, 4.0f, 20.0f, 0, 0, false, 0.0f};
                drawBeveledQuadTint(noteKey, 0.0f, nr, ng, nb, g_vertices, screenWidth, screenHeight);
                char label[16];
                int octave = (note.pitch / 12) - 1;
                std::snprintf(label, sizeof(label), "%s%d", noteNames[noteIndex], octave);
                pushText(g_vertices, nx + 4.0f, ny + gridStep - 12.0f, label, glm::vec3(0.1f, 0.2f, 0.25f), screenWidth, screenHeight);
            }
        }

        for (int i = static_cast<int>(state.deleteAnims.size()) - 1; i >= 0; --i) {
            float t = static_cast<float>(glfwGetTime() - state.deleteAnims[i].startTime);
            if (t > 0.2f) {
                state.deleteAnims.erase(state.deleteAnims.begin() + i);
                continue;
            }
            float expand = t * 20.0f;
            float x0 = state.deleteAnims[i].x - expand;
            float y0 = state.deleteAnims[i].y - expand;
            float x1 = state.deleteAnims[i].x + state.deleteAnims[i].w + expand;
            float y1 = state.deleteAnims[i].y + state.deleteAnims[i].h + expand;
            glm::vec3 color(state.deleteAnims[i].r, state.deleteAnims[i].g, state.deleteAnims[i].b);
            pushRectOutline(g_vertices, x0, y0, x1 - x0, y1 - y0, 1.0f, color, screenWidth, screenHeight);
        }

        for (const auto& key : state.whiteKeys) {
            drawBeveledQuadTint(key, state.scrollOffsetY, 0.8f, 0.8f, 0.8f, g_vertices, screenWidth, screenHeight);
        }
        for (const auto& key : state.blackKeys) {
            drawBeveledQuadTint(key, state.scrollOffsetY, 0.3f, 0.3f, 0.3f, g_vertices, screenWidth, screenHeight);
        }

        for (const auto& key : state.whiteKeys) {
            float y = key.y + state.scrollOffsetY;
            char label[16];
            std::snprintf(label, sizeof(label), "%s%d", noteNames[key.note], key.octave);
            float labelX = key.x + key.w - 22.0f;
            float labelY = y + key.h - 10.0f;
            pushText(g_vertices, labelX, labelY, label, glm::vec3(0.0f), screenWidth, screenHeight);
        }
        for (const auto& key : state.blackKeys) {
            float y = key.y + state.scrollOffsetY;
            char label[16];
            std::snprintf(label, sizeof(label), "%s%d", noteNames[key.note], key.octave);
            float labelX = key.x + key.w - 22.0f;
            float labelY = y + key.h - 10.0f;
            pushText(g_vertices, labelX, labelY, label, glm::vec3(0.9f), screenWidth, screenHeight);
        }

        pushRect(g_vertices, 0.0f, 0.0f, static_cast<float>(screenWidth), cfg.borderHeight, borderColor, screenWidth, screenHeight);
        pushRect(g_vertices, 0.0f, static_cast<float>(screenHeight) - cfg.borderHeight, static_cast<float>(screenWidth), cfg.borderHeight, borderColor, screenWidth, screenHeight);
        pushRect(g_vertices, 0.0f, cfg.borderHeight, cfg.leftBorderWidth, static_cast<float>(screenHeight) - 2.0f * cfg.borderHeight, borderColor, screenWidth, screenHeight);

        if (state.gridButton.isPressed || state.menuOpen) {
            pushRect(g_vertices, state.gridButton.x, state.gridButton.y, state.gridButton.w, state.gridButton.h, pianoAccent, screenWidth, screenHeight);
        }
        if (state.scaleButton.isPressed || state.scaleMenuOpen) {
            pushRect(g_vertices, state.scaleButton.x, state.scaleButton.y, state.scaleButton.w, state.scaleButton.h, pianoAccent, screenWidth, screenHeight);
        }

        pushText(g_vertices, state.modeDrawButton.x + 6.0f, 33.0f, "Draw", glm::vec3(0.85f, 0.9f, 0.9f), screenWidth, screenHeight);
        pushText(g_vertices, state.modePaintButton.x + 6.0f, 33.0f, "Paint", glm::vec3(0.85f, 0.9f, 0.9f), screenWidth, screenHeight);

        pushRect(g_vertices, layout.closeLeft, layout.closeTop, layout.closeSize, layout.closeSize, glm::vec3(0.75f, 0.28f, 0.25f), screenWidth, screenHeight);
        pushLine(g_vertices, layout.closeLeft + 7.0f, layout.closeTop + 7.0f, layout.closeLeft + layout.closeSize - 7.0f, layout.closeTop + layout.closeSize - 7.0f, 2.0f, glm::vec3(0.95f), screenWidth, screenHeight);
        pushLine(g_vertices, layout.closeLeft + layout.closeSize - 7.0f, layout.closeTop + 7.0f, layout.closeLeft + 7.0f, layout.closeTop + layout.closeSize - 7.0f, 2.0f, glm::vec3(0.95f), screenWidth, screenHeight);

        PianoRollResourceSystemLogic::Key drawKey{state.modeDrawButton.x, state.modeDrawButton.y, state.modeDrawButton.w, state.modeDrawButton.h, state.modeDrawButton.depth, 20.0f, 0, 0, false, state.modeDrawButton.pressAnim};
        PianoRollResourceSystemLogic::Key paintKey{state.modePaintButton.x, state.modePaintButton.y, state.modePaintButton.w, state.modePaintButton.h, state.modePaintButton.depth, 20.0f, 0, 0, false, state.modePaintButton.pressAnim};
        PianoRollResourceSystemLogic::Key scaleKey{state.scaleButton.x, state.scaleButton.y, state.scaleButton.w, state.scaleButton.h, state.scaleButton.depth, 20.0f, 0, 0, false, state.scaleButton.pressAnim};
        PianoRollResourceSystemLogic::Key buttonKey{state.gridButton.x, state.gridButton.y, state.gridButton.w, state.gridButton.h, state.gridButton.depth, 20.0f, 0, 0, false, state.gridButton.pressAnim};

        if (state.modeDrawButton.isToggled) {
            drawBeveledQuadTint(drawKey, 0.0f, 0.85f, 0.9f, 0.9f, g_vertices, screenWidth, screenHeight);
        } else {
            drawBeveledQuadTint(drawKey, 0.0f, 0.8f, 0.8f, 0.8f, g_vertices, screenWidth, screenHeight);
        }
        pushText(g_vertices, drawKey.x + 6.0f, drawKey.y + 18.0f, "Draw", glm::vec3(0.0f), screenWidth, screenHeight);

        if (state.modePaintButton.isToggled) {
            drawBeveledQuadTint(paintKey, 0.0f, 0.85f, 0.9f, 0.9f, g_vertices, screenWidth, screenHeight);
        } else {
            drawBeveledQuadTint(paintKey, 0.0f, 0.8f, 0.8f, 0.8f, g_vertices, screenWidth, screenHeight);
        }
        pushText(g_vertices, paintKey.x + 6.0f, paintKey.y + 18.0f, "Paint", glm::vec3(0.0f), screenWidth, screenHeight);

        if (state.scaleButton.isToggled) {
            drawBeveledQuadTint(scaleKey, 0.0f, 0.85f, 0.9f, 0.9f, g_vertices, screenWidth, screenHeight);
        } else {
            drawBeveledQuadTint(scaleKey, 0.0f, 0.8f, 0.8f, 0.8f, g_vertices, screenWidth, screenHeight);
        }
        pushText(g_vertices, scaleKey.x + 4.0f, scaleKey.y + 3.0f, "Scale", glm::vec3(0.0f), screenWidth, screenHeight);
        pushText(g_vertices, scaleKey.x + 4.0f, scaleKey.y + 18.0f, state.scaleButton.value.c_str(), glm::vec3(0.0f), screenWidth, screenHeight);

        if (state.gridButton.isToggled) {
            drawBeveledQuadTint(buttonKey, 0.0f, 0.85f, 0.9f, 0.9f, g_vertices, screenWidth, screenHeight);
        } else {
            drawBeveledQuadTint(buttonKey, 0.0f, 0.8f, 0.8f, 0.8f, g_vertices, screenWidth, screenHeight);
        }
        pushText(g_vertices, buttonKey.x + 4.0f, buttonKey.y + 3.0f, "Snap", glm::vec3(0.0f), screenWidth, screenHeight);
        pushMultilineText(g_vertices, buttonKey.x + 4.0f, buttonKey.y + 15.0f, PianoRollResourceSystemLogic::FormatButtonValue(state.gridButton.value), 10.0f, glm::vec3(0.0f), screenWidth, screenHeight);

        if (state.menuOpen) {
            float menuX = state.gridButton.x;
            float menuY = state.gridButton.y + state.gridButton.h + 6.0f;
            float menuW = 140.0f;
            float menuPadding = 6.0f;
            float menuRowHeight = 18.0f;
            const auto& snapOptions = PianoRollResourceSystemLogic::SnapOptions();
            float menuH = static_cast<float>(snapOptions.size()) * menuRowHeight + menuPadding * 2.0f;
            PianoRollResourceSystemLogic::Key menuKey{menuX, menuY, menuW, menuH, 6.0f, 20.0f, 0, 0, false, 0.0f};
            drawBeveledQuadTint(menuKey, 0.0f, 0.8f, 0.8f, 0.8f, g_vertices, screenWidth, screenHeight);
            if (state.hoverIndex >= 0) {
                float y0 = menuY + menuPadding + state.hoverIndex * menuRowHeight;
                float y1 = y0 + menuRowHeight;
                pushRect(g_vertices, menuX + 2.0f, y0, menuW - 4.0f, y1 - y0, glm::vec3(0.85f, 0.9f, 0.9f), screenWidth, screenHeight);
            }
            for (int i = 0; i < static_cast<int>(snapOptions.size()); ++i) {
                float textY = menuY + menuPadding + i * menuRowHeight + 3.0f;
                pushText(g_vertices, menuX + 8.0f, textY, snapOptions[i].c_str(), glm::vec3(0.0f), screenWidth, screenHeight);
            }
        }

        if (state.scaleMenuOpen) {
            float scaleMenuX = state.scaleButton.x;
            float scaleMenuY = state.scaleButton.y + state.scaleButton.h + 6.0f;
            float scaleMenuW = 240.0f;
            float scaleMenuH = 150.0f;
            float menuPadding = 6.0f;
            float menuRowHeight = 18.0f;
            float columnWidth = scaleMenuW / 3.0f;
            PianoRollResourceSystemLogic::Key menuKey{scaleMenuX, scaleMenuY, scaleMenuW, scaleMenuH, 6.0f, 20.0f, 0, 0, false, 0.0f};
            drawBeveledQuadTint(menuKey, 0.0f, 0.8f, 0.8f, 0.8f, g_vertices, screenWidth, screenHeight);

            if (state.hoverScaleColumn >= 0) {
                float hoverX = scaleMenuX + state.hoverScaleColumn * columnWidth;
                float hoverY = scaleMenuY + menuPadding + (1 + state.hoverScaleRow) * menuRowHeight;
                pushRect(g_vertices, hoverX + 2.0f, hoverY, columnWidth - 4.0f, menuRowHeight, glm::vec3(0.85f, 0.9f, 0.9f), screenWidth, screenHeight);
            }

            const auto& modeNames = PianoRollResourceSystemLogic::ModeNames();
            const auto& scaleNames = PianoRollResourceSystemLogic::ScaleNames();
            const auto& noteNamesLocal = PianoRollResourceSystemLogic::NoteNames();
            for (int i = 0; i < 7; ++i) {
                float rootX = scaleMenuX + 0.0f * columnWidth;
                float scaleX = scaleMenuX + 1.0f * columnWidth;
                float modeX = scaleMenuX + 2.0f * columnWidth;
                float textY = scaleMenuY + menuPadding + (i + 1) * menuRowHeight + 3.0f;
                float markOffset = 2.0f;
                if (state.scaleRoot == i && state.scaleType != PianoRollResourceSystemLogic::ScaleType::None) {
                    pushText(g_vertices, rootX + markOffset, textY, "x", glm::vec3(0.0f), screenWidth, screenHeight);
                }
                pushText(g_vertices, rootX + 12.0f, textY, noteNamesLocal[i], glm::vec3(0.0f), screenWidth, screenHeight);

                if ((i == 0 && state.scaleType == PianoRollResourceSystemLogic::ScaleType::None) ||
                    (i == 1 && state.scaleType == PianoRollResourceSystemLogic::ScaleType::Major) ||
                    (i == 2 && state.scaleType == PianoRollResourceSystemLogic::ScaleType::HarmonicMinor) ||
                    (i == 3 && state.scaleType == PianoRollResourceSystemLogic::ScaleType::MelodicMinor) ||
                    (i == 4 && state.scaleType == PianoRollResourceSystemLogic::ScaleType::HungarianMinor) ||
                    (i == 5 && state.scaleType == PianoRollResourceSystemLogic::ScaleType::NeapolitanMajor) ||
                    (i == 6 && state.scaleType == PianoRollResourceSystemLogic::ScaleType::DoubleHarmonicMinor)) {
                    pushText(g_vertices, scaleX + markOffset, textY, "x", glm::vec3(0.0f), screenWidth, screenHeight);
                }
                pushText(g_vertices, scaleX + 12.0f, textY, scaleNames[i], glm::vec3(0.0f), screenWidth, screenHeight);

                if (state.scaleMode == i && state.scaleType != PianoRollResourceSystemLogic::ScaleType::None) {
                    pushText(g_vertices, modeX + markOffset, textY, "x", glm::vec3(0.0f), screenWidth, screenHeight);
                }
                pushText(g_vertices, modeX + 12.0f, textY, modeNames[i], glm::vec3(0.0f), screenWidth, screenHeight);
            }
        }

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendColor(0.0f, 0.0f, 0.0f, 1.0f);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);

        glBindVertexArray(renderer.uiPianoRollVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.uiPianoRollVBO);
        glBufferData(GL_ARRAY_BUFFER, g_vertices.size() * sizeof(PianoRollResourceSystemLogic::UiVertex), g_vertices.data(), GL_DYNAMIC_DRAW);

        renderer.uiColorShader->use();
        renderer.uiColorShader->setFloat("alpha", 1.0f);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(PianoRollResourceSystemLogic::UiVertex), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PianoRollResourceSystemLogic::UiVertex), (void*)offsetof(PianoRollResourceSystemLogic::UiVertex, color));
        glEnableVertexAttribArray(1);

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(g_vertices.size()));
        glBindVertexArray(0);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
    }
}
