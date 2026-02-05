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
        if (!baseSystem.ui || !baseSystem.midi || !baseSystem.daw || !win) return;

        UIContext& ui = *baseSystem.ui;
        MidiContext& midi = *baseSystem.midi;
        DawContext& daw = *baseSystem.daw;

        int trackIndex = state.layout.trackIndex;
        int clipIndex = state.layout.clipIndex;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(midi.tracks.size())) return;
        if (clipIndex < 0 || clipIndex >= static_cast<int>(midi.tracks[trackIndex].clips.size())) return;
        MidiClip& clip = midi.tracks[trackIndex].clips[clipIndex];

        const auto& cfg = PianoRollResourceSystemLogic::Config();
        const auto& layout = state.layout;

        float closeLeft = layout.closeLeft;
        float closeTop = layout.closeTop;
        float closeSize = layout.closeSize;

        if (ui.uiLeftReleased) {
            if (PianoRollResourceSystemLogic::CursorInRect(ui, closeLeft, closeLeft + closeSize, closeTop, closeTop + closeSize)) {
                midi.pianoRollActive = false;
                midi.pianoRollTrack = -1;
                midi.pianoRollClipIndex = -1;
                if (state.cursorDefault) {
                    glfwSetCursor(win, state.cursorDefault);
                    state.currentCursor = state.cursorDefault;
                }
                ui.consumeClick = true;
                return;
            }
        }

        bool shiftDown = (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
        if (ui.mainScrollDelta != 0.0) {
            if (shiftDown) {
                state.scrollOffsetX -= static_cast<float>(ui.mainScrollDelta * 40.0);
            } else {
                state.scrollOffsetY += static_cast<float>(ui.mainScrollDelta * 30.0);
            }
            ui.mainScrollDelta = 0.0;
        }
        state.scrollOffsetY = std::clamp(state.scrollOffsetY, layout.minScrollY, layout.maxScrollY);
        state.scrollOffsetX = std::clamp(state.scrollOffsetX, layout.minScrollX, layout.maxScrollX);

        double pxPerSample = layout.pxPerSample;
        float gridLeft = layout.gridLeft;
        float gridRight = layout.gridRight;
        float viewTop = layout.viewTop;
        float viewBottom = layout.viewBottom;
        float gridOrigin = layout.gridOrigin;
        float gridStep = layout.gridStep;
        int totalRows = cfg.totalRows;

        double clipStartSample = layout.clipStartSample;
        double clipEndSample = layout.clipEndSample;
        float clipStartX = gridLeft + state.scrollOffsetX + static_cast<float>((clipStartSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
        float clipEndX = clipStartX + static_cast<float>(clip.length * pxPerSample);

        int mouseDown = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT);
        int rightDown = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT);
        bool mousePressedThisFrame = (mouseDown == GLFW_PRESS) && !state.wasMouseDown;
        bool mouseReleasedThisFrame = (mouseDown == GLFW_RELEASE) && state.wasMouseDown;
        double mouseX = ui.cursorX;
        double mouseY = ui.cursorY;

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
        if (inGridArea) {
            for (int row = startRow; row <= endRow; ++row) {
                if (row < 0 || row >= totalRows) continue;
                int pitch = 24 + row;
                float ny = gridOrigin - (row + 1) * gridStep;
                for (int i = 0; i < static_cast<int>(clip.notes.size()); ++i) {
                    const MidiNote& note = clip.notes[i];
                    if (note.pitch != pitch) continue;
                    float nx = gridLeft + state.scrollOffsetX + static_cast<float>((static_cast<double>(note.startSample) - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
                    float nw = std::max(2.0f, static_cast<float>(note.length * pxPerSample));
                    if (isInsideRect(nx, ny, nw, gridStep, mouseX, mouseY)) {
                        hoverNote = i;
                        break;
                    }
                }
                if (hoverNote >= 0) break;
            }
        }

        GLFWcursor* desiredCursor = state.cursorDefault;
        if (!state.menuOpen && !state.scaleMenuOpen && inGridArea) {
            if (hoverNote >= 0) {
                float noteStartX = gridLeft + state.scrollOffsetX + static_cast<float>((clipStartSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
                float nx = noteStartX + static_cast<float>(clip.notes[hoverNote].startSample * pxPerSample);
                float nw = std::max(2.0f, static_cast<float>(clip.notes[hoverNote].length * pxPerSample));
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
            state.resizingNote = false;
            float noteStartX = gridLeft + state.scrollOffsetX + static_cast<float>((clipStartSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
            for (int i = 0; i < static_cast<int>(clip.notes.size()); ++i) {
                const MidiNote& note = clip.notes[i];
                int row = note.pitch - 24;
                if (row < 0 || row >= totalRows) continue;
                float nx = noteStartX + static_cast<float>(note.startSample * pxPerSample);
                float nw = std::max(2.0f, static_cast<float>(note.length * pxPerSample));
                float ny = gridOrigin - (row + 1) * gridStep;
                if (isInsideRect(nx, ny, nw, gridStep, mouseX, mouseY)) {
                    state.activeNote = i;
                    if (mouseX > nx + nw - cfg.noteHandleSize) {
                        state.resizingNote = true;
                    } else {
                        state.resizingNote = false;
                        state.dragOffsetSamples = (mouseX - nx) / pxPerSample;
                    }
                    break;
                }
            }
            if (state.editMode == PianoRollResourceSystemLogic::EditMode::Paint && state.activeNote == -1 && mouseRow >= 0) {
                state.painting = true;
                state.paintLastX.assign(totalRows, -1.0);
                state.paintLastXGlobal = -1.0;
                state.paintLastRow = mouseRow;
                state.paintLastCursorX = -1.0;
                state.paintDir = 0;
            } else {
                state.painting = false;
            }
            if (state.editMode == PianoRollResourceSystemLogic::EditMode::Draw && state.activeNote == -1 && mouseRow >= 0) {
                double localX = (mouseX - gridLeft - state.scrollOffsetX) / pxPerSample;
                double snappedX = layout.snapSamples > 0.0 ? PianoRollResourceSystemLogic::SnapFloor(localX, layout.snapSamples) : localX;
                double defaultLen = layout.defaultStepSamples;
                double noteLen = (state.lastNoteLengthSamples > 0.0) ? state.lastNoteLengthSamples : defaultLen;
                int targetRow = PianoRollResourceSystemLogic::AdjustRowToScale(mouseRow, mouseY, gridOrigin, gridStep, totalRows, state.scaleRoot, state.scaleType, state.scaleMode);
                int pitch = 24 + targetRow;
                if (pitch >= 0 && pitch <= 127) {
                    double startSample = snappedX;
                    double len = noteLen;
                    if (PianoRollResourceSystemLogic::PlaceNote(clip.notes, pitch, startSample, len, layout.snapSamples, layout.minNoteLenSamples, true)) {
                        state.activeNote = static_cast<int>(clip.notes.size()) - 1;
                        state.resizingNote = false;
                        state.dragOffsetSamples = (mouseX - (gridLeft + state.scrollOffsetX + static_cast<float>(startSample * pxPerSample))) / pxPerSample;
                        state.lastNoteLengthSamples = len;
                    }
                }
            }
        }

        if (mouseDown == GLFW_PRESS && state.painting && inGridArea && mouseRow >= 0 && hoverNote < 0) {
            if (!PianoRollResourceSystemLogic::IsScaleNote(mouseRow % 12, state.scaleRoot, state.scaleType, state.scaleMode)) {
                // do nothing
            } else {
                double localX = (mouseX - gridLeft - state.scrollOffsetX) / pxPerSample;
                double targetX = layout.snapSamples > 0.0 ? PianoRollResourceSystemLogic::SnapFloor(localX, layout.snapSamples) : localX;
                double defaultLen = layout.defaultStepSamples;
                double noteLen = (state.lastNoteLengthSamples > 0.0) ? state.lastNoteLengthSamples : defaultLen;
                double lastX = state.paintLastX[static_cast<size_t>(mouseRow)];
                double lastXAny = state.paintLastXGlobal;
                double placeX = targetX;
                if (state.paintDir == 0 && lastXAny >= 0.0) {
                    state.paintDir = (targetX > lastXAny) ? 1 : -1;
                }
                if (state.paintDir < 0) {
                    placeX = targetX - noteLen;
                }
                if (state.paintDir != 0) {
                    if (lastX >= 0.0) {
                        double minDist = layout.snapSamples > 0.0 ? layout.snapSamples : noteLen;
                        if (std::fabs(placeX - lastX) < minDist * 0.5) {
                            placeX = lastX;
                        }
                    }
                    if (placeX < 0.0) placeX = 0.0;
                    if (placeX + noteLen > static_cast<double>(clip.length)) {
                        noteLen = std::max(layout.minNoteLenSamples, static_cast<double>(clip.length) - placeX);
                    }
                    if (noteLen >= layout.minNoteLenSamples) {
                        int pitch = 24 + mouseRow;
                        if (pitch >= 0 && pitch <= 127) {
                            double startSample = placeX;
                            double len = noteLen;
                            if (PianoRollResourceSystemLogic::PlaceNote(clip.notes, pitch, startSample, len, layout.snapSamples, layout.minNoteLenSamples, false)) {
                                state.paintLastX[static_cast<size_t>(mouseRow)] = placeX;
                                state.paintLastXGlobal = placeX;
                                state.paintLastRow = mouseRow;
                                state.lastNoteLengthSamples = len;
                            }
                        }
                    }
                }
            }
        }

        if (state.painting && inGridArea) {
            state.paintLastCursorX = (mouseX - gridLeft - state.scrollOffsetX) / pxPerSample;
        }

        if (mouseDown == GLFW_PRESS && state.activeNote >= 0 && state.activeNote < static_cast<int>(clip.notes.size())) {
            double localX = (mouseX - gridLeft - state.scrollOffsetX) / pxPerSample;
            if (state.resizingNote) {
                double newLen = layout.snapSamples > 0.0 ? PianoRollResourceSystemLogic::SnapValue(localX - static_cast<double>(clip.notes[state.activeNote].startSample), layout.snapSamples)
                                                       : (localX - static_cast<double>(clip.notes[state.activeNote].startSample));
                double nextStart = PianoRollResourceSystemLogic::GetNextNoteStart(clip.notes, clip.notes[state.activeNote].pitch, static_cast<double>(clip.notes[state.activeNote].startSample), state.activeNote);
                if (nextStart >= 0.0 && static_cast<double>(clip.notes[state.activeNote].startSample) + newLen > nextStart) {
                    newLen = layout.snapSamples > 0.0 ? PianoRollResourceSystemLogic::SnapValue(nextStart - static_cast<double>(clip.notes[state.activeNote].startSample), layout.snapSamples)
                                                      : (nextStart - static_cast<double>(clip.notes[state.activeNote].startSample));
                }
                if (static_cast<double>(clip.notes[state.activeNote].startSample) + newLen > static_cast<double>(clip.length)) {
                    newLen = std::max(layout.minSnapLenSamples, static_cast<double>(clip.length) - static_cast<double>(clip.notes[state.activeNote].startSample));
                }
                clip.notes[state.activeNote].length = static_cast<uint64_t>(std::round(newLen));
                state.lastNoteLengthSamples = newLen;
            } else {
                double snappedX = layout.snapSamples > 0.0 ? PianoRollResourceSystemLogic::SnapFloor(localX - state.dragOffsetSamples, layout.snapSamples)
                                                           : (localX - state.dragOffsetSamples);
                int targetRow = mouseRow >= 0 ? mouseRow : (clip.notes[state.activeNote].pitch - 24);
                targetRow = PianoRollResourceSystemLogic::AdjustRowToScale(targetRow, mouseY, gridOrigin, gridStep, totalRows, state.scaleRoot, state.scaleType, state.scaleMode);
                int pitch = 24 + targetRow;
                if (pitch >= 0 && pitch <= 127) {
                    int sameStart = PianoRollResourceSystemLogic::FindNoteAtStart(clip.notes, pitch, snappedX, state.activeNote);
                    if (sameStart >= 0) {
                        snappedX = layout.snapSamples > 0.0
                            ? PianoRollResourceSystemLogic::SnapFloor(static_cast<double>(clip.notes[sameStart].startSample + clip.notes[sameStart].length), layout.snapSamples)
                            : static_cast<double>(clip.notes[sameStart].startSample + clip.notes[sameStart].length);
                    }
                    if (snappedX + static_cast<double>(clip.notes[state.activeNote].length) > static_cast<double>(clip.length)) {
                        snappedX = std::max(0.0, static_cast<double>(clip.length) - static_cast<double>(clip.notes[state.activeNote].length));
                    }
                    clip.notes[state.activeNote].startSample = static_cast<uint64_t>(std::round(snappedX));
                    clip.notes[state.activeNote].pitch = pitch;
                }
            }
        }

        if (mouseReleasedThisFrame && state.activeNote >= 0 && state.activeNote < static_cast<int>(clip.notes.size()) && !state.resizingNote) {
            double localX = (mouseX - gridLeft - state.scrollOffsetX) / pxPerSample;
            double snappedX = layout.snapSamples > 0.0 ? PianoRollResourceSystemLogic::SnapFloor(localX - state.dragOffsetSamples, layout.snapSamples)
                                                       : (localX - state.dragOffsetSamples);
            int targetRow = mouseRow >= 0 ? mouseRow : (clip.notes[state.activeNote].pitch - 24);
            targetRow = PianoRollResourceSystemLogic::AdjustRowToScale(targetRow, mouseY, gridOrigin, gridStep, totalRows, state.scaleRoot, state.scaleType, state.scaleMode);
            int pitch = 24 + targetRow;
            if (pitch >= 0 && pitch <= 127) {
                int sameStart = PianoRollResourceSystemLogic::FindNoteAtStart(clip.notes, pitch, snappedX, state.activeNote);
                if (sameStart >= 0) {
                    snappedX = layout.snapSamples > 0.0
                        ? PianoRollResourceSystemLogic::SnapFloor(static_cast<double>(clip.notes[sameStart].startSample + clip.notes[sameStart].length), layout.snapSamples)
                        : static_cast<double>(clip.notes[sameStart].startSample + clip.notes[sameStart].length);
                }
                int overlap = PianoRollResourceSystemLogic::FindOverlappingNote(clip.notes, pitch, snappedX, state.activeNote);
                if (overlap >= 0) {
                    double newLen = snappedX - static_cast<double>(clip.notes[overlap].startSample);
                    if (newLen < layout.minNoteLenSamples) {
                        newLen = layout.minNoteLenSamples;
                        snappedX = static_cast<double>(clip.notes[overlap].startSample) + newLen;
                    }
                    clip.notes[overlap].length = static_cast<uint64_t>(std::round(newLen));
                }
                double nextStart = PianoRollResourceSystemLogic::GetNextNoteStart(clip.notes, pitch, snappedX, state.activeNote);
                double maxLen = static_cast<double>(clip.notes[state.activeNote].length);
                if (nextStart >= 0.0 && snappedX + maxLen > nextStart) {
                    maxLen = nextStart - snappedX;
                }
                if (snappedX + maxLen > static_cast<double>(clip.length)) {
                    maxLen = std::max(layout.minNoteLenSamples, static_cast<double>(clip.length) - snappedX);
                }
                clip.notes[state.activeNote].startSample = static_cast<uint64_t>(std::round(snappedX));
                clip.notes[state.activeNote].pitch = pitch;
                clip.notes[state.activeNote].length = static_cast<uint64_t>(std::round(maxLen));
            }
            state.activeNote = -1;
            state.resizingNote = false;
            state.painting = false;
            state.paintLastRow = -1;
            state.paintLastCursorX = -1.0;
            state.paintDir = 0;
        }

        if (rightDown == GLFW_PRESS && !state.wasRightDown && inGridArea) {
            float noteStartX = gridLeft + state.scrollOffsetX + static_cast<float>((clipStartSample - static_cast<double>(daw.timelineOffsetSamples)) * pxPerSample);
            int deleteIndex = -1;
            int deleteRow = -1;
            for (int row = startRow; row <= endRow; ++row) {
                if (row < 0 || row >= totalRows) continue;
                int pitch = 24 + row;
                float ny = gridOrigin - (row + 1) * gridStep;
                for (int i = 0; i < static_cast<int>(clip.notes.size()); ++i) {
                    const MidiNote& note = clip.notes[i];
                    if (note.pitch != pitch) continue;
                    float nx = noteStartX + static_cast<float>(note.startSample * pxPerSample);
                    float nw = std::max(2.0f, static_cast<float>(note.length * pxPerSample));
                    if (isInsideRect(nx, ny, nw, gridStep, mouseX, mouseY)) {
                        deleteIndex = i;
                        deleteRow = row;
                        break;
                    }
                }
                if (deleteIndex >= 0) break;
            }
            if (deleteIndex >= 0) {
                float nx = noteStartX + static_cast<float>(clip.notes[deleteIndex].startSample * pxPerSample);
                float nw = std::max(2.0f, static_cast<float>(clip.notes[deleteIndex].length * pxPerSample));
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
                clip.notes.erase(clip.notes.begin() + deleteIndex);
            }
        }

        state.wasMouseDown = (mouseDown == GLFW_PRESS);
        state.wasRightDown = (rightDown == GLFW_PRESS);
    }
}
