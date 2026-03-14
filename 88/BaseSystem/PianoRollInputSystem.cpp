#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace PianoRollResourceSystemLogic {
    struct PianoRollState;
    struct PianoRollConfig;
    struct PianoRollLayout;
    struct DeleteAnim;
    struct ToggleButton;
    enum class EditMode;
    enum class ScaleType;
    PianoRollState& State();
    const PianoRollConfig& Config();
    const std::array<const char*, 12>& NoteNames();
    const std::array<const char*, 7>& ModeNames();
    const std::vector<std::string>& SnapOptions();
    bool CursorInRect(const UIContext& ui, float left, float right, float top, float bottom);
    float Clamp01(float v);
    void NoteColor(int noteIndex, float& r, float& g, float& b);
    double SnapValue(double value, double snap);
    double SnapFloor(double value, double snap);
    bool IsScaleNote(int noteIndex, int root, ScaleType type, int mode);
    int AdjustRowToScale(int row, float mouseY, float gridOrigin, float laneStep, int totalRows, int root, ScaleType type, int mode);
    int FindNoteAtStart(const std::vector<MidiNote>& notes, int pitch, double startSample, int excludeIndex);
    int FindOverlappingNote(const std::vector<MidiNote>& notes, int pitch, double startSample, int excludeIndex);
    double GetNextNoteStart(const std::vector<MidiNote>& notes, int pitch, double startSample, int excludeIndex);
    bool PlaceNote(std::vector<MidiNote>& notes,
                   int pitch,
                   double& startSample,
                   double& noteLen,
                   double snapStep,
                   double minNoteLen,
                   bool allowShiftForward);
}

namespace PianoRollInputSystemLogic {
    namespace {
        void updateScaleButtonLabel(PianoRollResourceSystemLogic::PianoRollState& state) {
            if (state.scaleType == PianoRollResourceSystemLogic::ScaleType::None) {
                state.scaleButton.value = "none";
                return;
            }
            const auto& noteNames = PianoRollResourceSystemLogic::NoteNames();
            const auto& modeNames = PianoRollResourceSystemLogic::ModeNames();
            std::string name = noteNames[state.scaleRoot];
            name += modeNames[state.scaleMode % 7];
            if (state.scaleType == PianoRollResourceSystemLogic::ScaleType::Major) {
                name += " Major";
            } else if (state.scaleType == PianoRollResourceSystemLogic::ScaleType::HarmonicMinor) {
                name += " Harm";
            } else if (state.scaleType == PianoRollResourceSystemLogic::ScaleType::MelodicMinor) {
                name += " Mel";
            } else if (state.scaleType == PianoRollResourceSystemLogic::ScaleType::HungarianMinor) {
                name += " Hung";
            } else if (state.scaleType == PianoRollResourceSystemLogic::ScaleType::NeapolitanMajor) {
                name += " Neo";
            } else if (state.scaleType == PianoRollResourceSystemLogic::ScaleType::DoubleHarmonicMinor) {
                name += " Dbl";
            }
            state.scaleButton.value = name;
        }

        bool isInsideRect(float x, float y, float w, float h, float mx, float my) {
            return mx >= x && mx <= x + w && my >= y && my <= y + h;
        }
    }

    void UpdatePianoRollInput(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow* win) {
        PianoRollResourceSystemLogic::PianoRollState& state = PianoRollResourceSystemLogic::State();
        if (!state.active || !state.layoutReady) return;
        if (!baseSystem.ui || !baseSystem.midi || !win) return;

        UIContext& ui = *baseSystem.ui;
        MidiContext& midi = *baseSystem.midi;

        int trackIndex = state.layout.trackIndex;
        int clipIndex = state.layout.clipIndex;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(midi.tracks.size())) return;
        if (clipIndex < 0 || clipIndex >= static_cast<int>(midi.tracks[trackIndex].clips.size())) return;
        auto& laneClips = midi.tracks[trackIndex].clips;

        const auto& cfg = PianoRollResourceSystemLogic::Config();
        const auto& layout = state.layout;

        float closeLeft = layout.closeLeft;
        float closeTop = layout.closeTop;
        float closeSize = layout.closeSize;

        int mouseDown = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT);
        int rightDown = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT);
        bool mousePressedThisFrame = (mouseDown == GLFW_PRESS) && !state.wasMouseDown;
        bool mouseReleasedThisFrame = (mouseDown == GLFW_RELEASE) && state.wasMouseDown;
        double mouseX = ui.cursorX;
        double mouseY = ui.cursorY;

        if (mouseReleasedThisFrame) {
            if (PianoRollResourceSystemLogic::CursorInRect(ui, closeLeft, closeLeft + closeSize, closeTop, closeTop + closeSize)) {
                midi.pianoRollActive = false;
                midi.pianoRollTrack = -1;
                midi.pianoRollClipIndex = -1;
                midi.activeNote.store(-1, std::memory_order_relaxed);
                midi.activeVelocity.store(0.0f, std::memory_order_relaxed);
                state.activeNote = -1;
                state.activeNoteClip = -1;
                state.painting = false;
                state.paintClipIndex = -1;
                if (state.cursorDefault) {
                    glfwSetCursor(win, state.cursorDefault);
                    state.currentCursor = state.cursorDefault;
                }
                ui.consumeClick = true;
                return;
            }
        }

        float gridLeft = layout.gridLeft;
        float gridRight = layout.gridRight;
        float viewTop = layout.viewTop;
        float viewBottom = layout.viewBottom;
        bool shiftDown = (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
        bool altDown = (glfwGetKey(win, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
        if (ui.mainScrollDelta != 0.0) {
            bool inTimelineX = (mouseX >= gridLeft && mouseX <= gridRight);
            bool inRulerBand = inTimelineX
                && (mouseY >= 0.0)
                && (mouseY <= static_cast<double>(cfg.borderHeight));
            bool inGridBand = inTimelineX
                && (mouseY >= static_cast<double>(viewTop))
                && (mouseY <= static_cast<double>(viewBottom));
            if (shiftDown) {
                state.scrollOffsetX -= static_cast<float>(ui.mainScrollDelta * 40.0);
            } else if (inRulerBand) {
                double pxPerSampleSafe = std::max(1e-9, layout.pxPerSample);
                double sampleRate = 44100.0;
                if (layout.secondsPerBeat > 0.0) {
                    sampleRate = layout.beatSamples / layout.secondsPerBeat;
                }
                if (sampleRate <= 0.0) sampleRate = 44100.0;
                double currentSeconds = (state.timelineSecondsPerScreen > 0.0)
                    ? state.timelineSecondsPerScreen
                    : (layout.samplesPerScreen / sampleRate);
                currentSeconds = std::clamp(currentSeconds, 0.5, 120.0);
                double zoomFactor = (ui.mainScrollDelta > 0.0) ? 1.1 : (1.0 / 1.1);
                double newSeconds = std::clamp(currentSeconds * zoomFactor, 0.5, 120.0);
                double cursorSample = (mouseX - gridLeft - state.scrollOffsetX) / pxPerSampleSafe;
                if (cursorSample < 0.0) cursorSample = 0.0;
                state.timelineSecondsPerScreen = newSeconds;
                double newSamplesPerScreen = std::max(1.0, newSeconds * sampleRate);
                double newPxPerSample = layout.gridWidth / newSamplesPerScreen;
                state.scrollOffsetX = static_cast<float>(mouseX - gridLeft - cursorSample * newPxPerSample);
            } else if (altDown && inGridBand) {
                double oldZoom = std::clamp(state.verticalZoom, 0.5, 4.0);
                double zoomFactor = (ui.mainScrollDelta > 0.0) ? 1.1 : (1.0 / 1.1);
                double newZoom = std::clamp(oldZoom * zoomFactor, 0.5, 4.0);
                if (std::fabs(newZoom - oldZoom) > 1e-9) {
                    double oldStep = std::max(6.0, static_cast<double>(cfg.laneStep) * oldZoom);
                    double newStep = std::max(6.0, static_cast<double>(cfg.laneStep) * newZoom);
                    double totalRows = static_cast<double>(cfg.totalRows);
                    double baseOld = 0.5 * (layout.screenHeight + oldStep * totalRows);
                    double baseNew = 0.5 * (layout.screenHeight + newStep * totalRows);
                    double rowCoord = (baseOld + static_cast<double>(state.scrollOffsetY) - mouseY) / oldStep;
                    state.verticalZoom = newZoom;
                    state.scrollOffsetY = static_cast<float>(mouseY + rowCoord * newStep - baseNew);
                }
            } else {
                state.scrollOffsetY += static_cast<float>(ui.mainScrollDelta * 30.0);
            }
            ui.mainScrollDelta = 0.0;
        }
        state.scrollOffsetY = std::clamp(state.scrollOffsetY, layout.minScrollY, layout.maxScrollY);
        state.scrollOffsetX = std::clamp(state.scrollOffsetX, layout.minScrollX, layout.maxScrollX);

        double pxPerSample = layout.pxPerSample;
        float gridOrigin = layout.gridOrigin + state.scrollOffsetY;
        float gridStep = layout.gridStep;
        int totalRows = cfg.totalRows;

        auto clipIndexForGlobalSample = [&](double globalSample) -> int {
            for (int i = static_cast<int>(laneClips.size()) - 1; i >= 0; --i) {
                const auto& laneClip = laneClips[static_cast<size_t>(i)];
                if (laneClip.length == 0) continue;
                double start = static_cast<double>(laneClip.startSample);
                double end = start + static_cast<double>(laneClip.length);
                if (globalSample >= start && globalSample <= end) {
                    return i;
                }
            }
            return -1;
        };
        auto globalSampleFromMouseX = [&]() -> double {
            return (mouseX - gridLeft - state.scrollOffsetX) / pxPerSample;
        };

        bool buttonHit = isInsideRect(state.gridButton.x, state.gridButton.y, state.gridButton.w, state.gridButton.h, mouseX, mouseY);
        bool scaleButtonHit = isInsideRect(state.scaleButton.x, state.scaleButton.y, state.scaleButton.w, state.scaleButton.h, mouseX, mouseY);
        bool drawButtonHit = isInsideRect(state.modeDrawButton.x, state.modeDrawButton.y, state.modeDrawButton.w, state.modeDrawButton.h, mouseX, mouseY);
        bool paintButtonHit = isInsideRect(state.modePaintButton.x, state.modePaintButton.y, state.modePaintButton.w, state.modePaintButton.h, mouseX, mouseY);

        int pDown = glfwGetKey(win, GLFW_KEY_P);
        int bDown = glfwGetKey(win, GLFW_KEY_B);
        if (pDown == GLFW_PRESS && !state.wasPDown) {
            state.editMode = PianoRollResourceSystemLogic::EditMode::Draw;
        }
        if (bDown == GLFW_PRESS && !state.wasBDown) {
            state.editMode = PianoRollResourceSystemLogic::EditMode::Paint;
        }
        state.wasPDown = (pDown == GLFW_PRESS);
        state.wasBDown = (bDown == GLFW_PRESS);

        state.modeDrawButton.isToggled = (state.editMode == PianoRollResourceSystemLogic::EditMode::Draw);
        state.modePaintButton.isToggled = (state.editMode == PianoRollResourceSystemLogic::EditMode::Paint);
        state.scaleButton.isToggled = (state.scaleType != PianoRollResourceSystemLogic::ScaleType::None);

        updateScaleButtonLabel(state);

        float menuX = state.gridButton.x;
        float menuY = state.gridButton.y + state.gridButton.h + 6.0f;
        float menuW = 140.0f;
        float menuPadding = 6.0f;
        float menuRowHeight = 18.0f;
        const auto& snapOptions = PianoRollResourceSystemLogic::SnapOptions();
        float menuH = static_cast<float>(snapOptions.size()) * menuRowHeight + menuPadding * 2.0f;
        bool menuHit = isInsideRect(menuX, menuY, menuW, menuH, mouseX, mouseY);

        state.hoverIndex = -1;
        if (state.menuOpen && menuHit) {
            float localY = static_cast<float>(mouseY) - menuY - menuPadding;
            int index = static_cast<int>(localY / menuRowHeight);
            if (index >= 0 && index < static_cast<int>(snapOptions.size())) {
                state.hoverIndex = index;
            }
        }

        float scaleMenuX = state.scaleButton.x;
        float scaleMenuY = state.scaleButton.y + state.scaleButton.h + 6.0f;
        float scaleMenuW = 240.0f;
        float scaleMenuH = 150.0f;
        bool scaleMenuHit = isInsideRect(scaleMenuX, scaleMenuY, scaleMenuW, scaleMenuH, mouseX, mouseY);

        state.hoverScaleColumn = -1;
        state.hoverScaleRow = -1;
        if (state.scaleMenuOpen && scaleMenuHit) {
            float columnWidth = scaleMenuW / 3.0f;
            int column = static_cast<int>((mouseX - scaleMenuX) / columnWidth);
            float localY = static_cast<float>(mouseY) - scaleMenuY - menuPadding;
            int index = static_cast<int>(localY / menuRowHeight);
            if (column >= 0 && column < 3 && index >= 0 && index < 8) {
                state.hoverScaleColumn = column;
                state.hoverScaleRow = index - 1;
            }
        }

        if (state.menuOpen) {
            if (mousePressedThisFrame) {
                if (menuHit && state.hoverIndex >= 0) {
                    state.gridButton.value = snapOptions[state.hoverIndex];
                    state.gridButton.isToggled = (state.gridButton.value != "none");
                    state.menuOpen = false;
                } else if (!buttonHit) {
                    state.menuOpen = false;
                }
            }
        } else if (state.scaleMenuOpen) {
            if (mousePressedThisFrame) {
                if (scaleMenuHit && state.hoverScaleColumn >= 0) {
                    if (state.hoverScaleColumn == 0) {
                        state.scaleRoot = state.hoverScaleRow;
                    } else if (state.hoverScaleColumn == 1) {
                        if (state.hoverScaleRow == 0) {
                            state.scaleType = PianoRollResourceSystemLogic::ScaleType::None;
                        } else if (state.hoverScaleRow == 1) {
                            state.scaleType = PianoRollResourceSystemLogic::ScaleType::Major;
                        } else if (state.hoverScaleRow == 2) {
                            state.scaleType = PianoRollResourceSystemLogic::ScaleType::HarmonicMinor;
                        } else if (state.hoverScaleRow == 3) {
                            state.scaleType = PianoRollResourceSystemLogic::ScaleType::MelodicMinor;
                        } else if (state.hoverScaleRow == 4) {
                            state.scaleType = PianoRollResourceSystemLogic::ScaleType::HungarianMinor;
                        } else if (state.hoverScaleRow == 5) {
                            state.scaleType = PianoRollResourceSystemLogic::ScaleType::NeapolitanMajor;
                        } else if (state.hoverScaleRow == 6) {
                            state.scaleType = PianoRollResourceSystemLogic::ScaleType::DoubleHarmonicMinor;
                        }
                    } else if (state.hoverScaleColumn == 2) {
                        state.scaleMode = state.hoverScaleRow;
                    }
                    state.scaleMenuOpen = false;
                    updateScaleButtonLabel(state);
                } else if (!scaleButtonHit) {
                    state.scaleMenuOpen = false;
                }
            }
        } else if (mousePressedThisFrame) {
            if (buttonHit) {
                state.menuOpen = true;
                state.scaleMenuOpen = false;
            } else if (scaleButtonHit) {
                state.scaleMenuOpen = true;
                state.menuOpen = false;
            } else if (drawButtonHit) {
                state.editMode = PianoRollResourceSystemLogic::EditMode::Draw;
            } else if (paintButtonHit) {
                state.editMode = PianoRollResourceSystemLogic::EditMode::Paint;
            }
        }

        bool suppressKeyPress = (state.menuOpen || state.scaleMenuOpen || buttonHit || scaleButtonHit || drawButtonHit || paintButtonHit);

        int pressedIndex = -1;
        int blackPressedIndex = -1;
        if (mouseDown == GLFW_PRESS && !suppressKeyPress) {
            for (int i = 0; i < static_cast<int>(state.blackKeys.size()); ++i) {
                const auto& key = state.blackKeys[i];
                if (isInsideRect(key.x, key.y + state.scrollOffsetY, key.w, key.h, mouseX, mouseY)) {
                    blackPressedIndex = i;
                    break;
                }
            }
            if (blackPressedIndex < 0) {
                for (int i = 0; i < static_cast<int>(state.whiteKeys.size()); ++i) {
                    const auto& key = state.whiteKeys[i];
                    if (isInsideRect(key.x, key.y + state.scrollOffsetY, key.w, key.h, mouseX, mouseY)) {
                        pressedIndex = i;
                        break;
                    }
                }
            }
        }

        int livePianoNote = -1;
        if (blackPressedIndex >= 0 && blackPressedIndex < static_cast<int>(state.blackKeys.size())) {
            const auto& key = state.blackKeys[static_cast<size_t>(blackPressedIndex)];
            livePianoNote = key.note + (key.octave + 1) * 12;
        } else if (pressedIndex >= 0 && pressedIndex < static_cast<int>(state.whiteKeys.size())) {
            const auto& key = state.whiteKeys[static_cast<size_t>(pressedIndex)];
            livePianoNote = key.note + (key.octave + 1) * 12;
        }
        if (livePianoNote < 0 || livePianoNote > 127) {
            livePianoNote = -1;
        }
        midi.activeNote.store(livePianoNote, std::memory_order_relaxed);
        midi.activeVelocity.store(livePianoNote >= 0 ? 0.8f : 0.0f, std::memory_order_relaxed);

        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - state.lastTime);
        state.lastTime = currentTime;
        float animSpeed = (cfg.pressDuration > 0.0f) ? (1.0f / cfg.pressDuration) : 1.0f;

        for (int i = 0; i < static_cast<int>(state.whiteKeys.size()); ++i) {
            state.whiteKeys[i].isPressed = (i == pressedIndex);
            float target = state.whiteKeys[i].isPressed ? 0.5f : 0.0f;
            if (state.whiteKeys[i].pressAnim < target) {
                state.whiteKeys[i].pressAnim = std::min(target, state.whiteKeys[i].pressAnim + animSpeed * deltaTime);
            } else if (state.whiteKeys[i].pressAnim > target) {
                state.whiteKeys[i].pressAnim = std::max(target, state.whiteKeys[i].pressAnim - animSpeed * deltaTime);
            }
        }
        for (int i = 0; i < static_cast<int>(state.blackKeys.size()); ++i) {
            state.blackKeys[i].isPressed = (i == blackPressedIndex);
            float target = state.blackKeys[i].isPressed ? 0.5f : 0.0f;
            if (state.blackKeys[i].pressAnim < target) {
                state.blackKeys[i].pressAnim = std::min(target, state.blackKeys[i].pressAnim + animSpeed * deltaTime);
            } else if (state.blackKeys[i].pressAnim > target) {
                state.blackKeys[i].pressAnim = std::max(target, state.blackKeys[i].pressAnim - animSpeed * deltaTime);
            }
        }

        auto pulseButton = [&](PianoRollResourceSystemLogic::ToggleButton& button) {
            float target = button.isPressed ? 0.5f : 0.0f;
            if (button.pressAnim < target) {
                button.pressAnim = std::min(target, button.pressAnim + animSpeed * deltaTime);
            } else if (button.pressAnim > target) {
                button.pressAnim = std::max(target, button.pressAnim - animSpeed * deltaTime);
            }
        };
        pulseButton(state.gridButton);
        pulseButton(state.modeDrawButton);
        pulseButton(state.modePaintButton);
        pulseButton(state.scaleButton);

        bool inGridArea = (mouseX >= gridLeft && mouseX <= gridRight && mouseY >= viewTop && mouseY <= viewBottom);
        int mouseRow = -1;
        if (inGridArea) {
            mouseRow = static_cast<int>((gridOrigin - mouseY) / gridStep);
            if (mouseRow < 0 || mouseRow >= totalRows) mouseRow = -1;
        }

        int startRow = static_cast<int>(std::floor((gridOrigin - viewBottom) / gridStep));
        int endRow = static_cast<int>(std::ceil((gridOrigin - viewTop) / gridStep));
        if (startRow < 0) startRow = 0;
        if (endRow > totalRows - 1) endRow = totalRows - 1;

        int hoverNote = -1;
        int hoverClip = -1;
        if (inGridArea) {
            for (int row = startRow; row <= endRow; ++row) {
                if (row < 0 || row >= totalRows) continue;
                int pitch = 24 + row;
                float ny = gridOrigin - (row + 1) * gridStep;
                for (int ci = 0; ci < static_cast<int>(laneClips.size()); ++ci) {
                    const auto& laneClip = laneClips[static_cast<size_t>(ci)];
                    if (laneClip.length == 0) continue;
                    float laneClipStartX = gridLeft + state.scrollOffsetX
                        + static_cast<float>(static_cast<double>(laneClip.startSample) * pxPerSample);
                    for (int i = 0; i < static_cast<int>(laneClip.notes.size()); ++i) {
                        const MidiNote& note = laneClip.notes[static_cast<size_t>(i)];
                        if (note.pitch != pitch) continue;
                        float nx = laneClipStartX + static_cast<float>(note.startSample * pxPerSample);
                        float nw = std::max(2.0f, static_cast<float>(note.length * pxPerSample));
                        if (isInsideRect(nx, ny, nw, gridStep, mouseX, mouseY)) {
                            hoverNote = i;
                            hoverClip = ci;
                            break;
                        }
                    }
                    if (hoverNote >= 0) {
                        break;
                    }
                }
                if (hoverNote >= 0) break;
            }
        }

        GLFWcursor* desiredCursor = state.cursorDefault;
        if (!state.menuOpen && !state.scaleMenuOpen && inGridArea) {
            if (hoverNote >= 0 && hoverClip >= 0 && hoverClip < static_cast<int>(laneClips.size())) {
                const auto& hoverLaneClip = laneClips[static_cast<size_t>(hoverClip)];
                if (hoverNote >= static_cast<int>(hoverLaneClip.notes.size())) hoverNote = -1;
            }
            if (hoverNote >= 0 && hoverClip >= 0 && hoverClip < static_cast<int>(laneClips.size())) {
                const auto& hoverLaneClip = laneClips[static_cast<size_t>(hoverClip)];
                float noteStartX = gridLeft + state.scrollOffsetX
                    + static_cast<float>(static_cast<double>(hoverLaneClip.startSample) * pxPerSample);
                float nx = noteStartX + static_cast<float>(hoverLaneClip.notes[static_cast<size_t>(hoverNote)].startSample * pxPerSample);
                float nw = std::max(2.0f, static_cast<float>(hoverLaneClip.notes[static_cast<size_t>(hoverNote)].length * pxPerSample));
                float handleX = nx + nw - cfg.noteHandleSize;
                if (mouseX >= handleX) {
                    desiredCursor = state.cursorResize ? state.cursorResize : state.cursorDefault;
                } else {
                    desiredCursor = state.cursorMove ? state.cursorMove : state.cursorDefault;
                }
            } else if (state.editMode == PianoRollResourceSystemLogic::EditMode::Draw && state.cursorDraw) {
                desiredCursor = state.cursorDraw;
            } else if (state.editMode == PianoRollResourceSystemLogic::EditMode::Paint && state.cursorBrush) {
                desiredCursor = state.cursorBrush;
            }
        }

        if (desiredCursor && desiredCursor != state.currentCursor) {
            glfwSetCursor(win, desiredCursor);
            state.currentCursor = desiredCursor;
        }

        if (mousePressedThisFrame && inGridArea && !suppressKeyPress) {
            state.activeNote = -1;
            state.activeNoteClip = -1;
            state.resizingNote = false;
            if (hoverNote >= 0 && hoverClip >= 0 && hoverClip < static_cast<int>(laneClips.size())) {
                const auto& targetClip = laneClips[static_cast<size_t>(hoverClip)];
                if (hoverNote >= 0 && hoverNote < static_cast<int>(targetClip.notes.size())) {
                    state.activeNote = hoverNote;
                    state.activeNoteClip = hoverClip;
                    float noteStartX = gridLeft + state.scrollOffsetX
                        + static_cast<float>(static_cast<double>(targetClip.startSample) * pxPerSample);
                    float nx = noteStartX + static_cast<float>(targetClip.notes[static_cast<size_t>(hoverNote)].startSample * pxPerSample);
                    float nw = std::max(2.0f, static_cast<float>(targetClip.notes[static_cast<size_t>(hoverNote)].length * pxPerSample));
                    if (mouseX > nx + nw - cfg.noteHandleSize) {
                        state.resizingNote = true;
                    } else {
                        state.resizingNote = false;
                        state.dragOffsetSamples = (mouseX - nx) / pxPerSample;
                    }
                }
            }
            if (state.editMode == PianoRollResourceSystemLogic::EditMode::Paint && state.activeNote == -1 && mouseRow >= 0) {
                int targetClipIndex = clipIndexForGlobalSample(globalSampleFromMouseX());
                if (targetClipIndex >= 0 && targetClipIndex < static_cast<int>(laneClips.size())) {
                    state.painting = true;
                    state.paintClipIndex = targetClipIndex;
                    state.paintLastX.assign(totalRows, -1.0);
                    state.paintLastXGlobal = -1.0;
                    state.paintLastRow = mouseRow;
                    state.paintLastCursorX = -1.0;
                    state.paintDir = 0;
                } else {
                    state.painting = false;
                    state.paintClipIndex = -1;
                }
            } else {
                state.painting = false;
                state.paintClipIndex = -1;
            }
            if (state.editMode == PianoRollResourceSystemLogic::EditMode::Draw && state.activeNote == -1 && mouseRow >= 0) {
                int targetClipIndex = clipIndexForGlobalSample(globalSampleFromMouseX());
                if (targetClipIndex >= 0 && targetClipIndex < static_cast<int>(laneClips.size())) {
                    MidiClip& targetClip = laneClips[static_cast<size_t>(targetClipIndex)];
                    double localX = globalSampleFromMouseX() - static_cast<double>(targetClip.startSample);
                    double snappedX = layout.snapSamples > 0.0 ? PianoRollResourceSystemLogic::SnapFloor(localX, layout.snapSamples) : localX;
                    double defaultLen = layout.defaultStepSamples;
                    double noteLen = (state.lastNoteLengthSamples > 0.0) ? state.lastNoteLengthSamples : defaultLen;
                    int targetRow = PianoRollResourceSystemLogic::AdjustRowToScale(mouseRow, mouseY, gridOrigin, gridStep, totalRows, state.scaleRoot, state.scaleType, state.scaleMode);
                    int pitch = 24 + targetRow;
                    if (pitch >= 0 && pitch <= 127) {
                        double startSample = snappedX;
                        double len = noteLen;
                        if (startSample < 0.0) startSample = 0.0;
                        if (startSample + len > static_cast<double>(targetClip.length)) {
                            len = static_cast<double>(targetClip.length) - startSample;
                        }
                        if (len >= layout.minNoteLenSamples
                            && PianoRollResourceSystemLogic::PlaceNote(targetClip.notes, pitch, startSample, len, layout.snapSamples, layout.minNoteLenSamples, true)) {
                            state.activeNote = static_cast<int>(targetClip.notes.size()) - 1;
                            state.activeNoteClip = targetClipIndex;
                            state.resizingNote = false;
                            state.dragOffsetSamples = (mouseX - (gridLeft + state.scrollOffsetX
                                + static_cast<float>((static_cast<double>(targetClip.startSample) + startSample) * pxPerSample))) / pxPerSample;
                            state.lastNoteLengthSamples = len;
                        }
                    }
                }
            }
        }

        if (mouseDown == GLFW_PRESS && state.painting && inGridArea && mouseRow >= 0 && hoverNote < 0) {
            if (state.paintClipIndex < 0 || state.paintClipIndex >= static_cast<int>(laneClips.size())) {
                state.painting = false;
            } else {
                MidiClip& paintClip = laneClips[static_cast<size_t>(state.paintClipIndex)];
                double localX = globalSampleFromMouseX() - static_cast<double>(paintClip.startSample);
                double defaultLen = layout.defaultStepSamples;
                double noteLen = (state.lastNoteLengthSamples > 0.0) ? state.lastNoteLengthSamples : defaultLen;
                if (noteLen < layout.minNoteLenSamples) noteLen = layout.minNoteLenSamples;

                double lastX = state.paintLastX[static_cast<size_t>(mouseRow)];
                double lastXAny = state.paintLastXGlobal;
                double startSample = -1.0;

                if (layout.snapSamples > 0.0) {
                    double targetSample = PianoRollResourceSystemLogic::SnapFloor(localX, layout.snapSamples);
                    if (lastX < 0.0) {
                        startSample = targetSample;
                    } else if (std::fabs(targetSample - lastX) >= noteLen) {
                        startSample = targetSample;
                    }
                } else {
                    if (lastXAny < 0.0) {
                        startSample = localX;
                    } else {
                        double baseSample = (lastX >= 0.0) ? lastX : lastXAny;
                        double delta = localX - baseSample;
                        if (std::fabs(delta) >= noteLen) {
                            int steps = static_cast<int>(std::floor(std::fabs(delta) / noteLen));
                            if (steps > 0) {
                                startSample = baseSample + ((delta > 0.0) ? static_cast<double>(steps) * noteLen
                                                                           : -static_cast<double>(steps) * noteLen);
                            }
                        }
                    }
                }

                if (startSample >= 0.0) {
                    double placeSample = startSample;
                    double len = noteLen;
                    bool allowShiftForward = true;
                    if (layout.snapSamples <= 0.0 && lastXAny >= 0.0) {
                        double baseSample = (lastX >= 0.0) ? lastX : lastXAny;
                        if (localX < baseSample) {
                            allowShiftForward = false;
                        }
                    }

                    if (placeSample < 0.0) placeSample = 0.0;
                    if (placeSample + len > static_cast<double>(paintClip.length)) {
                        len = static_cast<double>(paintClip.length) - placeSample;
                    }
                    if (len >= layout.minNoteLenSamples) {
                        auto paintRowAtSample = [&](int row) {
                            if (row < 0 || row >= totalRows) return;
                            int noteIndex = row % 12;
                            if (noteIndex < 0) noteIndex += 12;
                            if (!PianoRollResourceSystemLogic::IsScaleNote(noteIndex, state.scaleRoot, state.scaleType, state.scaleMode)) {
                                return;
                            }
                            double rowLastX = state.paintLastX[static_cast<size_t>(row)];
                            double spacing = (layout.snapSamples > 0.0) ? len : std::max(1.0, len);
                            if (rowLastX >= 0.0 && std::fabs(placeSample - rowLastX) < spacing * 0.5) {
                                return;
                            }
                            int pitch = 24 + row;
                            if (pitch < 0 || pitch > 127) return;
                            double rowSample = placeSample;
                            double rowLen = len;
                            if (PianoRollResourceSystemLogic::PlaceNote(paintClip.notes, pitch, rowSample, rowLen, layout.snapSamples, layout.minNoteLenSamples, allowShiftForward)) {
                                state.paintLastX[static_cast<size_t>(row)] = rowSample;
                                state.paintLastXGlobal = rowSample;
                                state.lastNoteLengthSamples = rowLen;
                            }
                        };

                        if (state.paintLastRow >= 0 && state.paintLastRow != mouseRow) {
                            int step = (mouseRow > state.paintLastRow) ? 1 : -1;
                            for (int row = state.paintLastRow + step; row != mouseRow + step; row += step) {
                                paintRowAtSample(row);
                            }
                        } else {
                            paintRowAtSample(mouseRow);
                        }
                        state.paintLastRow = mouseRow;
                    }
                }
            }
        }

        if (state.painting && inGridArea) {
            if (state.paintClipIndex >= 0 && state.paintClipIndex < static_cast<int>(laneClips.size())) {
                state.paintLastCursorX = globalSampleFromMouseX() - static_cast<double>(laneClips[static_cast<size_t>(state.paintClipIndex)].startSample);
            } else {
                state.paintLastCursorX = -1.0;
            }
        }

        if (mouseDown == GLFW_PRESS
            && state.activeNoteClip >= 0
            && state.activeNoteClip < static_cast<int>(laneClips.size())
            && state.activeNote >= 0
            && state.activeNote < static_cast<int>(laneClips[static_cast<size_t>(state.activeNoteClip)].notes.size())) {
            MidiClip& activeClip = laneClips[static_cast<size_t>(state.activeNoteClip)];
            double localX = globalSampleFromMouseX() - static_cast<double>(activeClip.startSample);
            if (state.resizingNote) {
                double newLen = layout.snapSamples > 0.0 ? PianoRollResourceSystemLogic::SnapValue(localX - static_cast<double>(activeClip.notes[static_cast<size_t>(state.activeNote)].startSample), layout.snapSamples)
                                                       : (localX - static_cast<double>(activeClip.notes[static_cast<size_t>(state.activeNote)].startSample));
                double nextStart = PianoRollResourceSystemLogic::GetNextNoteStart(activeClip.notes, activeClip.notes[static_cast<size_t>(state.activeNote)].pitch, static_cast<double>(activeClip.notes[static_cast<size_t>(state.activeNote)].startSample), state.activeNote);
                if (nextStart >= 0.0 && static_cast<double>(activeClip.notes[static_cast<size_t>(state.activeNote)].startSample) + newLen > nextStart) {
                    newLen = layout.snapSamples > 0.0 ? PianoRollResourceSystemLogic::SnapValue(nextStart - static_cast<double>(activeClip.notes[static_cast<size_t>(state.activeNote)].startSample), layout.snapSamples)
                                                      : (nextStart - static_cast<double>(activeClip.notes[static_cast<size_t>(state.activeNote)].startSample));
                }
                if (static_cast<double>(activeClip.notes[static_cast<size_t>(state.activeNote)].startSample) + newLen > static_cast<double>(activeClip.length)) {
                    newLen = std::max(layout.minSnapLenSamples, static_cast<double>(activeClip.length) - static_cast<double>(activeClip.notes[static_cast<size_t>(state.activeNote)].startSample));
                }
                activeClip.notes[static_cast<size_t>(state.activeNote)].length = static_cast<uint64_t>(std::round(newLen));
                state.lastNoteLengthSamples = newLen;
            } else {
                double snappedX = layout.snapSamples > 0.0 ? PianoRollResourceSystemLogic::SnapFloor(localX - state.dragOffsetSamples, layout.snapSamples)
                                                           : (localX - state.dragOffsetSamples);
                int targetRow = mouseRow >= 0 ? mouseRow : (activeClip.notes[static_cast<size_t>(state.activeNote)].pitch - 24);
                targetRow = PianoRollResourceSystemLogic::AdjustRowToScale(targetRow, mouseY, gridOrigin, gridStep, totalRows, state.scaleRoot, state.scaleType, state.scaleMode);
                int pitch = 24 + targetRow;
                if (pitch >= 0 && pitch <= 127) {
                    int sameStart = PianoRollResourceSystemLogic::FindNoteAtStart(activeClip.notes, pitch, snappedX, state.activeNote);
                    if (sameStart >= 0) {
                        snappedX = layout.snapSamples > 0.0
                            ? PianoRollResourceSystemLogic::SnapFloor(static_cast<double>(activeClip.notes[static_cast<size_t>(sameStart)].startSample + activeClip.notes[static_cast<size_t>(sameStart)].length), layout.snapSamples)
                            : static_cast<double>(activeClip.notes[static_cast<size_t>(sameStart)].startSample + activeClip.notes[static_cast<size_t>(sameStart)].length);
                    }
                    if (snappedX + static_cast<double>(activeClip.notes[static_cast<size_t>(state.activeNote)].length) > static_cast<double>(activeClip.length)) {
                        snappedX = std::max(0.0, static_cast<double>(activeClip.length) - static_cast<double>(activeClip.notes[static_cast<size_t>(state.activeNote)].length));
                    }
                    activeClip.notes[static_cast<size_t>(state.activeNote)].startSample = static_cast<uint64_t>(std::round(snappedX));
                    activeClip.notes[static_cast<size_t>(state.activeNote)].pitch = pitch;
                }
            }
        }

        if (mouseReleasedThisFrame
            && state.activeNoteClip >= 0
            && state.activeNoteClip < static_cast<int>(laneClips.size())
            && state.activeNote >= 0
            && state.activeNote < static_cast<int>(laneClips[static_cast<size_t>(state.activeNoteClip)].notes.size())
            && !state.resizingNote) {
            MidiClip& activeClip = laneClips[static_cast<size_t>(state.activeNoteClip)];
            double localX = globalSampleFromMouseX() - static_cast<double>(activeClip.startSample);
            double snappedX = layout.snapSamples > 0.0 ? PianoRollResourceSystemLogic::SnapFloor(localX - state.dragOffsetSamples, layout.snapSamples)
                                                       : (localX - state.dragOffsetSamples);
            int targetRow = mouseRow >= 0 ? mouseRow : (activeClip.notes[static_cast<size_t>(state.activeNote)].pitch - 24);
            targetRow = PianoRollResourceSystemLogic::AdjustRowToScale(targetRow, mouseY, gridOrigin, gridStep, totalRows, state.scaleRoot, state.scaleType, state.scaleMode);
            int pitch = 24 + targetRow;
            if (pitch >= 0 && pitch <= 127) {
                int sameStart = PianoRollResourceSystemLogic::FindNoteAtStart(activeClip.notes, pitch, snappedX, state.activeNote);
                if (sameStart >= 0) {
                    snappedX = layout.snapSamples > 0.0
                        ? PianoRollResourceSystemLogic::SnapFloor(static_cast<double>(activeClip.notes[static_cast<size_t>(sameStart)].startSample + activeClip.notes[static_cast<size_t>(sameStart)].length), layout.snapSamples)
                        : static_cast<double>(activeClip.notes[static_cast<size_t>(sameStart)].startSample + activeClip.notes[static_cast<size_t>(sameStart)].length);
                }
                int overlap = PianoRollResourceSystemLogic::FindOverlappingNote(activeClip.notes, pitch, snappedX, state.activeNote);
                if (overlap >= 0) {
                    double newLen = snappedX - static_cast<double>(activeClip.notes[static_cast<size_t>(overlap)].startSample);
                    if (newLen < layout.minNoteLenSamples) {
                        newLen = layout.minNoteLenSamples;
                        snappedX = static_cast<double>(activeClip.notes[static_cast<size_t>(overlap)].startSample) + newLen;
                    }
                    activeClip.notes[static_cast<size_t>(overlap)].length = static_cast<uint64_t>(std::round(newLen));
                }
                double nextStart = PianoRollResourceSystemLogic::GetNextNoteStart(activeClip.notes, pitch, snappedX, state.activeNote);
                double maxLen = static_cast<double>(activeClip.notes[static_cast<size_t>(state.activeNote)].length);
                if (nextStart >= 0.0 && snappedX + maxLen > nextStart) {
                    maxLen = nextStart - snappedX;
                }
                if (snappedX + maxLen > static_cast<double>(activeClip.length)) {
                    maxLen = std::max(layout.minNoteLenSamples, static_cast<double>(activeClip.length) - snappedX);
                }
                activeClip.notes[static_cast<size_t>(state.activeNote)].startSample = static_cast<uint64_t>(std::round(snappedX));
                activeClip.notes[static_cast<size_t>(state.activeNote)].pitch = pitch;
                activeClip.notes[static_cast<size_t>(state.activeNote)].length = static_cast<uint64_t>(std::round(maxLen));
            }
            state.activeNote = -1;
            state.activeNoteClip = -1;
            state.resizingNote = false;
            state.painting = false;
            state.paintClipIndex = -1;
            state.paintLastRow = -1;
            state.paintLastCursorX = -1.0;
            state.paintDir = 0;
        }

        if (rightDown == GLFW_PRESS && inGridArea && !suppressKeyPress) {
            int deleteIndex = -1;
            int deleteClipIndex = -1;
            int deleteRow = -1;
            const float deletePadX = 6.0f;
            const float deletePadY = 4.0f;
            for (int row = startRow; row <= endRow; ++row) {
                if (row < 0 || row >= totalRows) continue;
                int pitch = 24 + row;
                float ny = gridOrigin - (row + 1) * gridStep;
                for (int ci = 0; ci < static_cast<int>(laneClips.size()); ++ci) {
                    const auto& laneClip = laneClips[static_cast<size_t>(ci)];
                    if (laneClip.length == 0) continue;
                    float noteStartX = gridLeft + state.scrollOffsetX
                        + static_cast<float>(static_cast<double>(laneClip.startSample) * pxPerSample);
                    for (int i = 0; i < static_cast<int>(laneClip.notes.size()); ++i) {
                        const MidiNote& note = laneClip.notes[static_cast<size_t>(i)];
                        if (note.pitch != pitch) continue;
                        float nx = noteStartX + static_cast<float>(note.startSample * pxPerSample);
                        float nw = std::max(2.0f, static_cast<float>(note.length * pxPerSample));
                        if (isInsideRect(nx - deletePadX, ny - deletePadY,
                                         nw + 2.0f * deletePadX, gridStep + 2.0f * deletePadY,
                                         mouseX, mouseY)) {
                            deleteIndex = i;
                            deleteClipIndex = ci;
                            deleteRow = row;
                            break;
                        }
                    }
                    if (deleteIndex >= 0) {
                        break;
                    }
                }
                if (deleteIndex >= 0) break;
            }
            if (deleteIndex >= 0 && deleteClipIndex >= 0 && deleteClipIndex < static_cast<int>(laneClips.size())) {
                MidiClip& deleteClip = laneClips[static_cast<size_t>(deleteClipIndex)];
                if (deleteIndex < 0 || deleteIndex >= static_cast<int>(deleteClip.notes.size())) {
                    deleteIndex = -1;
                }
            }
            if (deleteIndex >= 0 && deleteClipIndex >= 0 && deleteClipIndex < static_cast<int>(laneClips.size())) {
                MidiClip& deleteClip = laneClips[static_cast<size_t>(deleteClipIndex)];
                float noteStartX = gridLeft + state.scrollOffsetX
                    + static_cast<float>(static_cast<double>(deleteClip.startSample) * pxPerSample);
                float nx = noteStartX + static_cast<float>(deleteClip.notes[static_cast<size_t>(deleteIndex)].startSample * pxPerSample);
                float nw = std::max(2.0f, static_cast<float>(deleteClip.notes[static_cast<size_t>(deleteIndex)].length * pxPerSample));
                float ny = gridOrigin - (deleteRow + 1) * gridStep;
                PianoRollResourceSystemLogic::DeleteAnim anim;
                anim.x = nx;
                anim.y = ny;
                anim.w = nw;
                anim.h = gridStep;
                int noteIndex = (deleteRow + 24) % 12;
                if (noteIndex < 0) noteIndex += 12;
                PianoRollResourceSystemLogic::NoteColor(noteIndex, anim.r, anim.g, anim.b);
                anim.startTime = currentTime;
                state.deleteAnims.push_back(anim);
                deleteClip.notes.erase(deleteClip.notes.begin() + deleteIndex);
            }
        }

        state.wasMouseDown = (mouseDown == GLFW_PRESS);
        state.wasRightDown = (rightDown == GLFW_PRESS);
    }
}
