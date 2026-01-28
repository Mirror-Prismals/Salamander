#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#define STB_EASY_FONT_IMPLEMENTATION
#include "stb_easy_font.h"
#include "stb_image.h"

namespace PianoRollSystemLogic {
    namespace {
        struct UiVertex {
            glm::vec2 pos;
            glm::vec3 color;
        };

        struct Key {
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            float depth = 0.0f;
            float z = 0.0f;
            int note = 0;
            int octave = 0;
            bool isPressed = false;
            float pressAnim = 0.0f;
        };

        struct ToggleButton {
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            float depth = 0.0f;
            bool isPressed = false;
            bool isToggled = false;
            float pressAnim = 0.0f;
            std::string value = "none";
        };

        enum class EditMode {
            Draw,
            Paint
        };

        enum class ScaleType {
            None,
            Major,
            HarmonicMinor,
            MelodicMinor,
            HungarianMinor,
            NeapolitanMajor,
            DoubleHarmonicMinor
        };

        struct DeleteAnim {
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;
            double startTime = 0.0;
        };

        struct PianoRollState {
            bool initialized = false;
            bool cursorsLoaded = false;
            GLFWcursor* cursorDefault = nullptr;
            GLFWcursor* cursorDraw = nullptr;
            GLFWcursor* cursorBrush = nullptr;
            GLFWcursor* cursorMove = nullptr;
            GLFWcursor* cursorResize = nullptr;
            GLFWcursor* currentCursor = nullptr;

            float scrollOffsetY = 0.0f;
            float scrollOffsetX = 0.0f;

            std::vector<Key> whiteKeys;
            std::vector<Key> blackKeys;

            ToggleButton modeDrawButton;
            ToggleButton modePaintButton;
            ToggleButton gridButton;
            ToggleButton scaleButton;

            std::vector<DeleteAnim> deleteAnims;
            std::vector<double> paintLastX;
            double paintLastXGlobal = -1.0;
            int paintLastRow = -1;
            double paintLastCursorX = -1.0;
            int paintDir = 0;

            bool menuOpen = false;
            bool scaleMenuOpen = false;
            int hoverIndex = -1;
            int hoverScaleColumn = -1;
            int hoverScaleRow = -1;
            bool wasMouseDown = false;
            bool wasRightDown = false;
            bool wasPDown = false;
            bool wasBDown = false;

            int activeNote = -1;
            bool resizingNote = false;
            double dragOffsetSamples = 0.0;
            double lastNoteLengthSamples = 0.0;
            EditMode editMode = EditMode::Draw;
            bool painting = false;
            int scaleRoot = 0;
            ScaleType scaleType = ScaleType::None;
            int scaleMode = 0;

            double lastTime = 0.0;
            int cachedTrack = -1;
            int cachedClip = -1;
        };

        static PianoRollState g_state;
        static std::vector<UiVertex> g_vertices;

        constexpr float kBorderHeight = 60.0f;
        constexpr float kLeftBorderWidth = 12.5f;
        constexpr float kKeyWidth = 187.5f;
        constexpr float kKeyDepth = 7.5f;
        constexpr float kBlackKeyWidth = 125.0f;
        constexpr float kBlackKeyHeight = 30.0f;
        constexpr float kBlackKeyDepth = 7.5f;
        constexpr float kLaneStep = 30.0f;
        constexpr int kOctaveCount = 7;
        constexpr int kKeyCount = 7;
        constexpr int kTotalRows = kOctaveCount * 12;
        constexpr float kPressDuration = 0.12f;
        constexpr float kNoteHandleSize = 8.0f;

        glm::vec2 pixelToNDC(const glm::vec2& pixel, double width, double height) {
            float ndcX = static_cast<float>((pixel.x / width) * 2.0 - 1.0);
            float ndcY = static_cast<float>(1.0 - (pixel.y / height) * 2.0);
            return {ndcX, ndcY};
        }

        void pushQuad(std::vector<UiVertex>& verts,
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

        void pushRect(std::vector<UiVertex>& verts, float x, float y, float w, float h, const glm::vec3& color, double width, double height) {
            pushQuad(verts, {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, color, width, height);
        }

        void pushLine(std::vector<UiVertex>& verts, float x0, float y0, float x1, float y1, float thickness, const glm::vec3& color, double width, double height) {
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

        void pushRectOutline(std::vector<UiVertex>& verts, float x, float y, float w, float h, float thickness, const glm::vec3& color, double width, double height) {
            pushLine(verts, x, y, x + w, y, thickness, color, width, height);
            pushLine(verts, x + w, y, x + w, y + h, thickness, color, width, height);
            pushLine(verts, x + w, y + h, x, y + h, thickness, color, width, height);
            pushLine(verts, x, y + h, x, y, thickness, color, width, height);
        }

        void pushText(std::vector<UiVertex>& verts, float x, float y, const char* text, const glm::vec3& color, double width, double height) {
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

        void pushMultilineText(std::vector<UiVertex>& verts, float x, float y, const std::string& text, float lineHeight, const glm::vec3& color, double width, double height) {
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

        float clamp01(float v) {
            return std::clamp(v, 0.0f, 1.0f);
        }

        void noteColor(int noteIndex, float& r, float& g, float& b) {
            switch (noteIndex) {
                case 0:  r = 0.467f; g = 0.667f; b = 0.855f; break;
                case 1:  r = 0.129f; g = 0.596f; b = 0.647f; break;
                case 2:  r = 0.463f; g = 0.667f; b = 0.298f; break;
                case 3:  r = 0.518f; g = 0.620f; b = 0.020f; break;
                case 4:  r = 0.992f; g = 0.922f; b = 0.008f; break;
                case 5:  r = 0.992f; g = 0.596f; b = 0.016f; break;
                case 6:  r = 0.992f; g = 0.565f; b = 0.153f; break;
                case 7:  r = 0.988f; g = 0.467f; b = 0.067f; break;
                case 8:  r = 0.988f; g = 0.416f; b = 0.400f; break;
                case 9:  r = 0.933f; g = 0.502f; b = 0.643f; break;
                case 10: r = 0.510f; g = 0.282f; b = 0.694f; break;
                case 11: r = 0.376f; g = 0.384f; b = 0.702f; break;
                default: r = 0.75f; g = 0.85f; b = 0.9f; break;
            }
        }

        double snapValue(double value, double snap) {
            if (snap <= 0.0) return value;
            return std::round(value / snap) * snap;
        }

        double snapFloor(double value, double snap) {
            if (snap <= 0.0) return value;
            return std::floor(value / snap) * snap;
        }

        void getScaleIntervals(ScaleType type, int (&intervals)[7]) {
            if (type == ScaleType::Major) {
                int tmp[7] = { 0, 2, 4, 5, 7, 9, 11 };
                std::copy(tmp, tmp + 7, intervals);
            } else if (type == ScaleType::HarmonicMinor) {
                int tmp[7] = { 0, 2, 3, 5, 7, 8, 11 };
                std::copy(tmp, tmp + 7, intervals);
            } else if (type == ScaleType::MelodicMinor) {
                int tmp[7] = { 0, 2, 3, 5, 7, 9, 11 };
                std::copy(tmp, tmp + 7, intervals);
            } else if (type == ScaleType::HungarianMinor) {
                int tmp[7] = { 0, 2, 3, 6, 7, 8, 11 };
                std::copy(tmp, tmp + 7, intervals);
            } else if (type == ScaleType::NeapolitanMajor) {
                int tmp[7] = { 0, 1, 3, 5, 7, 9, 11 };
                std::copy(tmp, tmp + 7, intervals);
            } else if (type == ScaleType::DoubleHarmonicMinor) {
                int tmp[7] = { 0, 1, 4, 5, 7, 8, 11 };
                std::copy(tmp, tmp + 7, intervals);
            } else {
                int tmp[7] = { 0, 2, 4, 5, 7, 9, 11 };
                std::copy(tmp, tmp + 7, intervals);
            }
        }

        bool isScaleNote(int noteIndex, int root, ScaleType type, int mode) {
            if (type == ScaleType::None) return true;
            int intervals[7];
            getScaleIntervals(type, intervals);
            int rotated[7];
            for (int i = 0; i < 7; ++i) {
                rotated[i] = (intervals[(i + mode) % 7] - intervals[mode] + 12) % 12;
            }
            int rel = (noteIndex - root + 12) % 12;
            for (int i = 0; i < 7; ++i) {
                if (rel == rotated[i]) return true;
            }
            return false;
        }

        int adjustRowToScale(int row, float mouseY, float gridOrigin, float laneStep, int totalRows, int root, ScaleType type, int mode) {
            if (type == ScaleType::None) return row;
            int noteIndex = row % 12;
            if (noteIndex < 0) noteIndex += 12;
            if (isScaleNote(noteIndex, root, type, mode)) return row;

            float rowTop = gridOrigin - row * laneStep;
            float rowBottom = rowTop - laneStep;
            float mid = (rowTop + rowBottom) * 0.5f;
            bool goUp = mouseY < mid;

            if (goUp) {
                for (int r = row + 1; r < totalRows; ++r) {
                    int idx = r % 12;
                    if (idx < 0) idx += 12;
                    if (isScaleNote(idx, root, type, mode)) return r;
                }
            } else {
                for (int r = row - 1; r >= 0; --r) {
                    int idx = r % 12;
                    if (idx < 0) idx += 12;
                    if (isScaleNote(idx, root, type, mode)) return r;
                }
            }
            return row;
        }

        int findNoteAtStart(const std::vector<MidiNote>& notes, int pitch, double startSample, int excludeIndex) {
            for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
                if (i == excludeIndex) continue;
                if (notes[i].pitch != pitch) continue;
                if (std::fabs(static_cast<double>(notes[i].startSample) - startSample) < 0.5) {
                    return i;
                }
            }
            return -1;
        }

        int findOverlappingNote(const std::vector<MidiNote>& notes, int pitch, double startSample, int excludeIndex) {
            for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
                if (i == excludeIndex) continue;
                if (notes[i].pitch != pitch) continue;
                double start = static_cast<double>(notes[i].startSample);
                double end = start + static_cast<double>(notes[i].length);
                if (startSample > start && startSample < end) {
                    return i;
                }
            }
            return -1;
        }

        double getNextNoteStart(const std::vector<MidiNote>& notes, int pitch, double startSample, int excludeIndex) {
            double nextStart = -1.0;
            for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
                if (i == excludeIndex) continue;
                if (notes[i].pitch != pitch) continue;
                double x = static_cast<double>(notes[i].startSample);
                if (x > startSample) {
                    if (nextStart < 0.0 || x < nextStart) nextStart = x;
                }
            }
            return nextStart;
        }

        bool placeNote(std::vector<MidiNote>& notes,
                       int pitch,
                       double& startSample,
                       double& noteLen,
                       double snapStep,
                       double minNoteLen,
                       bool allowShiftForward = true) {
            for (int guard = 0; guard < static_cast<int>(notes.size()); ++guard) {
                int sameStart = findNoteAtStart(notes, pitch, startSample, -1);
                if (sameStart < 0) break;
                if (!allowShiftForward) return false;
                startSample = snapStep > 0.0 ? snapFloor(static_cast<double>(notes[sameStart].startSample + notes[sameStart].length), snapStep)
                                             : static_cast<double>(notes[sameStart].startSample + notes[sameStart].length);
            }

            int overlap = findOverlappingNote(notes, pitch, startSample, -1);
            if (overlap >= 0) {
                if (!allowShiftForward) return false;
                double newLen = startSample - static_cast<double>(notes[overlap].startSample);
                if (newLen < minNoteLen) {
                    newLen = minNoteLen;
                    startSample = static_cast<double>(notes[overlap].startSample) + newLen;
                }
                notes[overlap].length = static_cast<uint64_t>(std::round(newLen));
            }

            double nextStart = getNextNoteStart(notes, pitch, startSample, -1);
            if (nextStart >= 0.0 && startSample + noteLen > nextStart) {
                if (!allowShiftForward) return false;
                noteLen = nextStart - startSample;
            }
            if (noteLen < minNoteLen) return false;

            MidiNote note;
            note.pitch = pitch;
            note.startSample = static_cast<uint64_t>(std::round(startSample));
            note.length = static_cast<uint64_t>(std::round(noteLen));
            note.velocity = 1.0f;
            notes.push_back(note);
            return true;
        }

        bool cursorInRect(const UIContext& ui, float left, float right, float top, float bottom) {
            float x = static_cast<float>(ui.cursorX);
            float y = static_cast<float>(ui.cursorY);
            return x >= left && x <= right && y >= top && y <= bottom;
        }

        GLFWcursor* loadCursorImage(const char* path, int hotX, int hotY) {
            int width = 0;
            int height = 0;
            int channels = 0;
            unsigned char* data = stbi_load(path, &width, &height, &channels, 4);
            if (!data) {
                std::cerr << "PianoRoll: Failed to load cursor: " << path << "\n";
                return nullptr;
            }
            GLFWimage image;
            image.width = width;
            image.height = height;
            image.pixels = data;
            GLFWcursor* cursor = glfwCreateCursor(&image, hotX, hotY);
            stbi_image_free(data);
            return cursor;
        }

        void ensureResources(RendererContext& renderer, WorldContext& world) {
            if (!renderer.uiColorShader) {
                renderer.uiColorShader = std::make_unique<Shader>(world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(),
                                                                 world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.uiPianoRollVAO == 0) {
                glGenVertexArrays(1, &renderer.uiPianoRollVAO);
                glGenBuffers(1, &renderer.uiPianoRollVBO);
            }
        }

        void resetStateForClip(PianoRollState& state) {
            state.scrollOffsetX = 0.0f;
            state.scrollOffsetY = 0.0f;
            state.activeNote = -1;
            state.resizingNote = false;
            state.dragOffsetSamples = 0.0;
            state.lastNoteLengthSamples = 0.0;
            state.editMode = EditMode::Draw;
            state.painting = false;
            state.paintLastX.assign(kTotalRows, -1.0);
            state.paintLastXGlobal = -1.0;
            state.paintLastRow = -1;
            state.paintLastCursorX = -1.0;
            state.paintDir = 0;
            state.menuOpen = false;
            state.scaleMenuOpen = false;
            state.hoverIndex = -1;
            state.hoverScaleColumn = -1;
            state.hoverScaleRow = -1;
            state.wasMouseDown = false;
            state.wasRightDown = false;
            state.wasPDown = false;
            state.wasBDown = false;
            state.scaleRoot = 0;
            state.scaleType = ScaleType::None;
            state.scaleMode = 0;
            state.deleteAnims.clear();
            state.lastTime = glfwGetTime();
        }

        void buildKeyLayout(PianoRollState& state, double screenWidth, double screenHeight) {
            const int whiteNotes[7] = { 0, 2, 4, 5, 7, 9, 11 };
            const int whiteLaneIndex[7] = { 0, 2, 4, 5, 7, 9, 11 };
            const float whiteHeights[7] = { 60.0f, 60.0f, 30.0f, 60.0f, 60.0f, 60.0f, 30.0f };
            const int blackNotesByGap[6] = { 1, 3, -1, 6, 8, 10 };
            const int blackAnchorWhiteNote[6] = { 0, 2, -1, 5, 7, 9 };
            const bool blackAlignTop[6] = { true, true, true, true, true, true };

            float laneSpan = kLaneStep * 12.0f;
            float totalHeight = laneSpan * kOctaveCount;
            float topEdge = static_cast<float>((screenHeight - totalHeight) * 0.5f);
            float bottomEdge = topEdge + totalHeight;
            float baseOriginY = bottomEdge;

            state.whiteKeys.clear();
            state.blackKeys.clear();
            state.whiteKeys.reserve(kKeyCount * kOctaveCount);
            state.blackKeys.reserve((kKeyCount - 1) * kOctaveCount);

            for (int octaveIndex = 0; octaveIndex < kOctaveCount; ++octaveIndex) {
                float octaveOriginY = baseOriginY - octaveIndex * laneSpan;
                for (int i = 0; i < kKeyCount; ++i) {
                    Key key;
                    key.w = kKeyWidth;
                    key.h = whiteHeights[i];
                    key.depth = kKeyDepth;
                    key.x = kLeftBorderWidth;
                    float bottomY = octaveOriginY - whiteLaneIndex[i] * kLaneStep;
                    key.y = bottomY - key.h;
                    key.note = whiteNotes[i];
                    key.octave = octaveIndex + 1;
                    state.whiteKeys.push_back(key);
                }
            }

            float blackX = kLeftBorderWidth + 6.25f;
            int gapCount = kKeyCount - 1;
            for (int octaveIndex = 0; octaveIndex < kOctaveCount; ++octaveIndex) {
                float octaveOriginY = baseOriginY - octaveIndex * laneSpan;
                for (int gapIndex = 0; gapIndex < gapCount; ++gapIndex) {
                    if (gapIndex == 2) continue;
                    Key key;
                    key.w = kBlackKeyWidth;
                    key.h = kBlackKeyHeight;
                    key.depth = kBlackKeyDepth;
                    key.z = 15.0f;
                    key.x = blackX;
                    key.note = blackNotesByGap[gapIndex];
                    key.octave = octaveIndex + 1;
                    int anchorNote = blackAnchorWhiteNote[gapIndex];
                    float anchorBottomY = octaveOriginY - anchorNote * kLaneStep;
                    float anchorTopY = anchorBottomY - 60.0f;
                    float blackTopY = blackAlignTop[gapIndex] ? anchorTopY : (anchorTopY + 30.0f);
                    key.y = blackTopY;
                    state.blackKeys.push_back(key);
                }
            }
        }

        void drawBeveledQuad(const Key& key, float offsetY, const glm::vec3& baseColor, std::vector<UiVertex>& verts, double width, double height) {
            float pressAnim = key.pressAnim;
            float shiftRight = 4.0f * pressAnim;
            float pressOffsetZ = key.depth * pressAnim;
            float newDepth = key.depth * (1.0f - 0.5f * pressAnim);
            float x = key.x + shiftRight;
            float y = key.y + offsetY;
            float w = key.w;
            float h = key.h;

            glm::vec3 front = baseColor;
            glm::vec3 top = glm::clamp(baseColor + glm::vec3(0.1f), glm::vec3(0.0f), glm::vec3(1.0f));
            glm::vec3 right = glm::clamp(baseColor - glm::vec3(0.2f), glm::vec3(0.0f), glm::vec3(1.0f));
            glm::vec3 bottom = baseColor;
            glm::vec3 left = glm::clamp(baseColor - glm::vec3(0.15f), glm::vec3(0.0f), glm::vec3(1.0f));

            float depth = newDepth;
            pushRect(verts, x, y, w, h, front, width, height);
            pushRect(verts, x, y - depth, w + depth, depth, top, width, height);
            pushRect(verts, x + w, y - depth, depth, h + depth, right, width, height);
            pushRect(verts, x, y + h, w + depth, depth, bottom, width, height);
            pushRect(verts, x - depth, y, depth, h + depth, left, width, height);
        }

        void drawBeveledQuadTint(const Key& key, float offsetY, float fr, float fg, float fb, std::vector<UiVertex>& verts, double width, double height) {
            glm::vec3 base(fr, fg, fb);
            drawBeveledQuad(key, offsetY, base, verts, width, height);
        }

        std::string formatButtonValue(const std::string& value) {
            std::string out = value;
            size_t pos = out.find(" step");
            if (pos != std::string::npos) {
                out.replace(pos, 5, "\nstep");
                return out;
            }
            pos = out.find(" beat");
            if (pos != std::string::npos) {
                out.replace(pos, 5, "\nbeat");
            }
            return out;
        }

        double getSnapSpacingSamples(const std::string& value, double beatSamples, double barSamples) {
            if (value == "none") return 0.0;
            if (value == "bar") return barSamples;
            if (value == "beat") return beatSamples;
            if (value == "1/2 beat") return beatSamples * 0.5;
            if (value == "1/3 beat") return beatSamples / 3.0;
            if (value == "1/4 beat") return beatSamples * 0.25;
            if (value == "1/6 beat") return beatSamples / 6.0;

            double stepSamples = beatSamples * 0.25;
            if (value == "step") return stepSamples;
            if (value == "1/2 step") return stepSamples * 0.5;
            if (value == "1/3 step") return stepSamples / 3.0;
            if (value == "1/4 step") return stepSamples * 0.25;
            if (value == "1/6 step") return stepSamples / 6.0;
            return 0.0;
        }

        bool isInsideRect(float x, float y, float w, float h, float mx, float my) {
            return mx >= x && mx <= x + w && my >= y && my <= y + h;
        }
    }

    void UpdatePianoRoll(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt;
        if (!baseSystem.ui || !baseSystem.renderer || !baseSystem.world || !baseSystem.midi || !baseSystem.daw || !win) return;
        UIContext& ui = *baseSystem.ui;
        MidiContext& midi = *baseSystem.midi;
        DawContext& daw = *baseSystem.daw;
        if (!ui.active || ui.loadingActive) return;
        if (!midi.pianoRollActive) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureResources(renderer, world);
        if (!renderer.uiColorShader) return;

        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowSize(win, &windowWidth, &windowHeight);
        double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
        double screenHeight = windowHeight > 0 ? static_cast<double>(windowHeight) : 1080.0;

        float closeSize = 32.0f;
        float closePad = 18.0f;
        float closeLeft = static_cast<float>(screenWidth) - closePad - closeSize;
        float closeTop = closePad;

        if (ui.uiLeftReleased) {
            if (cursorInRect(ui, closeLeft, closeLeft + closeSize, closeTop, closeTop + closeSize)) {
                midi.pianoRollActive = false;
                midi.pianoRollTrack = -1;
                midi.pianoRollClipIndex = -1;
                if (g_state.cursorDefault) {
                    glfwSetCursor(win, g_state.cursorDefault);
                    g_state.currentCursor = g_state.cursorDefault;
                }
                ui.consumeClick = true;
                return;
            }
        }

        if (!g_state.cursorsLoaded) {
            g_state.cursorDefault = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
            g_state.cursorDraw = loadCursorImage("Procedures/assets/drawing_pencil.png", 0, 0);
            g_state.cursorBrush = loadCursorImage("Procedures/assets/drawing_brush.png", 0, 0);
            g_state.cursorMove = loadCursorImage("Procedures/assets/resize_a_cross.png", 16, 16);
            g_state.cursorResize = loadCursorImage("Procedures/assets/resize_a_horizontal.png", 16, 16);
            g_state.cursorsLoaded = true;
        }

        int trackIndex = midi.pianoRollTrack;
        int clipIndex = midi.pianoRollClipIndex;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(midi.tracks.size())) return;
        if (clipIndex < 0 || clipIndex >= static_cast<int>(midi.tracks[trackIndex].clips.size())) return;
        MidiClip& clip = midi.tracks[trackIndex].clips[clipIndex];

        // Lightweight runtime sanity check to confirm the clip payload.
        static int s_loggedTrack = -1;
        static int s_loggedClip = -1;
        if (s_loggedTrack != trackIndex || s_loggedClip != clipIndex) {
            s_loggedTrack = trackIndex;
            s_loggedClip = clipIndex;
            std::cerr << "PianoRoll open: track=" << trackIndex
                      << " clip=" << clipIndex
                      << " start=" << clip.startSample
                      << " len=" << clip.length
                      << " notes=" << clip.notes.size() << "\n";
        }

        if (g_state.cachedTrack != trackIndex || g_state.cachedClip != clipIndex || !g_state.initialized) {
            g_state.cachedTrack = trackIndex;
            g_state.cachedClip = clipIndex;
            resetStateForClip(g_state);
            g_state.initialized = true;
        }

        buildKeyLayout(g_state, screenWidth, screenHeight);

        g_state.modeDrawButton.w = 44.0f;
        g_state.modeDrawButton.h = 36.0f;
        g_state.modeDrawButton.depth = 6.0f;
        g_state.modeDrawButton.x = 8.0f;
        g_state.modeDrawButton.y = 15.0f;
        g_state.modeDrawButton.value = "Draw";

        g_state.modePaintButton.w = 44.0f;
        g_state.modePaintButton.h = 36.0f;
        g_state.modePaintButton.depth = 6.0f;
        g_state.modePaintButton.x = g_state.modeDrawButton.x + g_state.modeDrawButton.w + 6.0f;
        g_state.modePaintButton.y = 15.0f;
        g_state.modePaintButton.value = "Paint";

        g_state.gridButton.w = 36.0f;
        g_state.gridButton.h = 36.0f;
        g_state.gridButton.depth = 6.0f;
        g_state.gridButton.x = g_state.modePaintButton.x + g_state.modePaintButton.w + 6.0f;
        g_state.gridButton.y = 15.0f;
        if (g_state.gridButton.value.empty()) g_state.gridButton.value = "beat";

        g_state.scaleButton.w = 48.0f;
        g_state.scaleButton.h = 36.0f;
        g_state.scaleButton.depth = 6.0f;
        g_state.scaleButton.x = g_state.gridButton.x + g_state.gridButton.w + 6.0f;
        g_state.scaleButton.y = 15.0f;
        if (g_state.scaleButton.value.empty()) g_state.scaleButton.value = "none";

        float viewTop = kBorderHeight;
        float viewBottom = static_cast<float>(screenHeight) - kBorderHeight;

        float contentTop = 1e9f;
        float contentBottom = -1e9f;
        for (const auto& key : g_state.whiteKeys) {
            contentTop = std::min(contentTop, key.y);
            contentBottom = std::max(contentBottom, key.y + key.h);
        }
        for (const auto& key : g_state.blackKeys) {
            contentTop = std::min(contentTop, key.y);
            contentBottom = std::max(contentBottom, key.y + key.h);
        }

        float maxScrollY = viewTop - contentTop;
        float minScrollY = viewBottom - contentBottom;
        if (maxScrollY < minScrollY) {
            float centerScroll = 0.5f * (maxScrollY + minScrollY);
            maxScrollY = centerScroll;
            minScrollY = centerScroll;
        }

        bool shiftDown = win ? (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) : false;
        if (ui.mainScrollDelta != 0.0) {
            if (shiftDown) {
                g_state.scrollOffsetX -= static_cast<float>(ui.mainScrollDelta * 40.0);
            } else {
                g_state.scrollOffsetY += static_cast<float>(ui.mainScrollDelta * 30.0);
            }
            ui.mainScrollDelta = 0.0;
        }

        g_state.scrollOffsetY = std::clamp(g_state.scrollOffsetY, minScrollY, maxScrollY);

        double bpm = daw.bpm.load(std::memory_order_relaxed);
        if (bpm <= 0.0) bpm = 120.0;
        double secondsPerBeat = 60.0 / bpm;
        double beatSamples = secondsPerBeat * daw.sampleRate;
        double barSamples = beatSamples * 4.0;

        float gridLeft = kLeftBorderWidth + kKeyWidth;
        float gridRight = static_cast<float>(screenWidth);
        float gridWidth = gridRight - gridLeft;
        double samplesPerScreen = daw.timelineSecondsPerScreen * daw.sampleRate;
        if (samplesPerScreen <= 0.0) samplesPerScreen = 1.0;
        double pxPerSample = gridWidth / samplesPerScreen;

        double snapSamples = getSnapSpacingSamples(g_state.gridButton.value, beatSamples, barSamples);
        double snapWidthDisplay = snapSamples * pxPerSample;
        double defaultStepSamples = beatSamples * 0.25;
        double minNoteLenSamples = std::max(1.0, snapSamples > 0.0 ? snapSamples : defaultStepSamples);
        double minSnapLenSamples = std::max(1.0, snapSamples > 0.0 ? snapSamples : defaultStepSamples);

        double clipStartSample = static_cast<double>(clip.startSample);
        double clipEndSample = static_cast<double>(clip.startSample + clip.length);

        float clipStartX = gridLeft + g_state.scrollOffsetX + static_cast<float>((clipStartSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
        float clipEndX = clipStartX + static_cast<float>(clip.length * pxPerSample);

        float maxScrollX = gridLeft - (gridLeft + static_cast<float>((clipStartSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample));
        float minScrollX = gridRight - (gridLeft + static_cast<float>((clipEndSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample));
        if (maxScrollX < minScrollX) {
            float center = 0.5f * (maxScrollX + minScrollX);
            maxScrollX = center;
            minScrollX = center;
        }
        g_state.scrollOffsetX = std::clamp(g_state.scrollOffsetX, minScrollX, maxScrollX);

        clipStartX = gridLeft + g_state.scrollOffsetX + static_cast<float>((clipStartSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
        clipEndX = clipStartX + static_cast<float>(clip.length * pxPerSample);

        float mouseX = static_cast<float>(ui.cursorX);
        float mouseY = static_cast<float>(ui.cursorY);
        int mouseDown = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT);
        bool mousePressedThisFrame = (mouseDown == GLFW_PRESS) && !g_state.wasMouseDown;
        bool mouseReleasedThisFrame = (mouseDown == GLFW_RELEASE) && g_state.wasMouseDown;
        int rightDown = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT);

        bool buttonHit = isInsideRect(g_state.gridButton.x, g_state.gridButton.y, g_state.gridButton.w, g_state.gridButton.h, mouseX, mouseY);
        bool scaleButtonHit = isInsideRect(g_state.scaleButton.x, g_state.scaleButton.y, g_state.scaleButton.w, g_state.scaleButton.h, mouseX, mouseY);
        bool drawButtonHit = isInsideRect(g_state.modeDrawButton.x, g_state.modeDrawButton.y, g_state.modeDrawButton.w, g_state.modeDrawButton.h, mouseX, mouseY);
        bool paintButtonHit = isInsideRect(g_state.modePaintButton.x, g_state.modePaintButton.y, g_state.modePaintButton.w, g_state.modePaintButton.h, mouseX, mouseY);

        int pDown = glfwGetKey(win, GLFW_KEY_P);
        int bDown = glfwGetKey(win, GLFW_KEY_B);
        if (pDown == GLFW_PRESS && !g_state.wasPDown) {
            g_state.editMode = EditMode::Draw;
        }
        if (bDown == GLFW_PRESS && !g_state.wasBDown) {
            g_state.editMode = EditMode::Paint;
        }
        g_state.wasPDown = (pDown == GLFW_PRESS);
        g_state.wasBDown = (bDown == GLFW_PRESS);

        g_state.modeDrawButton.isToggled = (g_state.editMode == EditMode::Draw);
        g_state.modePaintButton.isToggled = (g_state.editMode == EditMode::Paint);

        g_state.scaleButton.isToggled = (g_state.scaleType != ScaleType::None);
        {
            const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const char* modeNames[7] = { "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian" };
            if (g_state.scaleType == ScaleType::None) {
                g_state.scaleButton.value = "none";
            } else {
                std::string name = noteNames[g_state.scaleRoot];
                name += " ";
                name += modeNames[g_state.scaleMode % 7];
                if (g_state.scaleType == ScaleType::Major) {
                    name += " Maj";
                } else if (g_state.scaleType == ScaleType::HarmonicMinor) {
                    name += " HMin";
                } else if (g_state.scaleType == ScaleType::MelodicMinor) {
                    name += " MMin";
                } else if (g_state.scaleType == ScaleType::HungarianMinor) {
                    name += " HngMin";
                } else if (g_state.scaleType == ScaleType::NeapolitanMajor) {
                    name += " NeoMaj";
                } else {
                    name += " DHMin";
                }
                g_state.scaleButton.value = name;
            }
        }

        float menuX = g_state.gridButton.x;
        float menuY = kBorderHeight + 6.0f;
        float menuW = 160.0f;
        const float menuRowHeight = 26.0f;
        const float menuPadding = 6.0f;
        const std::vector<std::string> snapOptions = {
            "none",
            "1/6 step",
            "1/4 step",
            "1/3 step",
            "1/2 step",
            "step",
            "1/6 beat",
            "1/4 beat",
            "1/3 beat",
            "1/2 beat",
            "beat",
            "bar"
        };
        float menuH = static_cast<float>(snapOptions.size()) * menuRowHeight + menuPadding * 2.0f;
        bool menuHit = isInsideRect(menuX, menuY, menuW, menuH, mouseX, mouseY);

        g_state.hoverIndex = -1;
        if (g_state.menuOpen && menuHit) {
            float localY = mouseY - menuY - menuPadding;
            int index = static_cast<int>(localY / menuRowHeight);
            if (index >= 0 && index < static_cast<int>(snapOptions.size())) {
                g_state.hoverIndex = index;
            }
        }

        float scaleMenuX = g_state.scaleButton.x;
        float scaleMenuY = kBorderHeight + 6.0f;
        float scaleMenuW = 360.0f;
        const int scaleMenuRows = 13;
        float scaleMenuH = static_cast<float>(scaleMenuRows) * menuRowHeight + menuPadding * 2.0f;
        bool scaleMenuHit = isInsideRect(scaleMenuX, scaleMenuY, scaleMenuW, scaleMenuH, mouseX, mouseY);

        g_state.hoverScaleColumn = -1;
        g_state.hoverScaleRow = -1;
        if (g_state.scaleMenuOpen && scaleMenuHit) {
            float columnWidth = scaleMenuW / 3.0f;
            float localY = mouseY - scaleMenuY - menuPadding;
            int index = static_cast<int>(localY / menuRowHeight);
            float localX = mouseX - scaleMenuX;
            int column = (localX < columnWidth) ? 0 : (localX < columnWidth * 2.0f ? 1 : 2);
            const int maxRows[3] = { 12, 7, 7 };
            if (index >= 1 && index <= maxRows[column]) {
                g_state.hoverScaleColumn = column;
                g_state.hoverScaleRow = index - 1;
            }
        }

        if (g_state.menuOpen) {
            if (mousePressedThisFrame) {
                if (menuHit && g_state.hoverIndex >= 0) {
                    g_state.gridButton.value = snapOptions[g_state.hoverIndex];
                    g_state.gridButton.isToggled = (g_state.gridButton.value != "none");
                    g_state.menuOpen = false;
                } else if (!menuHit) {
                    g_state.menuOpen = false;
                }
            }
        } else if (g_state.scaleMenuOpen) {
            if (mousePressedThisFrame) {
                if (scaleMenuHit && g_state.hoverScaleColumn >= 0) {
                    if (g_state.hoverScaleColumn == 0) {
                        g_state.scaleRoot = g_state.hoverScaleRow;
                    } else if (g_state.hoverScaleColumn == 1) {
                        if (g_state.hoverScaleRow == 0) {
                            g_state.scaleType = ScaleType::None;
                        } else if (g_state.hoverScaleRow == 1) {
                            g_state.scaleType = ScaleType::Major;
                        } else if (g_state.hoverScaleRow == 2) {
                            g_state.scaleType = ScaleType::HarmonicMinor;
                        } else if (g_state.hoverScaleRow == 3) {
                            g_state.scaleType = ScaleType::MelodicMinor;
                        } else if (g_state.hoverScaleRow == 4) {
                            g_state.scaleType = ScaleType::HungarianMinor;
                        } else if (g_state.hoverScaleRow == 5) {
                            g_state.scaleType = ScaleType::NeapolitanMajor;
                        } else if (g_state.hoverScaleRow == 6) {
                            g_state.scaleType = ScaleType::DoubleHarmonicMinor;
                        }
                    } else if (g_state.hoverScaleColumn == 2) {
                        g_state.scaleMode = g_state.hoverScaleRow;
                    }
                    g_state.scaleMenuOpen = false;
                } else if (!scaleMenuHit) {
                    g_state.scaleMenuOpen = false;
                }
            }
        } else if (mousePressedThisFrame) {
            if (buttonHit) {
                g_state.menuOpen = true;
                g_state.scaleMenuOpen = false;
            } else if (scaleButtonHit) {
                g_state.scaleMenuOpen = true;
                g_state.menuOpen = false;
            } else if (drawButtonHit) {
                g_state.editMode = EditMode::Draw;
            } else if (paintButtonHit) {
                g_state.editMode = EditMode::Paint;
            }
        }

        bool suppressKeyPress = (g_state.menuOpen || g_state.scaleMenuOpen || buttonHit || scaleButtonHit || drawButtonHit || paintButtonHit);
        int pressedIndex = -1;
        int blackPressedIndex = -1;
        if (mouseDown == GLFW_PRESS && !suppressKeyPress) {
            for (int i = 0; i < static_cast<int>(g_state.blackKeys.size()); ++i) {
                const auto& key = g_state.blackKeys[i];
                if (isInsideRect(key.x, key.y + g_state.scrollOffsetY, key.w, key.h, mouseX, mouseY)) {
                    blackPressedIndex = i;
                    break;
                }
            }
            if (blackPressedIndex == -1) {
                for (int i = 0; i < static_cast<int>(g_state.whiteKeys.size()); ++i) {
                    const auto& key = g_state.whiteKeys[i];
                    if (isInsideRect(key.x, key.y + g_state.scrollOffsetY, key.w, key.h, mouseX, mouseY)) {
                        pressedIndex = i;
                        break;
                    }
                }
            }
        }

        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - g_state.lastTime);
        g_state.lastTime = currentTime;
        float animSpeed = 0.5f / kPressDuration;

        for (int i = 0; i < static_cast<int>(g_state.whiteKeys.size()); ++i) {
            g_state.whiteKeys[i].isPressed = (i == pressedIndex);
            float target = g_state.whiteKeys[i].isPressed ? 0.5f : 0.0f;
            if (g_state.whiteKeys[i].pressAnim < target) {
                g_state.whiteKeys[i].pressAnim = std::min(target, g_state.whiteKeys[i].pressAnim + animSpeed * deltaTime);
            } else if (g_state.whiteKeys[i].pressAnim > target) {
                g_state.whiteKeys[i].pressAnim = std::max(target, g_state.whiteKeys[i].pressAnim - animSpeed * deltaTime);
            }
        }
        for (int i = 0; i < static_cast<int>(g_state.blackKeys.size()); ++i) {
            g_state.blackKeys[i].isPressed = (i == blackPressedIndex);
            float target = g_state.blackKeys[i].isPressed ? 0.5f : 0.0f;
            if (g_state.blackKeys[i].pressAnim < target) {
                g_state.blackKeys[i].pressAnim = std::min(target, g_state.blackKeys[i].pressAnim + animSpeed * deltaTime);
            } else if (g_state.blackKeys[i].pressAnim > target) {
                g_state.blackKeys[i].pressAnim = std::max(target, g_state.blackKeys[i].pressAnim - animSpeed * deltaTime);
            }
        }

        auto pulseButton = [&](ToggleButton& button) {
            float target = button.isToggled ? 0.5f : 0.0f;
            if (button.pressAnim < target) {
                button.pressAnim = std::min(target, button.pressAnim + animSpeed * deltaTime);
            } else if (button.pressAnim > target) {
                button.pressAnim = std::max(target, button.pressAnim - animSpeed * deltaTime);
            }
        };
        pulseButton(g_state.gridButton);
        pulseButton(g_state.modeDrawButton);
        pulseButton(g_state.modePaintButton);
        pulseButton(g_state.scaleButton);

        float gridOrigin = viewBottom;
        float gridStep = kLaneStep;
        int startRow = static_cast<int>(std::floor((gridOrigin - viewBottom) / gridStep));
        int endRow = static_cast<int>(std::ceil((gridOrigin - viewTop) / gridStep));
        if (startRow < 0) startRow = 0;
        if (endRow > kTotalRows - 1) endRow = kTotalRows - 1;

        int mouseRow = -1;
        if (mouseX >= gridLeft && mouseX <= gridRight && mouseY >= viewTop && mouseY <= viewBottom) {
            mouseRow = static_cast<int>((gridOrigin - mouseY) / gridStep);
            if (mouseRow < 0 || mouseRow >= kTotalRows) mouseRow = -1;
        }

        bool inGridArea = (mouseX >= gridLeft && mouseX <= gridRight && mouseY >= viewTop && mouseY <= viewBottom);
        int hoverNote = -1;
        if (inGridArea) {
            float noteStartX = gridLeft + g_state.scrollOffsetX + static_cast<float>((clipStartSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
            for (int i = static_cast<int>(clip.notes.size()) - 1; i >= 0; --i) {
                double noteStart = static_cast<double>(clip.notes[i].startSample);
                double noteLen = static_cast<double>(clip.notes[i].length);
                int row = clip.notes[i].pitch - 24;
                if (row < 0 || row >= kTotalRows) continue;
                float nx = noteStartX + static_cast<float>(noteStart * pxPerSample);
                float ny = gridOrigin - (row + 1) * gridStep;
                float nw = static_cast<float>(noteLen * pxPerSample);
                float nh = gridStep;
                if (mouseX >= nx && mouseX <= nx + nw && mouseY >= ny && mouseY <= ny + nh) {
                    hoverNote = i;
                    break;
                }
            }
        }

        GLFWcursor* desiredCursor = g_state.cursorDefault;
        if (inGridArea) {
            if (hoverNote >= 0) {
                float noteStartX = gridLeft + g_state.scrollOffsetX + static_cast<float>((clipStartSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
                double noteStart = static_cast<double>(clip.notes[hoverNote].startSample);
                double noteLen = static_cast<double>(clip.notes[hoverNote].length);
                float nx = noteStartX + static_cast<float>(noteStart * pxPerSample);
                float nw = static_cast<float>(noteLen * pxPerSample);
                if (mouseX >= nx + nw - kNoteHandleSize) {
                    desiredCursor = g_state.cursorResize ? g_state.cursorResize : g_state.cursorDefault;
                } else {
                    desiredCursor = g_state.cursorMove ? g_state.cursorMove : g_state.cursorDefault;
                }
            } else if (g_state.editMode == EditMode::Draw && g_state.cursorDraw) {
                desiredCursor = g_state.cursorDraw;
            } else if (g_state.editMode == EditMode::Paint && g_state.cursorBrush) {
                desiredCursor = g_state.cursorBrush;
            }
        }
        if (desiredCursor && desiredCursor != g_state.currentCursor) {
            glfwSetCursor(win, desiredCursor);
            g_state.currentCursor = desiredCursor;
        }

        if (mousePressedThisFrame && inGridArea) {
            g_state.activeNote = -1;
            g_state.resizingNote = false;
            float noteStartX = gridLeft + g_state.scrollOffsetX + static_cast<float>((clipStartSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
            for (int i = static_cast<int>(clip.notes.size()) - 1; i >= 0; --i) {
                double noteStart = static_cast<double>(clip.notes[i].startSample);
                double noteLen = static_cast<double>(clip.notes[i].length);
                int row = clip.notes[i].pitch - 24;
                if (row < 0 || row >= kTotalRows) continue;
                float nx = noteStartX + static_cast<float>(noteStart * pxPerSample);
                float ny = gridOrigin - (row + 1) * gridStep;
                float nw = static_cast<float>(noteLen * pxPerSample);
                float nh = gridStep;
                if (mouseX >= nx && mouseX <= nx + nw && mouseY >= ny && mouseY <= ny + nh) {
                    g_state.activeNote = i;
                    double nextStart = getNextNoteStart(clip.notes, clip.notes[i].pitch, noteStart, i);
                    bool hasSpace = (nextStart < 0.0) || (nextStart - noteStart > noteLen);
                    if (mouseX >= nx + nw - kNoteHandleSize && hasSpace) {
                        g_state.resizingNote = true;
                    } else {
                        g_state.resizingNote = false;
                        g_state.dragOffsetSamples = (mouseX - nx) / pxPerSample;
                    }
                    break;
                }
            }

            if (g_state.editMode == EditMode::Paint && g_state.activeNote == -1 && mouseRow >= 0) {
                g_state.painting = true;
                g_state.paintLastX.assign(kTotalRows, -1.0);
                g_state.paintLastXGlobal = -1.0;
                g_state.paintLastRow = mouseRow;
                g_state.paintLastCursorX = -1.0;
                g_state.paintDir = 0;
            } else {
                g_state.painting = false;
            }

            if (g_state.editMode == EditMode::Draw && g_state.activeNote == -1 && mouseRow >= 0) {
                double localX = (mouseX - gridLeft - g_state.scrollOffsetX) / pxPerSample;
                double snappedX = snapSamples > 0.0 ? snapFloor(localX, snapSamples) : localX;
                double defaultLen = beatSamples * 0.5;
                double noteLen = (g_state.lastNoteLengthSamples > 0.0) ? g_state.lastNoteLengthSamples : defaultLen;
                if (noteLen < minNoteLenSamples) noteLen = minNoteLenSamples;
                int targetRow = adjustRowToScale(mouseRow, mouseY, gridOrigin, gridStep, kTotalRows, g_state.scaleRoot, g_state.scaleType, g_state.scaleMode);
                int pitch = 24 + targetRow;
                double startSample = snappedX;
                double len = noteLen;
                if (startSample < 0.0) startSample = 0.0;
                if (startSample + len > static_cast<double>(clip.length)) {
                    len = std::max(0.0, static_cast<double>(clip.length) - startSample);
                }
                if (len >= minNoteLenSamples) {
                    if (placeNote(clip.notes, pitch, startSample, len, snapSamples, minNoteLenSamples)) {
                        g_state.activeNote = static_cast<int>(clip.notes.size()) - 1;
                        g_state.resizingNote = false;
                        g_state.dragOffsetSamples = (mouseX - (gridLeft + g_state.scrollOffsetX + static_cast<float>(startSample * pxPerSample))) / pxPerSample;
                    }
                }
            }
        }

        if (mouseDown == GLFW_PRESS && g_state.painting && inGridArea && mouseRow >= 0 && hoverNote < 0) {
            if (!isScaleNote(mouseRow % 12, g_state.scaleRoot, g_state.scaleType, g_state.scaleMode)) {
            } else {
                double localX = (mouseX - gridLeft - g_state.scrollOffsetX) / pxPerSample;
                double defaultLen = beatSamples * 0.5;
                double noteLen = (g_state.lastNoteLengthSamples > 0.0) ? g_state.lastNoteLengthSamples : defaultLen;
                if (noteLen < minNoteLenSamples) noteLen = minNoteLenSamples;

                double lastX = g_state.paintLastX[static_cast<size_t>(mouseRow)];
                double lastXAny = g_state.paintLastXGlobal;
                double startX = -1.0;

                if (snapSamples > 0.0) {
                    double targetX = snapFloor(localX, snapSamples);
                    if (lastX < 0.0) {
                        startX = targetX;
                    } else if (std::fabs(targetX - lastX) >= noteLen) {
                        startX = targetX;
                    }
                } else {
                    if (lastXAny < 0.0) {
                        startX = localX;
                    } else {
                        double baseX = (lastX >= 0.0) ? lastX : lastXAny;
                        double delta = localX - baseX;
                        if (std::fabs(delta) >= noteLen) {
                            int steps = static_cast<int>(std::floor(std::fabs(delta) / noteLen));
                            if (steps > 0) {
                                startX = baseX + (delta > 0.0 ? static_cast<double>(steps) * noteLen : -static_cast<double>(steps) * noteLen);
                            }
                        }
                    }
                }

                if (startX >= 0.0) {
                    double placeX = startX;
                    double len = noteLen;
                    bool allowShiftForward = true;
                    if (snapSamples <= 0.0 && lastXAny >= 0.0) {
                        double baseX = (lastX >= 0.0) ? lastX : lastXAny;
                        if (localX < baseX) allowShiftForward = false;
                    }
                    int pitch = 24 + mouseRow;
                    if (placeX < 0.0) placeX = 0.0;
                    if (placeX + len > static_cast<double>(clip.length)) {
                        len = std::max(0.0, static_cast<double>(clip.length) - placeX);
                    }
                    if (len >= minNoteLenSamples && placeNote(clip.notes, pitch, placeX, len, snapSamples, minNoteLenSamples, allowShiftForward)) {
                        g_state.paintLastX[static_cast<size_t>(mouseRow)] = placeX;
                        g_state.paintLastXGlobal = placeX;
                        g_state.paintLastRow = mouseRow;
                    }
                }
            }
        }
        if (g_state.painting && inGridArea) {
            g_state.paintLastCursorX = (mouseX - gridLeft - g_state.scrollOffsetX) / pxPerSample;
        }

        if (mouseDown == GLFW_PRESS && g_state.activeNote >= 0 && g_state.activeNote < static_cast<int>(clip.notes.size())) {
            double localX = (mouseX - gridLeft - g_state.scrollOffsetX) / pxPerSample;
            if (g_state.resizingNote) {
                double newLen = snapSamples > 0.0 ? snapValue(localX - static_cast<double>(clip.notes[g_state.activeNote].startSample), snapSamples)
                                                   : (localX - static_cast<double>(clip.notes[g_state.activeNote].startSample));
                if (newLen < minSnapLenSamples) newLen = minSnapLenSamples;
                double nextStart = getNextNoteStart(clip.notes, clip.notes[g_state.activeNote].pitch, static_cast<double>(clip.notes[g_state.activeNote].startSample), g_state.activeNote);
                if (nextStart >= 0.0 && static_cast<double>(clip.notes[g_state.activeNote].startSample) + newLen > nextStart) {
                    newLen = snapSamples > 0.0 ? snapValue(nextStart - static_cast<double>(clip.notes[g_state.activeNote].startSample), snapSamples)
                                               : (nextStart - static_cast<double>(clip.notes[g_state.activeNote].startSample));
                }
                if (newLen >= minSnapLenSamples) {
                    if (static_cast<double>(clip.notes[g_state.activeNote].startSample) + newLen > static_cast<double>(clip.length)) {
                        newLen = std::max(minSnapLenSamples, static_cast<double>(clip.length) - static_cast<double>(clip.notes[g_state.activeNote].startSample));
                    }
                    clip.notes[g_state.activeNote].length = static_cast<uint64_t>(std::round(newLen));
                    g_state.lastNoteLengthSamples = newLen;
                }
            } else {
                double snappedX = snapSamples > 0.0 ? snapFloor(localX - g_state.dragOffsetSamples, snapSamples)
                                                     : (localX - g_state.dragOffsetSamples);
                if (snappedX < 0.0) snappedX = 0.0;
                int targetRow = mouseRow >= 0 ? mouseRow : (clip.notes[g_state.activeNote].pitch - 24);
                if (targetRow >= 0) {
                    targetRow = adjustRowToScale(targetRow, mouseY, gridOrigin, gridStep, kTotalRows, g_state.scaleRoot, g_state.scaleType, g_state.scaleMode);
                }
                int pitch = 24 + targetRow;
                for (int guard = 0; guard < static_cast<int>(clip.notes.size()); ++guard) {
                    int sameStart = findNoteAtStart(clip.notes, pitch, snappedX, g_state.activeNote);
                    if (sameStart < 0) break;
                    snappedX = snapSamples > 0.0 ? snapFloor(static_cast<double>(clip.notes[sameStart].startSample + clip.notes[sameStart].length), snapSamples)
                                                 : static_cast<double>(clip.notes[sameStart].startSample + clip.notes[sameStart].length);
                }
                if (snappedX + static_cast<double>(clip.notes[g_state.activeNote].length) > static_cast<double>(clip.length)) {
                    snappedX = std::max(0.0, static_cast<double>(clip.length) - static_cast<double>(clip.notes[g_state.activeNote].length));
                }
                clip.notes[g_state.activeNote].startSample = static_cast<uint64_t>(std::round(snappedX));
                clip.notes[g_state.activeNote].pitch = pitch;
            }
        }

        if (mouseReleasedThisFrame && g_state.activeNote >= 0 && g_state.activeNote < static_cast<int>(clip.notes.size()) && !g_state.resizingNote) {
            double localX = (mouseX - gridLeft - g_state.scrollOffsetX) / pxPerSample;
            double snappedX = snapSamples > 0.0 ? snapFloor(localX - g_state.dragOffsetSamples, snapSamples)
                                                 : (localX - g_state.dragOffsetSamples);
            if (snappedX < 0.0) snappedX = 0.0;
            int targetRow = mouseRow >= 0 ? mouseRow : (clip.notes[g_state.activeNote].pitch - 24);
            if (targetRow >= 0) {
                targetRow = adjustRowToScale(targetRow, mouseY, gridOrigin, gridStep, kTotalRows, g_state.scaleRoot, g_state.scaleType, g_state.scaleMode);
            }
            int pitch = 24 + targetRow;

            for (int guard = 0; guard < static_cast<int>(clip.notes.size()); ++guard) {
                int sameStart = findNoteAtStart(clip.notes, pitch, snappedX, g_state.activeNote);
                if (sameStart < 0) break;
                snappedX = snapSamples > 0.0 ? snapFloor(static_cast<double>(clip.notes[sameStart].startSample + clip.notes[sameStart].length), snapSamples)
                                             : static_cast<double>(clip.notes[sameStart].startSample + clip.notes[sameStart].length);
            }

            int overlap = findOverlappingNote(clip.notes, pitch, snappedX, g_state.activeNote);
            if (overlap >= 0) {
                double newLen = snappedX - static_cast<double>(clip.notes[overlap].startSample);
                if (newLen < minNoteLenSamples) {
                    newLen = minNoteLenSamples;
                    snappedX = static_cast<double>(clip.notes[overlap].startSample) + newLen;
                }
                clip.notes[overlap].length = static_cast<uint64_t>(std::round(newLen));
            }

            double nextStart = getNextNoteStart(clip.notes, pitch, snappedX, g_state.activeNote);
            double maxLen = static_cast<double>(clip.notes[g_state.activeNote].length);
            if (nextStart >= 0.0 && snappedX + maxLen > nextStart) {
                maxLen = nextStart - snappedX;
            }
            if (snappedX + maxLen > static_cast<double>(clip.length)) {
                maxLen = std::max(1.0, static_cast<double>(clip.length) - snappedX);
            }
            if (maxLen < minNoteLenSamples) {
                maxLen = minNoteLenSamples;
            }
            clip.notes[g_state.activeNote].startSample = static_cast<uint64_t>(std::round(snappedX));
            clip.notes[g_state.activeNote].pitch = pitch;
            clip.notes[g_state.activeNote].length = static_cast<uint64_t>(std::round(maxLen));
        }

        if (mouseDown == GLFW_RELEASE) {
            g_state.activeNote = -1;
            g_state.resizingNote = false;
            g_state.painting = false;
            g_state.paintLastRow = -1;
            g_state.paintLastCursorX = -1.0;
            g_state.paintDir = 0;
        }

        if (rightDown == GLFW_PRESS && inGridArea) {
            float noteStartX = gridLeft + g_state.scrollOffsetX + static_cast<float>((clipStartSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
            for (int i = static_cast<int>(clip.notes.size()) - 1; i >= 0; --i) {
                double noteStart = static_cast<double>(clip.notes[i].startSample);
                double noteLen = static_cast<double>(clip.notes[i].length);
                int row = clip.notes[i].pitch - 24;
                if (row < 0 || row >= kTotalRows) continue;
                float nx = noteStartX + static_cast<float>(noteStart * pxPerSample);
                float ny = gridOrigin - (row + 1) * gridStep;
                float nw = static_cast<float>(noteLen * pxPerSample);
                float nh = gridStep;
                if (mouseX >= nx && mouseX <= nx + nw && mouseY >= ny && mouseY <= ny + nh) {
                    DeleteAnim anim;
                    anim.x = nx;
                    anim.y = ny;
                    anim.w = nw;
                    anim.h = nh;
                    int noteIndex = row % 12;
                    if (noteIndex < 0) noteIndex += 12;
                    noteColor(noteIndex, anim.r, anim.g, anim.b);
                    anim.startTime = currentTime;
                    g_state.deleteAnims.push_back(anim);
                    clip.notes.erase(clip.notes.begin() + i);
                    break;
                }
            }
        }

        g_state.wasMouseDown = (mouseDown == GLFW_PRESS);
        g_state.wasRightDown = (rightDown == GLFW_PRESS);

        g_vertices.clear();
        g_vertices.reserve(4096);

        glm::vec3 backdrop(0.0f, 0.188f, 0.188f);
        pushRect(g_vertices, 0.0f, 0.0f, static_cast<float>(screenWidth), static_cast<float>(screenHeight), backdrop, screenWidth, screenHeight);

        glm::vec3 gridBlack(0.0f, 0.22f, 0.22f);
        for (int row = startRow; row <= endRow; ++row) {
            int note = row % 12;
            if (note < 0) note += 12;
            bool isBlack = (note == 1 || note == 3 || note == 6 || note == 8 || note == 10);
            if (!isBlack) continue;
            float y1 = gridOrigin - row * gridStep;
            float y0 = y1 - gridStep;
            if (y1 < viewTop || y0 > viewBottom) continue;
            float yy0 = (y0 < viewTop) ? viewTop : y0;
            float yy1 = (y1 > viewBottom) ? viewBottom : y1;
            pushRect(g_vertices, gridLeft, yy0, gridRight - gridLeft, yy1 - yy0, gridBlack, screenWidth, screenHeight);
        }

        glm::vec3 gridLine(0.0f, 0.33f, 0.33f);
        for (int row = startRow; row <= endRow + 1; ++row) {
            float y = gridOrigin - row * gridStep;
            if (y < viewTop || y > viewBottom) continue;
            pushLine(g_vertices, gridLeft, y, gridRight, y, 1.0f, gridLine, screenWidth, screenHeight);
        }
        pushLine(g_vertices, gridLeft, viewTop, gridLeft, viewBottom, 1.0f, gridLine, screenWidth, screenHeight);

        float barStartPx = gridLeft + g_state.scrollOffsetX + static_cast<float>((0.0 - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
        float beatStartPx = barStartPx;
        float snapStartPx = barStartPx;
        int barIndexStart = static_cast<int>(std::floor((gridLeft - barStartPx) / (barSamples * pxPerSample)));
        int beatIndexStart = static_cast<int>(std::floor((gridLeft - beatStartPx) / (beatSamples * pxPerSample)));
        int snapIndexStart = 0;
        if (snapWidthDisplay > 0.0) {
            snapIndexStart = static_cast<int>(std::floor((gridLeft - snapStartPx) / snapWidthDisplay));
        }

        glm::vec3 barLine(0.12f, 0.48f, 0.48f);
        float barWidthPx = static_cast<float>(barSamples * pxPerSample);
        for (float x = barStartPx + barIndexStart * barWidthPx; x <= gridRight; x += barWidthPx) {
            pushLine(g_vertices, x, viewTop, x, viewBottom, 1.0f, barLine, screenWidth, screenHeight);
        }

        glm::vec3 beatLine(0.1f, 0.4f, 0.4f);
        float beatWidthPx = static_cast<float>(beatSamples * pxPerSample);
        for (float x = beatStartPx + beatIndexStart * beatWidthPx; x <= gridRight; x += beatWidthPx) {
            pushLine(g_vertices, x, viewTop, x, viewBottom, 1.0f, beatLine, screenWidth, screenHeight);
        }

        if (snapWidthDisplay > 0.0) {
            glm::vec3 snapLine(0.08f, 0.32f, 0.32f);
            for (float x = snapStartPx + snapIndexStart * static_cast<float>(snapWidthDisplay); x <= gridRight; x += static_cast<float>(snapWidthDisplay)) {
                pushLine(g_vertices, x, viewTop, x, viewBottom, 1.0f, snapLine, screenWidth, screenHeight);
            }
        }

        glm::vec3 labelColor(0.6f, 0.7f, 0.7f);
        const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int row = startRow; row <= endRow; ++row) {
            int noteIndex = row % 12;
            if (noteIndex < 0) noteIndex += 12;
            int octave = 1 + (row / 12);
            float y1 = gridOrigin - row * gridStep;
            float y0 = y1 - gridStep;
            if (y1 < viewTop || y0 > viewBottom) continue;
            float labelY = y0 + gridStep * 0.65f;
            char label[8];
            std::snprintf(label, sizeof(label), "%s%d", noteNames[noteIndex], octave);
            pushText(g_vertices, gridLeft + 6.0f, labelY, label, labelColor, screenWidth, screenHeight);
        }

        float noteStartX = gridLeft + g_state.scrollOffsetX + static_cast<float>((clipStartSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
        for (const auto& note : clip.notes) {
            int row = note.pitch - 24;
            if (row < 0 || row >= kTotalRows) continue;
            float nx = noteStartX + static_cast<float>(note.startSample * pxPerSample);
            float ny = gridOrigin - (row + 1) * gridStep;
            float nw = static_cast<float>(note.length * pxPerSample);
            float nh = gridStep;
            if (ny + gridStep < viewTop || ny > viewBottom) continue;
            if (nx + nw < gridLeft || nx > gridRight) continue;
            int noteIndex = row % 12;
            if (noteIndex < 0) noteIndex += 12;
            float nr = 0.75f, ng = 0.85f, nb = 0.9f;
            noteColor(noteIndex, nr, ng, nb);
            Key noteKey;
            noteKey.x = nx;
            noteKey.y = ny;
            noteKey.w = nw;
            noteKey.h = nh;
            noteKey.depth = 4.0f;
            noteKey.z = -10.0f;
            drawBeveledQuadTint(noteKey, 0.0f, nr, ng, nb, g_vertices, screenWidth, screenHeight);

            int octave = 1 + (row / 12);
            char label[8];
            std::snprintf(label, sizeof(label), "%s%d", noteNames[noteIndex], octave);
            pushText(g_vertices, nx + 4.0f, ny + gridStep - 12.0f, label, glm::vec3(0.1f, 0.2f, 0.25f), screenWidth, screenHeight);
        }

        const float deleteDuration = 0.18f;
        for (int i = static_cast<int>(g_state.deleteAnims.size()) - 1; i >= 0; --i) {
            float t = static_cast<float>(currentTime - g_state.deleteAnims[i].startTime);
            if (t > deleteDuration) {
                g_state.deleteAnims.erase(g_state.deleteAnims.begin() + i);
                continue;
            }
            float p = t / deleteDuration;
            float expand = 10.0f * p;
            float x0 = g_state.deleteAnims[i].x - expand;
            float y0 = g_state.deleteAnims[i].y - expand;
            float x1 = g_state.deleteAnims[i].x + g_state.deleteAnims[i].w + expand;
            float y1 = g_state.deleteAnims[i].y + g_state.deleteAnims[i].h + expand;
            glm::vec3 color(g_state.deleteAnims[i].r, g_state.deleteAnims[i].g, g_state.deleteAnims[i].b);
            pushRectOutline(g_vertices, x0, y0, x1 - x0, y1 - y0, 1.0f, color, screenWidth, screenHeight);
        }

        for (const auto& key : g_state.whiteKeys) {
            drawBeveledQuadTint(key, g_state.scrollOffsetY, 0.8f, 0.8f, 0.8f, g_vertices, screenWidth, screenHeight);
        }
        for (const auto& key : g_state.blackKeys) {
            drawBeveledQuadTint(key, g_state.scrollOffsetY, 0.3f, 0.3f, 0.3f, g_vertices, screenWidth, screenHeight);
        }

        for (const auto& key : g_state.whiteKeys) {
            float y = key.y + g_state.scrollOffsetY;
            if (y + key.h < viewTop || y > viewBottom) continue;
            char label[8];
            std::snprintf(label, sizeof(label), "%s%d", noteNames[key.note], key.octave);
            float labelX = key.x + key.w - 22.0f;
            float labelY = y + key.h - 10.0f;
            pushText(g_vertices, labelX, labelY, label, glm::vec3(0.0f), screenWidth, screenHeight);
        }
        for (const auto& key : g_state.blackKeys) {
            float y = key.y + g_state.scrollOffsetY;
            if (y + key.h < viewTop || y > viewBottom) continue;
            char label[8];
            std::snprintf(label, sizeof(label), "%s%d", noteNames[key.note], key.octave);
            float labelX = key.x + key.w - 22.0f;
            float labelY = y + key.h - 10.0f;
            pushText(g_vertices, labelX, labelY, label, glm::vec3(0.9f), screenWidth, screenHeight);
        }

        if (clipStartX > gridLeft) {
            pushRect(g_vertices, gridLeft, viewTop, clipStartX - gridLeft, viewBottom - viewTop, glm::vec3(0.0f, 0.15f, 0.15f), screenWidth, screenHeight);
        }
        if (clipEndX < gridRight) {
            pushRect(g_vertices, clipEndX, viewTop, gridRight - clipEndX, viewBottom - viewTop, glm::vec3(0.0f, 0.15f, 0.15f), screenWidth, screenHeight);
        }

        pushRect(g_vertices, 0.0f, 0.0f, static_cast<float>(screenWidth), kBorderHeight, glm::vec3(0.0f, 0.188f, 0.188f), screenWidth, screenHeight);
        pushRect(g_vertices, 0.0f, static_cast<float>(screenHeight) - kBorderHeight, static_cast<float>(screenWidth), kBorderHeight, glm::vec3(0.0f, 0.188f, 0.188f), screenWidth, screenHeight);
        pushRect(g_vertices, 0.0f, kBorderHeight, kLeftBorderWidth, static_cast<float>(screenHeight) - 2.0f * kBorderHeight, glm::vec3(0.0f, 0.188f, 0.188f), screenWidth, screenHeight);

        int barNumber = barIndexStart + 1;
        for (float x = barStartPx + barIndexStart * barWidthPx; x <= gridRight; x += barWidthPx, ++barNumber) {
            if (barNumber <= 0) continue;
            char label[8];
            std::snprintf(label, sizeof(label), "%d", barNumber);
            pushText(g_vertices, x + 6.0f, 33.0f, label, glm::vec3(0.85f, 0.9f, 0.9f), screenWidth, screenHeight);
        }

        pushRect(g_vertices, closeLeft, closeTop, closeSize, closeSize, glm::vec3(0.75f, 0.28f, 0.25f), screenWidth, screenHeight);
        pushLine(g_vertices, closeLeft + 7.0f, closeTop + 7.0f, closeLeft + closeSize - 7.0f, closeTop + closeSize - 7.0f, 2.0f, glm::vec3(0.95f), screenWidth, screenHeight);
        pushLine(g_vertices, closeLeft + closeSize - 7.0f, closeTop + 7.0f, closeLeft + 7.0f, closeTop + closeSize - 7.0f, 2.0f, glm::vec3(0.95f), screenWidth, screenHeight);

        Key drawKey{g_state.modeDrawButton.x, g_state.modeDrawButton.y, g_state.modeDrawButton.w, g_state.modeDrawButton.h, g_state.modeDrawButton.depth, 20.0f, 0, 0, false, g_state.modeDrawButton.pressAnim};
        Key paintKey{g_state.modePaintButton.x, g_state.modePaintButton.y, g_state.modePaintButton.w, g_state.modePaintButton.h, g_state.modePaintButton.depth, 20.0f, 0, 0, false, g_state.modePaintButton.pressAnim};
        Key scaleKey{g_state.scaleButton.x, g_state.scaleButton.y, g_state.scaleButton.w, g_state.scaleButton.h, g_state.scaleButton.depth, 20.0f, 0, 0, false, g_state.scaleButton.pressAnim};
        Key buttonKey{g_state.gridButton.x, g_state.gridButton.y, g_state.gridButton.w, g_state.gridButton.h, g_state.gridButton.depth, 20.0f, 0, 0, false, g_state.gridButton.pressAnim};

        if (g_state.modeDrawButton.isToggled) {
            drawBeveledQuadTint(drawKey, 0.0f, 0.85f, 0.9f, 0.9f, g_vertices, screenWidth, screenHeight);
        } else {
            drawBeveledQuadTint(drawKey, 0.0f, 0.8f, 0.8f, 0.8f, g_vertices, screenWidth, screenHeight);
        }
        pushText(g_vertices, drawKey.x + 6.0f, drawKey.y + 18.0f, "Draw", glm::vec3(0.0f), screenWidth, screenHeight);

        if (g_state.modePaintButton.isToggled) {
            drawBeveledQuadTint(paintKey, 0.0f, 0.85f, 0.9f, 0.9f, g_vertices, screenWidth, screenHeight);
        } else {
            drawBeveledQuadTint(paintKey, 0.0f, 0.8f, 0.8f, 0.8f, g_vertices, screenWidth, screenHeight);
        }
        pushText(g_vertices, paintKey.x + 6.0f, paintKey.y + 18.0f, "Paint", glm::vec3(0.0f), screenWidth, screenHeight);

        if (g_state.scaleButton.isToggled) {
            drawBeveledQuadTint(scaleKey, 0.0f, 0.85f, 0.9f, 0.9f, g_vertices, screenWidth, screenHeight);
        } else {
            drawBeveledQuadTint(scaleKey, 0.0f, 0.8f, 0.8f, 0.8f, g_vertices, screenWidth, screenHeight);
        }
        pushText(g_vertices, scaleKey.x + 4.0f, scaleKey.y + 3.0f, "Scale", glm::vec3(0.0f), screenWidth, screenHeight);
        pushText(g_vertices, scaleKey.x + 4.0f, scaleKey.y + 18.0f, g_state.scaleButton.value.c_str(), glm::vec3(0.0f), screenWidth, screenHeight);

        if (g_state.gridButton.isToggled) {
            drawBeveledQuadTint(buttonKey, 0.0f, 0.85f, 0.9f, 0.9f, g_vertices, screenWidth, screenHeight);
        } else {
            drawBeveledQuadTint(buttonKey, 0.0f, 0.8f, 0.8f, 0.8f, g_vertices, screenWidth, screenHeight);
        }
        pushText(g_vertices, buttonKey.x + 4.0f, buttonKey.y + 3.0f, "Snap", glm::vec3(0.0f), screenWidth, screenHeight);
        pushMultilineText(g_vertices, buttonKey.x + 4.0f, buttonKey.y + 15.0f, formatButtonValue(g_state.gridButton.value), 10.0f, glm::vec3(0.0f), screenWidth, screenHeight);

        if (g_state.menuOpen) {
            Key menuKey{menuX, menuY, menuW, menuH, 6.0f, 30.0f, 0, 0, false, 0.0f};
            drawBeveledQuadTint(menuKey, 0.0f, 0.8f, 0.8f, 0.8f, g_vertices, screenWidth, screenHeight);

            if (g_state.hoverIndex >= 0) {
                float y0 = menuY + menuPadding + g_state.hoverIndex * menuRowHeight;
                float y1 = y0 + menuRowHeight;
                pushRect(g_vertices, menuX + 2.0f, y0, menuW - 4.0f, y1 - y0, glm::vec3(0.85f, 0.9f, 0.9f), screenWidth, screenHeight);
            }

            for (int i = 0; i < static_cast<int>(snapOptions.size()); ++i) {
                float textY = menuY + menuPadding + i * menuRowHeight + 4.0f;
                pushText(g_vertices, menuX + 8.0f, textY, snapOptions[i].c_str(), glm::vec3(0.0f), screenWidth, screenHeight);
            }
        }

        if (g_state.scaleMenuOpen) {
            Key menuKey{scaleMenuX, scaleMenuY, scaleMenuW, scaleMenuH, 6.0f, 30.0f, 0, 0, false, 0.0f};
            drawBeveledQuadTint(menuKey, 0.0f, 0.8f, 0.8f, 0.8f, g_vertices, screenWidth, screenHeight);

            float columnWidth = scaleMenuW / 3.0f;
            float markOffset = 4.0f;
            const char* modeNames[7] = { "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian" };
            const char* scaleNames[7] = { "Off", "Major", "Harm Min", "Mel Min", "Hung Min", "Neo Maj", "Dbl Harm" };
            const char* noteNamesLocal[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

            if (g_state.hoverScaleColumn >= 0) {
                float hoverX = scaleMenuX + g_state.hoverScaleColumn * columnWidth;
                float hoverY = scaleMenuY + menuPadding + (1 + g_state.hoverScaleRow) * menuRowHeight;
                pushRect(g_vertices, hoverX + 2.0f, hoverY, columnWidth - 4.0f, menuRowHeight, glm::vec3(0.85f, 0.9f, 0.9f), screenWidth, screenHeight);
            }

            float rootX = scaleMenuX;
            for (int i = 0; i < 12; ++i) {
                float textY = scaleMenuY + menuPadding + (1 + i) * menuRowHeight + 4.0f;
                if (g_state.scaleRoot == i && g_state.scaleType != ScaleType::None) {
                    pushText(g_vertices, rootX + markOffset, textY, "x", glm::vec3(0.0f), screenWidth, screenHeight);
                }
                pushText(g_vertices, rootX + 12.0f, textY, noteNamesLocal[i], glm::vec3(0.0f), screenWidth, screenHeight);
            }

            float scaleX = scaleMenuX + columnWidth;
            for (int i = 0; i < 7; ++i) {
                float textY = scaleMenuY + menuPadding + (1 + i) * menuRowHeight + 4.0f;
                if ((i == 0 && g_state.scaleType == ScaleType::None) ||
                    (i == 1 && g_state.scaleType == ScaleType::Major) ||
                    (i == 2 && g_state.scaleType == ScaleType::HarmonicMinor) ||
                    (i == 3 && g_state.scaleType == ScaleType::MelodicMinor) ||
                    (i == 4 && g_state.scaleType == ScaleType::HungarianMinor) ||
                    (i == 5 && g_state.scaleType == ScaleType::NeapolitanMajor) ||
                    (i == 6 && g_state.scaleType == ScaleType::DoubleHarmonicMinor)) {
                    pushText(g_vertices, scaleX + markOffset, textY, "x", glm::vec3(0.0f), screenWidth, screenHeight);
                }
                pushText(g_vertices, scaleX + 12.0f, textY, scaleNames[i], glm::vec3(0.0f), screenWidth, screenHeight);
            }

            float modeX = scaleMenuX + columnWidth * 2.0f;
            for (int i = 0; i < 7; ++i) {
                float textY = scaleMenuY + menuPadding + (1 + i) * menuRowHeight + 4.0f;
                if (g_state.scaleMode == i && g_state.scaleType != ScaleType::None) {
                    pushText(g_vertices, modeX + markOffset, textY, "x", glm::vec3(0.0f), screenWidth, screenHeight);
                }
                pushText(g_vertices, modeX + 12.0f, textY, modeNames[i], glm::vec3(0.0f), screenWidth, screenHeight);
            }
        }

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(renderer.uiPianoRollVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.uiPianoRollVBO);
        glBufferData(GL_ARRAY_BUFFER, g_vertices.size() * sizeof(UiVertex), g_vertices.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, pos));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, color));
        glEnableVertexAttribArray(1);
        renderer.uiColorShader->use();
        renderer.uiColorShader->setFloat("alpha", 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(g_vertices.size()));
        glEnable(GL_DEPTH_TEST);

        ui.consumeClick = true;
    }
}
