#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "stb_image.h"

namespace PianoRollResourceSystemLogic {
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

    struct PianoRollConfig {
        float borderHeight = 60.0f;
        float leftBorderWidth = 12.5f;
        float keyWidth = 187.5f;
        float keyDepth = 7.5f;
        float blackKeyWidth = 125.0f;
        float blackKeyHeight = 30.0f;
        float blackKeyDepth = 7.5f;
        float laneStep = 30.0f;
        int octaveCount = 7;
        int keyCount = 7;
        int totalRows = 84;
        float pressDuration = 0.12f;
        float noteHandleSize = 8.0f;
    };

    struct PianoRollLayout {
        bool ready = false;
        int trackIndex = -1;
        int clipIndex = -1;
        double screenWidth = 1920.0;
        double screenHeight = 1080.0;
        float closeSize = 32.0f;
        float closePad = 18.0f;
        float closeLeft = 0.0f;
        float closeTop = 0.0f;
        float viewTop = 0.0f;
        float viewBottom = 0.0f;
        float gridLeft = 0.0f;
        float gridRight = 0.0f;
        float gridWidth = 0.0f;
        float maxScrollY = 0.0f;
        float minScrollY = 0.0f;
        float maxScrollX = 0.0f;
        float minScrollX = 0.0f;
        double bpm = 120.0;
        double secondsPerBeat = 0.5;
        double beatSamples = 0.0;
        double barSamples = 0.0;
        double samplesPerScreen = 1.0;
        double timelineTotalSamples = 1.0;
        double pxPerSample = 1.0;
        double snapSamples = 0.0;
        double defaultStepSamples = 0.0;
        double minNoteLenSamples = 1.0;
        double minSnapLenSamples = 1.0;
        double clipStartSample = 0.0;
        double clipEndSample = 0.0;
        float gridOrigin = 0.0f;
        float gridStep = 0.0f;
    };

    struct PianoRollState {
        bool initialized = false;
        bool active = false;
        bool layoutReady = false;
        bool timelineViewInitialized = false;
        double timelineSecondsPerScreen = 0.0;
        double verticalZoom = 1.0;
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
        int activeNoteClip = -1;
        bool resizingNote = false;
        double dragOffsetSamples = 0.0;
        double lastNoteLengthSamples = 0.0;
        EditMode editMode = EditMode::Draw;
        bool painting = false;
        int paintClipIndex = -1;
        int scaleRoot = 0;
        ScaleType scaleType = ScaleType::None;
        int scaleMode = 0;

        double lastTime = 0.0;
        int cachedTrack = -1;
        int cachedClip = -1;

        PianoRollLayout layout;
    };

    namespace {
        PianoRollState g_state;

        const PianoRollConfig kConfig = [] {
            PianoRollConfig cfg;
            cfg.totalRows = cfg.octaveCount * 12;
            return cfg;
        }();

        const std::array<const char*, 12> kNoteNames = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        const std::array<const char*, 7> kModeNames = {
            "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian"
        };
        const std::array<const char*, 7> kScaleNames = {
            "Off", "Major", "Harm Min", "Mel Min", "Hung Min", "Neo Maj", "Dbl Harm"
        };
        const std::vector<std::string> kSnapOptions = {
            "none",
            "bar",
            "beat",
            "1/2 beat",
            "1/3 beat",
            "1/4 beat",
            "1/6 beat",
            "step",
            "1/2 step",
            "1/3 step",
            "1/4 step",
            "1/6 step"
        };

        GLFWcursor* loadCursorImage(const char* path, int hotX, int hotY) {
            int width = 0;
            int height = 0;
            int channels = 0;
            unsigned char* data = stbi_load(path, &width, &height, &channels, 4);
            if (!data) {
                std::cerr << "PianoRoll: Failed to load cursor: " << path << "\n";
                return nullptr;
            }

            int outW = width / 2;
            int outH = height / 2;
            std::vector<unsigned char> scaled;
            if (outW > 0 && outH > 0) {
                scaled.resize(static_cast<size_t>(outW * outH * 4));
                for (int y = 0; y < outH; ++y) {
                    for (int x = 0; x < outW; ++x) {
                        int srcX = x * 2;
                        int srcY = y * 2;
                        int srcIdx = (srcY * width + srcX) * 4;
                        int dstIdx = (y * outW + x) * 4;
                        scaled[static_cast<size_t>(dstIdx + 0)] = data[srcIdx + 0];
                        scaled[static_cast<size_t>(dstIdx + 1)] = data[srcIdx + 1];
                        scaled[static_cast<size_t>(dstIdx + 2)] = data[srcIdx + 2];
                        scaled[static_cast<size_t>(dstIdx + 3)] = data[srcIdx + 3];
                    }
                }
            }

            GLFWimage image;
            image.width = outW > 0 ? outW : width;
            image.height = outH > 0 ? outH : height;
            image.pixels = outW > 0 ? scaled.data() : data;
            GLFWcursor* cursor = glfwCreateCursor(&image, hotX / 2, hotY / 2);
            stbi_image_free(data);
            return cursor;
        }
    }

    PianoRollState& State() {
        return g_state;
    }

    const PianoRollConfig& Config() {
        return kConfig;
    }

    const std::array<const char*, 12>& NoteNames() {
        return kNoteNames;
    }

    const std::array<const char*, 7>& ModeNames() {
        return kModeNames;
    }

    const std::array<const char*, 7>& ScaleNames() {
        return kScaleNames;
    }

    const std::vector<std::string>& SnapOptions() {
        return kSnapOptions;
    }

    float Clamp01(float v) {
        return std::clamp(v, 0.0f, 1.0f);
    }

    void NoteColor(int noteIndex, float& r, float& g, float& b) {
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

    double SnapValue(double value, double snap) {
        if (snap <= 0.0) return value;
        return std::round(value / snap) * snap;
    }

    double SnapFloor(double value, double snap) {
        if (snap <= 0.0) return value;
        return std::floor(value / snap) * snap;
    }

    void GetScaleIntervals(ScaleType type, int (&intervals)[7]) {
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

    bool IsScaleNote(int noteIndex, int root, ScaleType type, int mode) {
        if (type == ScaleType::None) return true;
        int intervals[7];
        GetScaleIntervals(type, intervals);
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

    int AdjustRowToScale(int row, float mouseY, float gridOrigin, float laneStep, int totalRows, int root, ScaleType type, int mode) {
        if (type == ScaleType::None) return row;
        int noteIndex = row % 12;
        if (noteIndex < 0) noteIndex += 12;
        if (IsScaleNote(noteIndex, root, type, mode)) return row;

        float rowTop = gridOrigin - row * laneStep;
        float rowBottom = rowTop - laneStep;
        float mid = (rowTop + rowBottom) * 0.5f;
        bool goUp = mouseY < mid;

        if (goUp) {
            for (int r = row + 1; r < totalRows; ++r) {
                int idx = r % 12;
                if (idx < 0) idx += 12;
                if (IsScaleNote(idx, root, type, mode)) return r;
            }
        } else {
            for (int r = row - 1; r >= 0; --r) {
                int idx = r % 12;
                if (idx < 0) idx += 12;
                if (IsScaleNote(idx, root, type, mode)) return r;
            }
        }
        return row;
    }

    int FindNoteAtStart(const std::vector<MidiNote>& notes, int pitch, double startSample, int excludeIndex) {
        for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
            if (i == excludeIndex) continue;
            if (notes[i].pitch != pitch) continue;
            if (std::fabs(static_cast<double>(notes[i].startSample) - startSample) < 0.5) {
                return i;
            }
        }
        return -1;
    }

    int FindOverlappingNote(const std::vector<MidiNote>& notes, int pitch, double startSample, int excludeIndex) {
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

    double GetNextNoteStart(const std::vector<MidiNote>& notes, int pitch, double startSample, int excludeIndex) {
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

    bool PlaceNote(std::vector<MidiNote>& notes,
                   int pitch,
                   double& startSample,
                   double& noteLen,
                   double snapStep,
                   double minNoteLen,
                   bool allowShiftForward) {
        for (int guard = 0; guard < static_cast<int>(notes.size()); ++guard) {
            int sameStart = FindNoteAtStart(notes, pitch, startSample, -1);
            if (sameStart < 0) break;
            if (!allowShiftForward) return false;
            startSample = snapStep > 0.0 ? SnapFloor(static_cast<double>(notes[sameStart].startSample + notes[sameStart].length), snapStep)
                                         : static_cast<double>(notes[sameStart].startSample + notes[sameStart].length);
        }

        int overlap = FindOverlappingNote(notes, pitch, startSample, -1);
        if (overlap >= 0) {
            if (!allowShiftForward) return false;
            double newLen = startSample - static_cast<double>(notes[overlap].startSample);
            if (newLen < minNoteLen) {
                newLen = minNoteLen;
                startSample = static_cast<double>(notes[overlap].startSample) + newLen;
            }
            notes[overlap].length = static_cast<uint64_t>(std::round(newLen));
        }

        double nextStart = GetNextNoteStart(notes, pitch, startSample, -1);
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

    bool CursorInRect(const UIContext& ui, float left, float right, float top, float bottom) {
        float x = static_cast<float>(ui.cursorX);
        float y = static_cast<float>(ui.cursorY);
        return x >= left && x <= right && y >= top && y <= bottom;
    }

    void EnsureResources(RendererContext& renderer, WorldContext& world) {
        if (!renderer.uiColorShader) {
            renderer.uiColorShader = std::make_unique<Shader>(world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(),
                                                             world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
        }
        if (renderer.uiPianoRollVAO == 0) {
            glGenVertexArrays(1, &renderer.uiPianoRollVAO);
            glGenBuffers(1, &renderer.uiPianoRollVBO);
        }
    }

    void ResetStateForClip(PianoRollState& state) {
        const PianoRollConfig& cfg = Config();
        state.scrollOffsetX = 0.0f;
        state.scrollOffsetY = 0.0f;
        state.timelineViewInitialized = false;
        state.timelineSecondsPerScreen = 0.0;
        state.activeNote = -1;
        state.activeNoteClip = -1;
        state.resizingNote = false;
        state.dragOffsetSamples = 0.0;
        state.lastNoteLengthSamples = 0.0;
        state.editMode = EditMode::Draw;
        state.painting = false;
        state.paintClipIndex = -1;
        state.paintLastX.assign(cfg.totalRows, -1.0);
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

    void BuildKeyLayout(PianoRollState& state, double screenWidth, double screenHeight) {
        const PianoRollConfig& cfg = Config();
        const int whiteNotes[7] = { 0, 2, 4, 5, 7, 9, 11 };
        const int whiteLaneIndex[7] = { 0, 2, 4, 5, 7, 9, 11 };
        const int whiteRowSpan[7] = { 2, 2, 1, 2, 2, 2, 1 };
        const int blackNotesByGap[6] = { 1, 3, -1, 6, 8, 10 };
        const int blackAnchorWhiteNote[6] = { 0, 2, -1, 5, 7, 9 };
        const bool blackAlignTop[6] = { true, true, true, true, true, true };

        float zoom = std::clamp(static_cast<float>(state.verticalZoom), 0.5f, 4.0f);
        float laneStep = std::max(6.0f, cfg.laneStep * zoom);
        float laneSpan = laneStep * 12.0f;
        float totalHeight = laneStep * static_cast<float>(cfg.totalRows);
        float topEdge = static_cast<float>((screenHeight - totalHeight) * 0.5f);
        float bottomEdge = topEdge + totalHeight;
        float baseOriginY = bottomEdge;
        float blackHeight = cfg.blackKeyHeight * zoom;

        state.whiteKeys.clear();
        state.blackKeys.clear();
        state.whiteKeys.reserve(cfg.keyCount * cfg.octaveCount);
        state.blackKeys.reserve((cfg.keyCount - 1) * cfg.octaveCount);

        for (int octaveIndex = 0; octaveIndex < cfg.octaveCount; ++octaveIndex) {
            float octaveOriginY = baseOriginY - octaveIndex * laneSpan;
            for (int i = 0; i < cfg.keyCount; ++i) {
                Key key;
                key.w = cfg.keyWidth;
                key.h = static_cast<float>(whiteRowSpan[i]) * laneStep;
                key.depth = cfg.keyDepth;
                key.x = cfg.leftBorderWidth;
                float bottomY = octaveOriginY - whiteLaneIndex[i] * laneStep;
                key.y = bottomY - key.h;
                key.note = whiteNotes[i];
                key.octave = octaveIndex + 1;
                state.whiteKeys.push_back(key);
            }
        }

        float blackX = cfg.leftBorderWidth + 6.25f;
        int gapCount = cfg.keyCount - 1;
        for (int octaveIndex = 0; octaveIndex < cfg.octaveCount; ++octaveIndex) {
            float octaveOriginY = baseOriginY - octaveIndex * laneSpan;
            for (int gapIndex = 0; gapIndex < gapCount; ++gapIndex) {
                if (gapIndex == 2) continue;
                Key key;
                key.w = cfg.blackKeyWidth;
                key.h = blackHeight;
                key.depth = cfg.blackKeyDepth;
                key.z = 15.0f;
                key.x = blackX;
                key.note = blackNotesByGap[gapIndex];
                key.octave = octaveIndex + 1;
                int anchorNote = blackAnchorWhiteNote[gapIndex];
                float anchorBottomY = octaveOriginY - anchorNote * laneStep;
                float anchorTopY = anchorBottomY - 2.0f * laneStep;
                float blackTopY = blackAlignTop[gapIndex] ? anchorTopY : (anchorTopY + laneStep);
                key.y = blackTopY;
                state.blackKeys.push_back(key);
            }
        }
    }

    std::string FormatButtonValue(const std::string& value) {
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

    double GetSnapSpacingSamples(const std::string& value, double beatSamples, double barSamples) {
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

    void UpdatePianoRollResources(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow* win) {
        PianoRollState& state = State();
        state.active = false;
        state.layoutReady = false;

        if (!baseSystem.ui || !baseSystem.renderer || !baseSystem.world || !baseSystem.midi || !baseSystem.daw || !win) return;
        UIContext& ui = *baseSystem.ui;
        MidiContext& midi = *baseSystem.midi;
        if (!ui.active || ui.loadingActive) return;
        if (!midi.pianoRollActive) {
            midi.activeNote.store(-1, std::memory_order_relaxed);
            midi.activeVelocity.store(0.0f, std::memory_order_relaxed);
            state.initialized = false;
            state.cachedTrack = -1;
            state.cachedClip = -1;
            state.timelineViewInitialized = false;
            return;
        }

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        EnsureResources(renderer, world);
        if (!renderer.uiColorShader) return;

        int trackIndex = midi.pianoRollTrack;
        int clipIndex = midi.pianoRollClipIndex;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(midi.tracks.size())) return;
        if (clipIndex < 0 || clipIndex >= static_cast<int>(midi.tracks[trackIndex].clips.size())) return;

        if (!state.cursorsLoaded) {
            state.cursorDefault = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
            state.cursorDraw = loadCursorImage("Procedures/assets/drawing_pencil.png", 0, 0);
            state.cursorBrush = loadCursorImage("Procedures/assets/drawing_brush.png", 0, 0);
            state.cursorMove = loadCursorImage("Procedures/assets/resize_a_cross.png", 16, 16);
            state.cursorResize = loadCursorImage("Procedures/assets/resize_a_horizontal.png", 16, 16);
            state.cursorsLoaded = true;
        }

        static int s_loggedTrack = -1;
        static int s_loggedClip = -1;
        if (s_loggedTrack != trackIndex || s_loggedClip != clipIndex) {
            s_loggedTrack = trackIndex;
            s_loggedClip = clipIndex;
            const MidiClip& clip = midi.tracks[trackIndex].clips[clipIndex];
            std::cerr << "PianoRoll open: track=" << trackIndex
                      << " clip=" << clipIndex
                      << " start=" << clip.startSample
                      << " len=" << clip.length
                      << " notes=" << clip.notes.size() << "\n";
        }

        if (state.cachedTrack != trackIndex || state.cachedClip != clipIndex || !state.initialized) {
            state.cachedTrack = trackIndex;
            state.cachedClip = clipIndex;
            ResetStateForClip(state);
            state.initialized = true;
        }

        state.active = true;
    }
}
