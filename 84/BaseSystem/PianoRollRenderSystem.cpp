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

#define STB_EASY_FONT_IMPLEMENTATION
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
            glm::vec2 b{x1 - hx, y0 - hy};
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
            glm::vec3 lightColor = baseColor * 1.2f;
            glm::vec3 darkColor = baseColor * 0.6f;
            glm::vec3 midColor = baseColor;

            float x = key.x;
            float y = key.y + offsetY;
            float w = key.w;
            float h = key.h;

            pushRect(verts, x, y, w, h, midColor, width, height);
            pushRect(verts, x, y, w, 2.0f, lightColor, width, height);
            pushRect(verts, x, y + h - 2.0f, w, 2.0f, darkColor, width, height);
        }

        void drawBeveledQuadTint(const PianoRollResourceSystemLogic::Key& key, float offsetY, float fr, float fg, float fb, std::vector<PianoRollResourceSystemLogic::UiVertex>& verts, double width, double height) {
            glm::vec3 baseColor(fr, fg, fb);
            drawBeveledQuad(key, offsetY, baseColor, verts, width, height);
        }
    }

    void UpdatePianoRollRender(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow* win) {
        PianoRollResourceSystemLogic::PianoRollState& state = PianoRollResourceSystemLogic::State();
        if (!state.active || !state.layoutReady) return;
        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.midi || !baseSystem.daw || !win) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        MidiContext& midi = *baseSystem.midi;
        DawContext& daw = *baseSystem.daw;

        if (!renderer.uiColorShader) return;

        int trackIndex = state.layout.trackIndex;
        int clipIndex = state.layout.clipIndex;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(midi.tracks.size())) return;
        if (clipIndex < 0 || clipIndex >= static_cast<int>(midi.tracks[trackIndex].clips.size())) return;
        MidiClip& clip = midi.tracks[trackIndex].clips[clipIndex];

        const auto& cfg = PianoRollResourceSystemLogic::Config();
        const auto& layout = state.layout;

        double screenWidth = layout.screenWidth;
        double screenHeight = layout.screenHeight;
        float gridLeft = layout.gridLeft;
        float gridRight = layout.gridRight;
        float viewTop = layout.viewTop;
        float viewBottom = layout.viewBottom;
        float gridOrigin = layout.gridOrigin;
        float gridStep = layout.gridStep;
        int totalRows = cfg.totalRows;
        double pxPerSample = layout.pxPerSample;
        double clipStartSample = layout.clipStartSample;

        float clipStartX = gridLeft + state.scrollOffsetX + static_cast<float>((clipStartSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
        float clipEndX = clipStartX + static_cast<float>(clip.length * pxPerSample);

        g_vertices.clear();
        g_vertices.reserve(4096);

        glm::vec3 backdrop(0.0f, 0.12f, 0.12f);
        pushRect(g_vertices, 0.0f, 0.0f, static_cast<float>(screenWidth), static_cast<float>(screenHeight), backdrop, screenWidth, screenHeight);

        int startRow = static_cast<int>(std::floor((gridOrigin - viewBottom) / gridStep));
        int endRow = static_cast<int>(std::ceil((gridOrigin - viewTop) / gridStep));
        if (startRow < 0) startRow = 0;
        if (endRow > totalRows - 1) endRow = totalRows - 1;

        glm::vec3 gridBlack(0.0f, 0.18f, 0.18f);
        glm::vec3 gridLine(0.0f, 0.22f, 0.22f);
        for (int row = startRow; row <= endRow; ++row) {
            float y0 = gridOrigin - (row + 1) * gridStep;
            float y1 = gridOrigin - row * gridStep;
            if (row % 2 == 0) {
                pushRect(g_vertices, gridLeft, y0, gridRight - gridLeft, y1 - y0, gridBlack, screenWidth, screenHeight);
            }
        }
        for (int row = startRow; row <= endRow; ++row) {
            float y = gridOrigin - row * gridStep;
            pushLine(g_vertices, gridLeft, y, gridRight, y, 1.0f, gridLine, screenWidth, screenHeight);
        }
        pushLine(g_vertices, gridLeft, viewTop, gridLeft, viewBottom, 1.0f, gridLine, screenWidth, screenHeight);

        float barStartPx = gridLeft + state.scrollOffsetX + static_cast<float>((0.0 - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
        double barSamples = layout.barSamples;
        double beatSamples = layout.beatSamples;
        float barStepPx = static_cast<float>(barSamples * pxPerSample);
        float beatStepPx = static_cast<float>(beatSamples * pxPerSample);
        int barCount = static_cast<int>(std::ceil((gridRight - gridLeft) / std::max(1.0f, barStepPx))) + 2;
        for (int i = 0; i < barCount; ++i) {
            float x = barStartPx + i * barStepPx;
            if (x < gridLeft - 2.0f || x > gridRight + 2.0f) continue;
            pushLine(g_vertices, x, viewTop, x, viewBottom, 1.0f, glm::vec3(0.1f, 0.25f, 0.25f), screenWidth, screenHeight);
        }
        int beatCount = static_cast<int>(std::ceil((gridRight - gridLeft) / std::max(1.0f, beatStepPx))) + 2;
        for (int i = 0; i < beatCount; ++i) {
            float x = barStartPx + i * beatStepPx;
            if (x < gridLeft - 2.0f || x > gridRight + 2.0f) continue;
            pushLine(g_vertices, x, viewTop, x, viewBottom, 1.0f, glm::vec3(0.0f, 0.22f, 0.22f), screenWidth, screenHeight);
        }
        if (layout.snapSamples > 0.0) {
            float snapStepPx = static_cast<float>(layout.snapSamples * pxPerSample);
            int snapCount = static_cast<int>(std::ceil((gridRight - gridLeft) / std::max(1.0f, snapStepPx))) + 2;
            for (int i = 0; i < snapCount; ++i) {
                float x = barStartPx + i * snapStepPx;
                if (x < gridLeft - 2.0f || x > gridRight + 2.0f) continue;
                pushLine(g_vertices, x, viewTop, x, viewBottom, 1.0f, glm::vec3(0.0f, 0.18f, 0.18f), screenWidth, screenHeight);
            }
        }

        const auto& noteNames = PianoRollResourceSystemLogic::NoteNames();
        for (int row = startRow; row <= endRow; ++row) {
            if (row < 0 || row >= totalRows) continue;
            int noteIndex = (row + 24) % 12;
            if (noteIndex < 0) noteIndex += 12;
            int octave = (row + 24) / 12;
            char label[16];
            std::snprintf(label, sizeof(label), "%s%d", noteNames[noteIndex], octave);
            float y1 = gridOrigin - row * gridStep;
            float labelY = y1 - 4.0f;
            pushText(g_vertices, gridLeft + 6.0f, labelY, label, glm::vec3(0.0f, 0.2f, 0.2f), screenWidth, screenHeight);
        }

        float noteStartX = gridLeft + state.scrollOffsetX + static_cast<float>((clipStartSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
        for (const auto& note : clip.notes) {
            int row = note.pitch - 24;
            if (row < startRow || row > endRow) continue;
            float nx = noteStartX + static_cast<float>(note.startSample * pxPerSample);
            float nw = std::max(2.0f, static_cast<float>(note.length * pxPerSample));
            float ny = gridOrigin - (row + 1) * gridStep;
            int noteIndex = note.pitch % 12;
            if (noteIndex < 0) noteIndex += 12;
            float nr = 0.75f, ng = 0.85f, nb = 0.9f;
            PianoRollResourceSystemLogic::NoteColor(noteIndex, nr, ng, nb);
            PianoRollResourceSystemLogic::Key noteKey{nx, ny, nw, gridStep, 6.0f, 20.0f, 0, 0, false, 0.0f};
            drawBeveledQuadTint(noteKey, 0.0f, nr, ng, nb, g_vertices, screenWidth, screenHeight);
            char label[16];
            int octave = note.pitch / 12;
            std::snprintf(label, sizeof(label), "%s%d", noteNames[noteIndex], octave);
            pushText(g_vertices, nx + 4.0f, ny + gridStep - 12.0f, label, glm::vec3(0.1f, 0.2f, 0.25f), screenWidth, screenHeight);
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
            float labelX = key.x + 8.0f;
            float labelY = y + key.h - 12.0f;
            pushText(g_vertices, labelX, labelY, label, glm::vec3(0.0f), screenWidth, screenHeight);
        }
        for (const auto& key : state.blackKeys) {
            float y = key.y + state.scrollOffsetY;
            char label[16];
            std::snprintf(label, sizeof(label), "%s%d", noteNames[key.note], key.octave);
            float labelX = key.x + 8.0f;
            float labelY = y + key.h - 12.0f;
            pushText(g_vertices, labelX, labelY, label, glm::vec3(0.9f), screenWidth, screenHeight);
        }

        if (clipStartX > gridLeft) {
            pushRect(g_vertices, gridLeft, viewTop, clipStartX - gridLeft, viewBottom - viewTop, glm::vec3(0.0f, 0.15f, 0.15f), screenWidth, screenHeight);
        }
        if (clipEndX < gridRight) {
            pushRect(g_vertices, clipEndX, viewTop, gridRight - clipEndX, viewBottom - viewTop, glm::vec3(0.0f, 0.15f, 0.15f), screenWidth, screenHeight);
        }

        pushRect(g_vertices, 0.0f, 0.0f, static_cast<float>(screenWidth), cfg.borderHeight, glm::vec3(0.0f, 0.188f, 0.188f), screenWidth, screenHeight);
        pushRect(g_vertices, 0.0f, static_cast<float>(screenHeight) - cfg.borderHeight, static_cast<float>(screenWidth), cfg.borderHeight, glm::vec3(0.0f, 0.188f, 0.188f), screenWidth, screenHeight);
        pushRect(g_vertices, 0.0f, cfg.borderHeight, cfg.leftBorderWidth, static_cast<float>(screenHeight) - 2.0f * cfg.borderHeight, glm::vec3(0.0f, 0.188f, 0.188f), screenWidth, screenHeight);

        if (state.gridButton.isPressed || state.menuOpen) {
            pushRect(g_vertices, state.gridButton.x, state.gridButton.y, state.gridButton.w, state.gridButton.h, glm::vec3(0.0f, 0.2f, 0.2f), screenWidth, screenHeight);
        }
        if (state.scaleButton.isPressed || state.scaleMenuOpen) {
            pushRect(g_vertices, state.scaleButton.x, state.scaleButton.y, state.scaleButton.w, state.scaleButton.h, glm::vec3(0.0f, 0.2f, 0.2f), screenWidth, screenHeight);
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
