#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "stb_image.h"

namespace DawClipSystemLogic {
    void TrimClipsForNewClip(DawTrack& track, const DawClip& clip);
    void RebuildTrackCacheFromClips(DawContext& daw, DawTrack& track);
    bool CycleTrackLoopTake(DawContext& daw, int trackIndex, int direction);
}
namespace DawTrackSystemLogic {
    bool MoveTrack(BaseSystem& baseSystem, int fromIndex, int toIndex);
}
namespace MidiTransportSystemLogic {
    bool CycleTrackLoopTake(MidiContext& midi, int trackIndex, int direction);
}
namespace DawLaneTimelineSystemLogic {
    struct LaneLayout;
    bool hasDawUiWorld(const LevelContext& level);
    LaneLayout ComputeLaneLayout(const BaseSystem& baseSystem, const DawContext& daw, GLFWwindow* win);
    std::vector<int> BuildAudioLaneIndex(const DawContext& daw, int audioTrackCount);
    void ClampTimelineOffset(DawContext& daw);
    double GridSecondsForZoom(double secondsPerScreen, double secondsPerBeat);
    uint64_t MaxTimelineSamples(const DawContext& daw);
}
namespace DawTimelineRebaseLogic { void ShiftTimelineRight(BaseSystem& baseSystem, uint64_t shiftSamples); }
namespace DawIOSystemLogic {
    bool OpenExportFolderDialog(std::string& ioPath);
    bool StartStemExport(BaseSystem& baseSystem);
    std::string ThemeColorToHex(const glm::vec4& color);
    bool ApplyThemeByIndex(BaseSystem& baseSystem, int themeIndex, bool persistToDisk);
    bool RemoveThemeByIndex(BaseSystem& baseSystem, int themeIndex, std::string& outMessage);
    bool SaveThemeFromDraft(BaseSystem& baseSystem,
                            const std::string& rawName,
                            const std::string& backgroundHex,
                            const std::string& panelHex,
                            const std::string& buttonHex,
                            const std::string& pianoRollHex,
                            const std::string& pianoRollAccentHex,
                            const std::string& laneHex,
                            std::string& outMessage);
    void BeginThemeDraftFromDefault(BaseSystem& baseSystem);
    void EnsureThemeState(BaseSystem& baseSystem);
}

namespace DawLaneInputSystemLogic {
    namespace {
        constexpr float kLoopHandleWidth = 8.0f;
        constexpr float kTrackHandleSize = 60.0f;
        constexpr float kTrackHandleInset = 12.0f;
        constexpr float kClipHorizontalPad = 2.0f;
        constexpr float kClipVerticalInset = 0.0f;
        constexpr float kClipMinHeight = 2.0f;
        constexpr float kClipLipMinHeight = 6.0f;
        constexpr float kClipLipMaxHeight = 12.0f;
        constexpr float kTrimEdgeHitWidth = 8.0f;
        constexpr uint64_t kMinClipSamples = 1;
        constexpr float kTakeRowGap = 4.0f;
        constexpr float kTakeRowSpacing = 2.0f;
        constexpr float kTakeRowMinHeight = 10.0f;
        constexpr float kTakeRowMaxHeight = 18.0f;

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
            Rect panelRect;
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

        struct ClipTrimHit {
            bool valid = false;
            bool leftEdge = false;
            int track = -1;
            int clipIndex = -1;
            float edgeDistance = FLT_MAX;
            uint64_t clipStart = 0;
            uint64_t clipLength = 0;
            uint64_t clipSourceOffset = 0;
        };

        static GLFWcursor* g_laneTrimCursor = nullptr;
        static bool g_laneTrimCursorLoaded = false;
        static bool g_laneTrimCursorApplied = false;
        static bool g_cmdEShortcutWasDown = false;
        static bool g_cmdShiftMShortcutWasDown = false;
        static bool g_cmdLShortcutWasDown = false;
        static bool g_spaceShortcutWasDown = false;
        static bool g_deleteShortcutWasDown = false;
        static bool g_cmdPrevTakeShortcutWasDown = false;
        static bool g_cmdNextTakeShortcutWasDown = false;
        static bool g_rightMouseWasDown = false;
        static std::array<uint8_t, GLFW_KEY_LAST + 1> g_exportKeyDown{};
        static std::array<uint8_t, GLFW_KEY_LAST + 1> g_settingsKeyDown{};

        bool isCommandDown(GLFWwindow* win) {
            if (!win) return false;
            return glfwGetKey(win, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS
                || glfwGetKey(win, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
        }

        bool isShiftDown(GLFWwindow* win) {
            if (!win) return false;
            return glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                || glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        }

        Rect makeRect(float x, float y, float w, float h) {
            Rect rect;
            rect.left = x;
            rect.right = x + w;
            rect.top = y;
            rect.bottom = y + h;
            return rect;
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
            out.panelRect = makeRect(out.x, out.y, out.w, out.h);
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

        bool cursorInRect(const UIContext& ui, const Rect& rect) {
            return ui.cursorX >= rect.left
                && ui.cursorX <= rect.right
                && ui.cursorY >= rect.top
                && ui.cursorY <= rect.bottom;
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

        bool keyPressedEdge(GLFWwindow* win, int key, std::array<uint8_t, GLFW_KEY_LAST + 1>& cache) {
            if (!win || key < 0 || key > GLFW_KEY_LAST) return false;
            bool down = glfwGetKey(win, key) == GLFW_PRESS;
            bool pressed = down && !cache[static_cast<size_t>(key)];
            cache[static_cast<size_t>(key)] = down ? 1u : 0u;
            return pressed;
        }

        bool exportKeyPressed(GLFWwindow* win, int key) {
            return keyPressedEdge(win, key, g_exportKeyDown);
        }

        bool settingsKeyPressed(GLFWwindow* win, int key) {
            return keyPressedEdge(win, key, g_settingsKeyDown);
        }

        void appendStemChar(std::string& stem, char c) {
            constexpr size_t kMaxStemChars = 48;
            if (stem.size() >= kMaxStemChars) return;
            stem.push_back(c);
        }

        void appendThemeNameChar(std::string& value, char c) {
            constexpr size_t kMaxChars = 40;
            if (value.size() >= kMaxChars) return;
            value.push_back(c);
        }

        void appendThemeHexChar(std::string& value, char c) {
            constexpr size_t kMaxChars = 8;
            if (value.size() >= kMaxChars) return;
            value.push_back(c);
        }

        void updateStemNameTyping(GLFWwindow* win, DawContext& daw, bool shiftDown) {
            if (!win) return;
            if (daw.exportSelectedStem < 0 || daw.exportSelectedStem >= DawContext::kBusCount) return;
            std::string& stem = daw.exportStemNames[static_cast<size_t>(daw.exportSelectedStem)];

            if (exportKeyPressed(win, GLFW_KEY_BACKSPACE)) {
                if (!stem.empty()) stem.pop_back();
            }
            if (exportKeyPressed(win, GLFW_KEY_SPACE)) {
                appendStemChar(stem, '_');
            }
            if (exportKeyPressed(win, GLFW_KEY_MINUS)) {
                appendStemChar(stem, shiftDown ? '_' : '-');
            }
            if (exportKeyPressed(win, GLFW_KEY_PERIOD)) {
                appendStemChar(stem, '.');
            }

            for (int key = GLFW_KEY_0; key <= GLFW_KEY_9; ++key) {
                if (exportKeyPressed(win, key)) {
                    appendStemChar(stem, static_cast<char>('0' + (key - GLFW_KEY_0)));
                }
            }
            for (int key = GLFW_KEY_A; key <= GLFW_KEY_Z; ++key) {
                if (!exportKeyPressed(win, key)) continue;
                char c = static_cast<char>('a' + (key - GLFW_KEY_A));
                if (shiftDown) c = static_cast<char>(c - ('a' - 'A'));
                appendStemChar(stem, c);
            }
        }

        void updateThemeDraftTyping(GLFWwindow* win, DawContext& daw, bool shiftDown) {
            if (!win) return;
            if (daw.themeEditField < 0 || daw.themeEditField > 6) return;

            std::string* target = nullptr;
            bool hexField = false;
            if (daw.themeEditField == 0) {
                target = &daw.themeDraftName;
            } else if (daw.themeEditField == 1) {
                target = &daw.themeDraftBackgroundHex;
                hexField = true;
            } else if (daw.themeEditField == 2) {
                target = &daw.themeDraftPanelHex;
                hexField = true;
            } else if (daw.themeEditField == 3) {
                target = &daw.themeDraftButtonHex;
                hexField = true;
            } else if (daw.themeEditField == 4) {
                target = &daw.themeDraftPianoRollHex;
                hexField = true;
            } else if (daw.themeEditField == 5) {
                target = &daw.themeDraftPianoRollAccentHex;
                hexField = true;
            } else if (daw.themeEditField == 6) {
                target = &daw.themeDraftLaneHex;
                hexField = true;
            }
            if (!target) return;

            if (settingsKeyPressed(win, GLFW_KEY_BACKSPACE)) {
                if (!target->empty()) target->pop_back();
            }

            if (!hexField) {
                if (settingsKeyPressed(win, GLFW_KEY_SPACE)) {
                    appendThemeNameChar(*target, ' ');
                }
                if (settingsKeyPressed(win, GLFW_KEY_MINUS)) {
                    appendThemeNameChar(*target, shiftDown ? '_' : '-');
                }
                if (settingsKeyPressed(win, GLFW_KEY_PERIOD)) {
                    appendThemeNameChar(*target, '.');
                }
                for (int key = GLFW_KEY_0; key <= GLFW_KEY_9; ++key) {
                    if (!settingsKeyPressed(win, key)) continue;
                    appendThemeNameChar(*target, static_cast<char>('0' + (key - GLFW_KEY_0)));
                }
                for (int key = GLFW_KEY_A; key <= GLFW_KEY_Z; ++key) {
                    if (!settingsKeyPressed(win, key)) continue;
                    char c = static_cast<char>('a' + (key - GLFW_KEY_A));
                    if (shiftDown) c = static_cast<char>(c - ('a' - 'A'));
                    appendThemeNameChar(*target, c);
                }
                return;
            }

            for (int key = GLFW_KEY_0; key <= GLFW_KEY_9; ++key) {
                if (!settingsKeyPressed(win, key)) continue;
                appendThemeHexChar(*target, static_cast<char>('0' + (key - GLFW_KEY_0)));
            }
            for (int key = GLFW_KEY_A; key <= GLFW_KEY_F; ++key) {
                if (!settingsKeyPressed(win, key)) continue;
                char c = static_cast<char>('A' + (key - GLFW_KEY_A));
                appendThemeHexChar(*target, c);
            }
        }

        uint64_t gridStepSamples(const DawContext& daw, double secondsPerScreen) {
            double bpm = daw.bpm.load(std::memory_order_relaxed);
            if (bpm <= 0.0) bpm = 120.0;
            double secondsPerBeat = 60.0 / bpm;
            double gridSeconds = DawLaneTimelineSystemLogic::GridSecondsForZoom(secondsPerScreen, secondsPerBeat);
            if (gridSeconds <= 0.0) return 1;
            double sampleRate = (daw.sampleRate > 0.0) ? daw.sampleRate : 44100.0;
            return std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(gridSeconds * sampleRate)));
        }

        uint64_t snapSampleToGrid(const DawContext& daw, double secondsPerScreen, int64_t sample) {
            if (sample < 0) sample = 0;
            uint64_t step = gridStepSamples(daw, secondsPerScreen);
            if (step == 0) return static_cast<uint64_t>(sample);
            return (static_cast<uint64_t>(sample) / step) * step;
        }

        uint64_t computeRebaseShiftSamples(const DawContext& daw, int64_t negativeSample) {
            if (negativeSample >= 0) return 0;
            double sampleRate = (daw.sampleRate > 0.0) ? daw.sampleRate : 44100.0;
            double bpm = daw.bpm.load(std::memory_order_relaxed);
            if (bpm <= 0.0) bpm = 120.0;
            uint64_t barSamples = std::max<uint64_t>(1,
                static_cast<uint64_t>(std::llround((60.0 / bpm) * 4.0 * sampleRate)));
            uint64_t need = static_cast<uint64_t>(-negativeSample) + barSamples * 2ull;
            uint64_t shift = ((need + barSamples - 1ull) / barSamples) * barSamples;
            if (shift == 0) shift = barSamples;
            return shift;
        }

        uint64_t sampleFromCursorX(BaseSystem& baseSystem,
                                   DawContext& daw,
                                   float laneLeft,
                                   float laneRight,
                                   double secondsPerScreen,
                                   double cursorX,
                                   bool snap) {
            double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
            double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples <= 0.0) windowSamples = 1.0;
            double t = (laneRight > laneLeft)
                ? (cursorX - laneLeft) / static_cast<double>(laneRight - laneLeft)
                : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            int64_t sample = static_cast<int64_t>(std::llround(offsetSamples + t * windowSamples));
            if (sample < 0) {
                uint64_t shiftSamples = computeRebaseShiftSamples(daw, sample);
                DawTimelineRebaseLogic::ShiftTimelineRight(baseSystem, shiftSamples);
                sample += static_cast<int64_t>(shiftSamples);
            }
            if (snap) return snapSampleToGrid(daw, secondsPerScreen, sample);
            return static_cast<uint64_t>(sample);
        }

        int laneIndexFromCursorYClamped(float y, float startY, float rowSpan, int laneCount) {
            if (laneCount <= 0) return -1;
            int idx = static_cast<int>(std::floor((y - startY) / rowSpan + 0.5f));
            return std::clamp(idx, 0, laneCount - 1);
        }

        GLFWcursor* loadTrimCursorImage(const char* path, int hotX, int hotY) {
            int width = 0;
            int height = 0;
            int channels = 0;
            unsigned char* data = stbi_load(path, &width, &height, &channels, 4);
            if (!data) return nullptr;
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
            image.width = (outW > 0) ? outW : width;
            image.height = (outH > 0) ? outH : height;
            image.pixels = (outW > 0) ? scaled.data() : data;
            GLFWcursor* cursor = glfwCreateCursor(&image, hotX / 2, hotY / 2);
            stbi_image_free(data);
            return cursor;
        }

        GLFWcursor* ensureLaneTrimCursor() {
            if (!g_laneTrimCursorLoaded) {
                g_laneTrimCursor = loadTrimCursorImage("Procedures/assets/resize_a_horizontal.png", 16, 16);
                if (!g_laneTrimCursor) {
                    g_laneTrimCursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
                }
                g_laneTrimCursorLoaded = true;
            }
            return g_laneTrimCursor;
        }

        void applySplitToMidiClip(MidiClip& clip, uint64_t newStart, uint64_t newLength) {
            uint64_t oldStart = clip.startSample;
            uint64_t newEnd = newStart + newLength;
            std::vector<MidiNote> trimmed;
            trimmed.reserve(clip.notes.size());
            for (const auto& note : clip.notes) {
                if (note.length == 0) continue;
                uint64_t noteStart = oldStart + note.startSample;
                uint64_t noteEnd = noteStart + note.length;
                if (noteEnd <= newStart || noteStart >= newEnd) continue;
                uint64_t clippedStart = std::max(noteStart, newStart);
                uint64_t clippedEnd = std::min(noteEnd, newEnd);
                if (clippedEnd <= clippedStart) continue;
                MidiNote out = note;
                out.startSample = clippedStart - newStart;
                out.length = clippedEnd - clippedStart;
                trimmed.push_back(out);
            }
            clip.startSample = newStart;
            clip.length = newLength;
            clip.notes = std::move(trimmed);
        }

        void trimMidiClipsForNewClip(MidiTrack& track, const MidiClip& clip) {
            if (clip.length == 0) return;
            uint64_t newStart = clip.startSample;
            uint64_t newEnd = clip.startSample + clip.length;
            std::vector<MidiClip> updated;
            updated.reserve(track.clips.size() + 1);
            for (const auto& existing : track.clips) {
                if (existing.length == 0) continue;
                uint64_t exStart = existing.startSample;
                uint64_t exEnd = existing.startSample + existing.length;
                if (exEnd <= newStart || exStart >= newEnd) {
                    updated.push_back(existing);
                    continue;
                }
                if (newStart <= exStart && newEnd >= exEnd) {
                    continue;
                }
                if (newStart > exStart && newEnd < exEnd) {
                    MidiClip left = existing;
                    MidiClip right = existing;
                    applySplitToMidiClip(left, exStart, newStart - exStart);
                    applySplitToMidiClip(right, newEnd, exEnd - newEnd);
                    if (left.length > 0) updated.push_back(std::move(left));
                    if (right.length > 0) updated.push_back(std::move(right));
                } else if (newStart <= exStart) {
                    MidiClip right = existing;
                    applySplitToMidiClip(right, newEnd, exEnd - newEnd);
                    if (right.length > 0) updated.push_back(std::move(right));
                } else {
                    MidiClip left = existing;
                    applySplitToMidiClip(left, exStart, newStart - exStart);
                    if (left.length > 0) updated.push_back(std::move(left));
                }
            }
            track.clips = std::move(updated);
        }

        void sortMidiClipsByStart(std::vector<MidiClip>& clips) {
            std::sort(clips.begin(), clips.end(), [](const MidiClip& a, const MidiClip& b) {
                if (a.startSample == b.startSample) return a.length < b.length;
                return a.startSample < b.startSample;
            });
        }

        float takeRowHeight(float laneHalfH) {
            float laneHeight = laneHalfH * 2.0f;
            return std::clamp(laneHeight * 0.26f, kTakeRowMinHeight, kTakeRowMaxHeight);
        }

        bool splitSelectedAudioClipAtPlayhead(BaseSystem& baseSystem, DawContext& daw, uint64_t splitSample) {
            int trackIndex = daw.selectedClipTrack;
            int clipIndex = daw.selectedClipIndex;
            if (trackIndex < 0 || clipIndex < 0 || trackIndex >= static_cast<int>(daw.tracks.size())) return false;
            DawTrack& track = daw.tracks[static_cast<size_t>(trackIndex)];
            if (clipIndex >= static_cast<int>(track.clips.size())) return false;

            const DawClip& src = track.clips[static_cast<size_t>(clipIndex)];
            uint64_t clipStart = src.startSample;
            uint64_t clipEnd = src.startSample + src.length;
            if (src.length == 0 || splitSample <= clipStart || splitSample >= clipEnd) return false;

            DawClip left = src;
            DawClip right = src;
            left.length = splitSample - clipStart;
            right.startSample = splitSample;
            right.length = clipEnd - splitSample;
            right.sourceOffset = src.sourceOffset + (splitSample - clipStart);

            track.clips[static_cast<size_t>(clipIndex)] = left;
            track.clips.insert(track.clips.begin() + clipIndex + 1, right);
            DawClipSystemLogic::RebuildTrackCacheFromClips(daw, track);
            daw.selectedClipTrack = trackIndex;
            daw.selectedClipIndex = clipIndex + 1;
            daw.selectedAutomationClipTrack = -1;
            daw.selectedAutomationClipIndex = -1;
            if (baseSystem.midi) {
                baseSystem.midi->selectedClipTrack = -1;
                baseSystem.midi->selectedClipIndex = -1;
            }
            return true;
        }

        bool splitSelectedMidiClipAtPlayhead(BaseSystem& baseSystem, DawContext& daw, uint64_t splitSample) {
            if (!baseSystem.midi) return false;
            MidiContext& midi = *baseSystem.midi;
            int trackIndex = midi.selectedClipTrack;
            int clipIndex = midi.selectedClipIndex;
            if (trackIndex < 0 || clipIndex < 0 || trackIndex >= static_cast<int>(midi.tracks.size())) return false;
            MidiTrack& track = midi.tracks[static_cast<size_t>(trackIndex)];
            if (clipIndex >= static_cast<int>(track.clips.size())) return false;

            const MidiClip& src = track.clips[static_cast<size_t>(clipIndex)];
            uint64_t clipStart = src.startSample;
            uint64_t clipEnd = src.startSample + src.length;
            if (src.length == 0 || splitSample <= clipStart || splitSample >= clipEnd) return false;

            MidiClip left = src;
            MidiClip right = src;
            applySplitToMidiClip(left, clipStart, splitSample - clipStart);
            applySplitToMidiClip(right, splitSample, clipEnd - splitSample);
            track.clips[static_cast<size_t>(clipIndex)] = left;
            track.clips.insert(track.clips.begin() + clipIndex + 1, right);

            midi.selectedTrackIndex = trackIndex;
            midi.selectedClipTrack = trackIndex;
            midi.selectedClipIndex = clipIndex + 1;
            daw.selectedClipTrack = -1;
            daw.selectedClipIndex = -1;
            daw.selectedAutomationClipTrack = -1;
            daw.selectedAutomationClipIndex = -1;
            return true;
        }

        bool createMidiClipsFromTimelineSelection(BaseSystem& baseSystem, DawContext& daw) {
            if (!baseSystem.midi) return false;
            MidiContext& midi = *baseSystem.midi;
            if (!daw.timelineSelectionActive) return false;
            if (midi.tracks.empty()) return false;

            uint64_t selStart = std::min(daw.timelineSelectionStartSample, daw.timelineSelectionEndSample);
            uint64_t selEnd = std::max(daw.timelineSelectionStartSample, daw.timelineSelectionEndSample);
            if (selEnd <= selStart) return false;

            int laneMin = std::min(daw.timelineSelectionStartLane, daw.timelineSelectionEndLane);
            int laneMax = std::max(daw.timelineSelectionStartLane, daw.timelineSelectionEndLane);
            if (laneMax < 0) return false;

            std::vector<int> targetTracks;
            if (!daw.laneOrder.empty()) {
                laneMin = std::max(0, laneMin);
                laneMax = std::min(laneMax, static_cast<int>(daw.laneOrder.size()) - 1);
                for (int lane = laneMin; lane <= laneMax; ++lane) {
                    const auto& entry = daw.laneOrder[static_cast<size_t>(lane)];
                    if (entry.type != 1) continue;
                    if (entry.trackIndex < 0 || entry.trackIndex >= static_cast<int>(midi.tracks.size())) continue;
                    if (std::find(targetTracks.begin(), targetTracks.end(), entry.trackIndex) == targetTracks.end()) {
                        targetTracks.push_back(entry.trackIndex);
                    }
                }
            } else {
                int audioTrackCount = static_cast<int>(daw.tracks.size());
                int startTrack = std::max(0, laneMin - audioTrackCount);
                int endTrack = std::min(static_cast<int>(midi.tracks.size()) - 1, laneMax - audioTrackCount);
                for (int t = startTrack; t <= endTrack; ++t) {
                    targetTracks.push_back(t);
                }
            }

            if (targetTracks.empty()) return false;

            for (int trackIdx : targetTracks) {
                const MidiTrack& track = midi.tracks[static_cast<size_t>(trackIdx)];
                for (const auto& clip : track.clips) {
                    if (clip.length == 0) continue;
                    uint64_t clipStart = clip.startSample;
                    uint64_t clipEnd = clip.startSample + clip.length;
                    bool overlap = !(clipEnd <= selStart || clipStart >= selEnd);
                    if (overlap) return false;
                }
            }

            int firstTrack = -1;
            int firstClipIndex = -1;
            for (int trackIdx : targetTracks) {
                MidiTrack& track = midi.tracks[static_cast<size_t>(trackIdx)];
                MidiClip newClip{};
                newClip.startSample = selStart;
                newClip.length = selEnd - selStart;
                track.clips.push_back(newClip);
                std::sort(track.clips.begin(), track.clips.end(), [](const MidiClip& a, const MidiClip& b) {
                    if (a.startSample == b.startSample) return a.length < b.length;
                    return a.startSample < b.startSample;
                });
                if (firstTrack < 0) {
                    firstTrack = trackIdx;
                    for (size_t i = 0; i < track.clips.size(); ++i) {
                        if (track.clips[i].startSample == newClip.startSample
                            && track.clips[i].length == newClip.length
                            && track.clips[i].notes.empty()) {
                            firstClipIndex = static_cast<int>(i);
                            break;
                        }
                    }
                }
            }

            if (firstTrack >= 0) {
                midi.selectedTrackIndex = firstTrack;
                midi.selectedClipTrack = firstTrack;
                midi.selectedClipIndex = firstClipIndex;
                daw.selectedClipTrack = -1;
                daw.selectedClipIndex = -1;
                daw.selectedAutomationClipTrack = -1;
                daw.selectedAutomationClipIndex = -1;
            }
            return true;
        }

        bool createAutomationClipsFromTimelineSelection(BaseSystem& baseSystem, DawContext& daw) {
            if (!daw.timelineSelectionActive) return false;
            if (daw.automationTracks.empty()) return false;

            uint64_t selStart = std::min(daw.timelineSelectionStartSample, daw.timelineSelectionEndSample);
            uint64_t selEnd = std::max(daw.timelineSelectionStartSample, daw.timelineSelectionEndSample);
            if (selEnd <= selStart) return false;

            int laneMin = std::min(daw.timelineSelectionStartLane, daw.timelineSelectionEndLane);
            int laneMax = std::max(daw.timelineSelectionStartLane, daw.timelineSelectionEndLane);
            if (laneMax < 0) return false;

            std::vector<int> targetTracks;
            if (!daw.laneOrder.empty()) {
                laneMin = std::max(0, laneMin);
                laneMax = std::min(laneMax, static_cast<int>(daw.laneOrder.size()) - 1);
                for (int lane = laneMin; lane <= laneMax; ++lane) {
                    const auto& entry = daw.laneOrder[static_cast<size_t>(lane)];
                    if (entry.type != 2) continue;
                    if (entry.trackIndex < 0
                        || entry.trackIndex >= static_cast<int>(daw.automationTracks.size())) continue;
                    if (std::find(targetTracks.begin(), targetTracks.end(), entry.trackIndex) == targetTracks.end()) {
                        targetTracks.push_back(entry.trackIndex);
                    }
                }
            } else {
                int audioTrackCount = static_cast<int>(daw.tracks.size());
                int midiTrackCount = baseSystem.midi ? static_cast<int>(baseSystem.midi->tracks.size()) : 0;
                int base = audioTrackCount + midiTrackCount;
                int startTrack = std::max(0, laneMin - base);
                int endTrack = std::min(static_cast<int>(daw.automationTracks.size()) - 1, laneMax - base);
                for (int t = startTrack; t <= endTrack; ++t) {
                    targetTracks.push_back(t);
                }
            }

            if (targetTracks.empty()) return false;

            for (int trackIdx : targetTracks) {
                const AutomationTrack& track = daw.automationTracks[static_cast<size_t>(trackIdx)];
                for (const auto& clip : track.clips) {
                    if (clip.length == 0) continue;
                    uint64_t clipStart = clip.startSample;
                    uint64_t clipEnd = clip.startSample + clip.length;
                    bool overlap = !(clipEnd <= selStart || clipStart >= selEnd);
                    if (overlap) return false;
                }
            }

            int firstTrack = -1;
            int firstClipIndex = -1;
            for (int trackIdx : targetTracks) {
                AutomationTrack& track = daw.automationTracks[static_cast<size_t>(trackIdx)];
                AutomationClip newClip{};
                newClip.startSample = selStart;
                newClip.length = selEnd - selStart;
                track.clips.push_back(newClip);
                std::sort(track.clips.begin(), track.clips.end(), [](const AutomationClip& a, const AutomationClip& b) {
                    if (a.startSample == b.startSample) return a.length < b.length;
                    return a.startSample < b.startSample;
                });
                if (firstTrack < 0) {
                    firstTrack = trackIdx;
                    for (size_t i = 0; i < track.clips.size(); ++i) {
                        if (track.clips[i].startSample == newClip.startSample
                            && track.clips[i].length == newClip.length
                            && track.clips[i].points.empty()) {
                            firstClipIndex = static_cast<int>(i);
                            break;
                        }
                    }
                }
            }

            if (firstTrack >= 0) {
                daw.selectedAutomationClipTrack = firstTrack;
                daw.selectedAutomationClipIndex = firstClipIndex;
                daw.selectedClipTrack = -1;
                daw.selectedClipIndex = -1;
                if (baseSystem.midi) {
                    baseSystem.midi->selectedClipTrack = -1;
                    baseSystem.midi->selectedClipIndex = -1;
                }
            }
            return true;
        }

        bool deleteSelectedAudioClip(BaseSystem& baseSystem, DawContext& daw) {
            int trackIndex = daw.selectedClipTrack;
            int clipIndex = daw.selectedClipIndex;
            if (trackIndex < 0 || clipIndex < 0 || trackIndex >= static_cast<int>(daw.tracks.size())) return false;
            DawTrack& track = daw.tracks[static_cast<size_t>(trackIndex)];
            if (clipIndex >= static_cast<int>(track.clips.size())) return false;
            track.clips.erase(track.clips.begin() + clipIndex);
            DawClipSystemLogic::RebuildTrackCacheFromClips(daw, track);
            daw.selectedClipTrack = -1;
            daw.selectedClipIndex = -1;
            daw.selectedAutomationClipTrack = -1;
            daw.selectedAutomationClipIndex = -1;
            daw.timelineSelectionActive = false;
            daw.timelineSelectionDragActive = false;
            return true;
        }

        bool deleteSelectedMidiClip(BaseSystem& baseSystem, DawContext& daw) {
            if (!baseSystem.midi) return false;
            MidiContext& midi = *baseSystem.midi;
            int trackIndex = midi.selectedClipTrack;
            int clipIndex = midi.selectedClipIndex;
            if (trackIndex < 0 || clipIndex < 0 || trackIndex >= static_cast<int>(midi.tracks.size())) return false;
            MidiTrack& track = midi.tracks[static_cast<size_t>(trackIndex)];
            if (clipIndex >= static_cast<int>(track.clips.size())) return false;
            track.clips.erase(track.clips.begin() + clipIndex);
            midi.selectedClipTrack = -1;
            midi.selectedClipIndex = -1;
            daw.selectedClipTrack = -1;
            daw.selectedClipIndex = -1;
            daw.selectedAutomationClipTrack = -1;
            daw.selectedAutomationClipIndex = -1;
            daw.timelineSelectionActive = false;
            daw.timelineSelectionDragActive = false;
            return true;
        }

        bool deleteSelectedAutomationClip(DawContext& daw) {
            int trackIndex = daw.selectedAutomationClipTrack;
            int clipIndex = daw.selectedAutomationClipIndex;
            if (trackIndex < 0 || clipIndex < 0 || trackIndex >= static_cast<int>(daw.automationTracks.size())) {
                return false;
            }
            AutomationTrack& track = daw.automationTracks[static_cast<size_t>(trackIndex)];
            if (clipIndex >= static_cast<int>(track.clips.size())) return false;
            track.clips.erase(track.clips.begin() + clipIndex);
            daw.selectedAutomationClipTrack = -1;
            daw.selectedAutomationClipIndex = -1;
            daw.timelineSelectionActive = false;
            daw.timelineSelectionDragActive = false;
            return true;
        }

        bool cursorInLaneRect(const UIContext& ui, float laneLeft, float laneRight, float top, float bottom) {
            float x = static_cast<float>(ui.cursorX);
            float y = static_cast<float>(ui.cursorY);
            return x >= laneLeft && x <= laneRight && y >= top && y <= bottom;
        }

        bool cursorInRect(const UIContext& ui, float left, float right, float top, float bottom) {
            float x = static_cast<float>(ui.cursorX);
            float y = static_cast<float>(ui.cursorY);
            return x >= left && x <= right && y >= top && y <= bottom;
        }

        bool cursorInTrackHandleRect(const UIContext& ui,
                                     float laneLeft,
                                     float laneRight,
                                     float centerY,
                                     float laneHalfH) {
            float laneHeight = laneHalfH * 2.0f;
            float handleSize = std::min(kTrackHandleSize, std::max(14.0f, laneHeight));
            float handleHalf = handleSize * 0.5f;
            float centerX = laneRight + kTrackHandleInset + handleHalf;
            float minCenterX = laneLeft + 4.0f + handleHalf;
            if (centerX < minCenterX) centerX = minCenterX;
            return cursorInRect(ui,
                                centerX - handleHalf,
                                centerX + handleHalf,
                                centerY - handleHalf,
                                centerY + handleHalf);
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

        void expandAndClampClipX(float laneLeft, float laneRight, float& x0, float& x1) {
            x0 = std::max(laneLeft, x0 - kClipHorizontalPad);
            x1 = std::min(laneRight, x1 + kClipHorizontalPad);
        }

        bool isCursorOverOpenPanel(const BaseSystem& baseSystem, const UIContext& ui) {
            if (!baseSystem.panel) return false;
            const PanelContext& panel = *baseSystem.panel;
            const float x = static_cast<float>(ui.cursorX);
            const float y = static_cast<float>(ui.cursorY);
            auto pickRect = [](const PanelRect& renderRect, const PanelRect& fallbackRect) -> const PanelRect& {
                if (renderRect.w > 0.0f && renderRect.h > 0.0f) return renderRect;
                return fallbackRect;
            };
            auto overRect = [&](const PanelRect& rect) {
                if (rect.w <= 0.0f || rect.h <= 0.0f) return false;
                return x >= rect.x && x <= rect.x + rect.w
                    && y >= rect.y && y <= rect.y + rect.h;
            };
            if (panel.topState > 0.01f && overRect(pickRect(panel.topRenderRect, panel.topRect))) return true;
            if (panel.bottomState > 0.01f && overRect(pickRect(panel.bottomRenderRect, panel.bottomRect))) return true;
            if (panel.leftState > 0.01f && overRect(pickRect(panel.leftRenderRect, panel.leftRect))) return true;
            if (panel.rightState > 0.01f && overRect(pickRect(panel.rightRenderRect, panel.rightRect))) return true;
            return false;
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

        ClipTrimHit findAudioTrimHit(const DawContext& daw,
                                     const UIContext& ui,
                                     const std::vector<int>& audioLaneIndex,
                                     int audioTrackCount,
                                     float laneLeft,
                                     float laneRight,
                                     float laneHalfH,
                                     float rowSpan,
                                     float startY,
                                     double secondsPerScreen) {
            ClipTrimHit hit;
            if (audioTrackCount <= 0 || laneRight <= laneLeft) return hit;

            double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
            double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples <= 0.0) windowSamples = 1.0;

            for (int t = 0; t < audioTrackCount; ++t) {
                const auto& clips = daw.tracks[static_cast<size_t>(t)].clips;
                if (clips.empty()) continue;
                int laneIndex = (t >= 0 && t < static_cast<int>(audioLaneIndex.size())) ? audioLaneIndex[static_cast<size_t>(t)] : -1;
                if (laneIndex < 0) continue;
                float centerY = startY + static_cast<float>(laneIndex) * rowSpan;
                float top = 0.0f;
                float bottom = 0.0f;
                float lipBottom = 0.0f;
                computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);
                if (ui.cursorY < top || ui.cursorY > lipBottom) continue;

                for (size_t ci = 0; ci < clips.size(); ++ci) {
                    const auto& clip = clips[ci];
                    if (clip.length == 0) continue;
                    double clipStart = static_cast<double>(clip.startSample);
                    double clipEnd = static_cast<double>(clip.startSample + clip.length);
                    if (clipEnd <= offsetSamples || clipStart >= offsetSamples + windowSamples) continue;
                    double visibleStart = std::max(clipStart, offsetSamples);
                    double visibleEnd = std::min(clipEnd, offsetSamples + windowSamples);
                    float t0 = static_cast<float>((visibleStart - offsetSamples) / windowSamples);
                    float t1 = static_cast<float>((visibleEnd - offsetSamples) / windowSamples);
                    float x0 = laneLeft + (laneRight - laneLeft) * t0;
                    float x1 = laneLeft + (laneRight - laneLeft) * t1;
                    expandAndClampClipX(laneLeft, laneRight, x0, x1);
                    if (x1 <= x0) continue;
                    if (ui.cursorX < x0 - kTrimEdgeHitWidth || ui.cursorX > x1 + kTrimEdgeHitWidth) continue;

                    float distLeft = std::fabs(static_cast<float>(ui.cursorX) - x0);
                    float distRight = std::fabs(static_cast<float>(ui.cursorX) - x1);
                    bool nearLeft = distLeft <= kTrimEdgeHitWidth;
                    bool nearRight = distRight <= kTrimEdgeHitWidth;
                    if (!nearLeft && !nearRight) continue;

                    bool pickLeft = false;
                    float edgeDist = 0.0f;
                    if (nearLeft && nearRight) {
                        pickLeft = distLeft <= distRight;
                        edgeDist = pickLeft ? distLeft : distRight;
                    } else if (nearLeft) {
                        pickLeft = true;
                        edgeDist = distLeft;
                    } else {
                        pickLeft = false;
                        edgeDist = distRight;
                    }

                    if (!hit.valid || edgeDist < hit.edgeDistance) {
                        hit.valid = true;
                        hit.leftEdge = pickLeft;
                        hit.track = t;
                        hit.clipIndex = static_cast<int>(ci);
                        hit.edgeDistance = edgeDist;
                        hit.clipStart = clip.startSample;
                        hit.clipLength = clip.length;
                        hit.clipSourceOffset = clip.sourceOffset;
                    }
                }
            }
            return hit;
        }
    }

    void ApplyLaneResizeCursor(GLFWwindow* win, bool active) {
        if (!win) return;
        if (active) {
            GLFWcursor* cursor = ensureLaneTrimCursor();
            if (cursor) {
                glfwSetCursor(win, cursor);
                g_laneTrimCursorApplied = true;
            }
        } else if (g_laneTrimCursorApplied) {
            glfwSetCursor(win, nullptr);
            g_laneTrimCursorApplied = false;
        }
    }

    void UpdateDawLaneInput(BaseSystem& baseSystem, std::vector<Entity>&, float dt, GLFWwindow* win) {
        if (!baseSystem.ui || !baseSystem.daw || !baseSystem.level || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        if (baseSystem.midi && baseSystem.midi->pianoRollActive) return;
        if (!DawLaneTimelineSystemLogic::hasDawUiWorld(*baseSystem.level)) return;

        bool allowLaneInput = !isCursorOverOpenPanel(baseSystem, ui);

        DawContext& daw = *baseSystem.daw;
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
        const float handleY = layout.handleY;
        const float handleHalf = layout.handleHalf;
        const float rulerTopY = layout.rulerTopY;
        const float rulerBottomY = layout.rulerBottomY;
        const float rulerLeft = layout.rulerLeft;
        const float rulerRight = layout.rulerRight;
        const float upperRulerTop = layout.upperRulerTop;
        const float upperRulerBottom = layout.upperRulerBottom;
        const float verticalRulerLeft = layout.verticalRulerLeft;
        const float verticalRulerRight = layout.verticalRulerRight;
        const float verticalRulerTop = layout.verticalRulerTop;
        const float verticalRulerBottom = layout.verticalRulerBottom;
        const double secondsPerScreen = layout.secondsPerScreen;
        const int midiTrackCount = baseSystem.midi
            ? static_cast<int>(baseSystem.midi->tracks.size())
            : 0;

        std::vector<int> midiLaneIndex(static_cast<size_t>(std::max(0, midiTrackCount)), -1);
        if (midiTrackCount > 0) {
            if (!daw.laneOrder.empty()) {
                for (size_t laneIdx = 0; laneIdx < daw.laneOrder.size(); ++laneIdx) {
                    const auto& entry = daw.laneOrder[laneIdx];
                    if (entry.type == 1
                        && entry.trackIndex >= 0
                        && entry.trackIndex < midiTrackCount) {
                        midiLaneIndex[static_cast<size_t>(entry.trackIndex)] = static_cast<int>(laneIdx);
                    }
                }
            } else {
                for (int t = 0; t < midiTrackCount; ++t) {
                    midiLaneIndex[static_cast<size_t>(t)] = audioTrackCount + t;
                }
            }
        }

        auto computeDisplayLaneIndex = [&](int laneIndex, int laneType) -> int {
            int displayIndex = laneIndex;
            int previewSlot = -1;
            bool previewingDrag = false;
            if (daw.dragActive && daw.dragLaneType == laneType && daw.dragLaneIndex >= 0) {
                previewSlot = daw.dragDropIndex;
                previewingDrag = true;
            } else if (daw.externalDropActive && daw.externalDropType == laneType) {
                previewSlot = daw.externalDropIndex;
            }
            if (previewSlot < 0) return displayIndex;
            if (previewingDrag) {
                if (laneIndex == daw.dragLaneIndex) return -1;
                if (laneIndex > daw.dragLaneIndex) {
                    displayIndex -= 1;
                }
            }
            if (displayIndex >= previewSlot) {
                displayIndex += 1;
            }
            return displayIndex;
        };

        const bool exportInProgress = daw.exportInProgress.load(std::memory_order_relaxed);
        if (daw.exportMenuOpen || exportInProgress) {
            const ExportDialogLayout exportLayout = computeExportDialogLayout(layout);
            const Rect panelRect = makeRect(exportLayout.x, exportLayout.y, exportLayout.w, exportLayout.h);

            auto closeExportMenu = [&]() {
                daw.exportMenuOpen = false;
                daw.exportSelectedStem = -1;
                g_exportKeyDown.fill(0u);
            };
            auto clampBars = [&]() {
                daw.exportStartBar = std::clamp(daw.exportStartBar, -4096, 4096);
                daw.exportEndBar = std::clamp(daw.exportEndBar, -4096, 4096);
                if (daw.exportEndBar <= daw.exportStartBar) {
                    daw.exportEndBar = daw.exportStartBar + 1;
                }
            };

            if (ui.uiLeftPressed) {
                bool clickedInside = cursorInRect(ui, panelRect);
                if (cursorInRect(ui, exportLayout.closeBtn) || cursorInRect(ui, exportLayout.cancelBtn)) {
                    if (!exportInProgress) {
                        closeExportMenu();
                    }
                    ui.consumeClick = true;
                } else if (!exportInProgress && cursorInRect(ui, exportLayout.startMinus)) {
                    daw.exportStartBar -= 1;
                    clampBars();
                    ui.consumeClick = true;
                } else if (!exportInProgress && cursorInRect(ui, exportLayout.startPlus)) {
                    daw.exportStartBar += 1;
                    clampBars();
                    ui.consumeClick = true;
                } else if (!exportInProgress && cursorInRect(ui, exportLayout.endMinus)) {
                    daw.exportEndBar -= 1;
                    clampBars();
                    ui.consumeClick = true;
                } else if (!exportInProgress && cursorInRect(ui, exportLayout.endPlus)) {
                    daw.exportEndBar += 1;
                    clampBars();
                    ui.consumeClick = true;
                } else if (!exportInProgress && cursorInRect(ui, exportLayout.folderBtn)) {
                    std::string folder = daw.exportFolderPath;
                    if (DawIOSystemLogic::OpenExportFolderDialog(folder)) {
                        daw.exportFolderPath = folder;
                        daw.exportStatusMessage.clear();
                    }
                    ui.consumeClick = true;
                } else if (!exportInProgress && cursorInRect(ui, exportLayout.exportBtn)) {
                    if (DawIOSystemLogic::StartStemExport(baseSystem)) {
                        daw.exportMenuOpen = true;
                        daw.exportStatusMessage = "Exporting stems...";
                    }
                    ui.consumeClick = true;
                } else {
                    int selectedStem = -1;
                    for (int i = 0; i < DawContext::kBusCount; ++i) {
                        if (cursorInRect(ui, exportLayout.stemRows[static_cast<size_t>(i)])) {
                            selectedStem = i;
                            break;
                        }
                    }
                    if (selectedStem >= 0) {
                        daw.exportSelectedStem = selectedStem;
                        ui.consumeClick = true;
                    } else if (!clickedInside && !exportInProgress) {
                        closeExportMenu();
                        ui.consumeClick = true;
                    } else {
                        ui.consumeClick = clickedInside;
                    }
                }
            }

            if (!exportInProgress) {
                updateStemNameTyping(win, daw, isShiftDown(win));
            } else {
                daw.exportSelectedStem = -1;
            }

            g_cmdEShortcutWasDown = false;
            g_cmdShiftMShortcutWasDown = false;
            g_cmdLShortcutWasDown = false;
            g_spaceShortcutWasDown = false;
            g_deleteShortcutWasDown = false;
            g_cmdPrevTakeShortcutWasDown = false;
            g_cmdNextTakeShortcutWasDown = false;
            g_rightMouseWasDown = (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
            ApplyLaneResizeCursor(win, false);
            return;
        }

        if (daw.settingsMenuOpen) {
            DawIOSystemLogic::EnsureThemeState(baseSystem);
            const SettingsDialogLayout settingsLayout = computeSettingsDialogLayout(layout);

            auto closeSettingsMenu = [&]() {
                daw.settingsMenuOpen = false;
                daw.themeCreateMode = false;
                daw.themeEditField = -1;
                g_settingsKeyDown.fill(0u);
            };
            auto selectedTheme = [&]() -> const DawThemePreset* {
                if (daw.settingsSelectedTheme < 0
                    || daw.settingsSelectedTheme >= static_cast<int>(daw.themes.size())) {
                    return nullptr;
                }
                return &daw.themes[static_cast<size_t>(daw.settingsSelectedTheme)];
            };
            auto selectedThemeDeleteProtected = [&]() -> bool {
                const DawThemePreset* preset = selectedTheme();
                if (!preset) return false;
                return preset->name == "Default"
                    || preset->name == "Default 2"
                    || preset->name == "Default 3";
            };

            if (ui.uiLeftPressed) {
                const bool clickedInside = cursorInRect(ui, settingsLayout.panelRect);
                if (cursorInRect(ui, settingsLayout.closeBtn)) {
                    closeSettingsMenu();
                    ui.consumeClick = true;
                } else if (!clickedInside) {
                    closeSettingsMenu();
                    ui.consumeClick = true;
                } else if (daw.themeCreateMode) {
                    if (cursorInRect(ui, settingsLayout.nameField)) {
                        daw.themeEditField = 0;
                    } else if (cursorInRect(ui, settingsLayout.bgField)) {
                        daw.themeEditField = 1;
                    } else if (cursorInRect(ui, settingsLayout.panelField)) {
                        daw.themeEditField = 2;
                    } else if (cursorInRect(ui, settingsLayout.buttonField)) {
                        daw.themeEditField = 3;
                    } else if (cursorInRect(ui, settingsLayout.pianoField)) {
                        daw.themeEditField = 4;
                    } else if (cursorInRect(ui, settingsLayout.pianoAccentField)) {
                        daw.themeEditField = 5;
                    } else if (cursorInRect(ui, settingsLayout.laneField)) {
                        daw.themeEditField = 6;
                    } else if (cursorInRect(ui, settingsLayout.backBtn)) {
                        daw.themeCreateMode = false;
                        daw.themeEditField = -1;
                        daw.themeStatusMessage.clear();
                    } else if (cursorInRect(ui, settingsLayout.saveBtn)) {
                        std::string status;
                        if (DawIOSystemLogic::SaveThemeFromDraft(baseSystem,
                                                                 daw.themeDraftName,
                                                                 daw.themeDraftBackgroundHex,
                                                                 daw.themeDraftPanelHex,
                                                                 daw.themeDraftButtonHex,
                                                                 daw.themeDraftPianoRollHex,
                                                                 daw.themeDraftPianoRollAccentHex,
                                                                 daw.themeDraftLaneHex,
                                                                 status)) {
                            DawIOSystemLogic::ApplyThemeByIndex(baseSystem, daw.settingsSelectedTheme, true);
                            daw.themeCreateMode = false;
                            daw.themeEditField = -1;
                        }
                        daw.themeStatusMessage = status;
                    }
                    ui.consumeClick = true;
                } else {
                    int clickedTheme = -1;
                    if (cursorInRect(ui, settingsLayout.listRect)) {
                        const int maxRows = std::max(0, static_cast<int>((settingsLayout.listRect.bottom - settingsLayout.listRect.top
                            - settingsLayout.listPad * 2.0f) / settingsLayout.listRowHeight));
                        for (int i = 0; i < static_cast<int>(daw.themes.size()) && i < maxRows; ++i) {
                            if (cursorInRect(ui, settingsThemeRowRect(settingsLayout, i))) {
                                clickedTheme = i;
                                break;
                            }
                        }
                    }
                    if (clickedTheme >= 0) {
                        daw.settingsSelectedTheme = clickedTheme;
                        daw.themeStatusMessage.clear();
                    } else if (cursorInRect(ui, settingsLayout.applyBtn)) {
                        if (DawIOSystemLogic::ApplyThemeByIndex(baseSystem, daw.settingsSelectedTheme, true)) {
                            daw.themeStatusMessage = "Theme applied.";
                        } else {
                            daw.themeStatusMessage = "Apply failed.";
                        }
                    } else if (cursorInRect(ui, settingsLayout.createBtn)) {
                        DawIOSystemLogic::BeginThemeDraftFromDefault(baseSystem);
                        daw.themeCreateMode = true;
                        daw.themeEditField = 0;
                        daw.themeStatusMessage.clear();
                        g_settingsKeyDown.fill(0u);
                    } else if (cursorInRect(ui, settingsLayout.editBtn)) {
                        const DawThemePreset* preset = selectedTheme();
                        if (!preset || preset->name == "Default") {
                            daw.themeStatusMessage = "Default theme cannot be edited.";
                        } else {
                            daw.themeDraftName = preset->name;
                            daw.themeDraftBackgroundHex = DawIOSystemLogic::ThemeColorToHex(preset->background);
                            daw.themeDraftPanelHex = DawIOSystemLogic::ThemeColorToHex(preset->panel);
                            daw.themeDraftButtonHex = DawIOSystemLogic::ThemeColorToHex(preset->button);
                            daw.themeDraftPianoRollHex = DawIOSystemLogic::ThemeColorToHex(preset->pianoRoll);
                            daw.themeDraftPianoRollAccentHex = DawIOSystemLogic::ThemeColorToHex(preset->pianoRollAccent);
                            daw.themeDraftLaneHex = DawIOSystemLogic::ThemeColorToHex(preset->lane);
                            daw.themeCreateMode = true;
                            daw.themeEditField = 0;
                            daw.themeStatusMessage.clear();
                            g_settingsKeyDown.fill(0u);
                        }
                    } else if (cursorInRect(ui, settingsLayout.deleteBtn)) {
                        if (selectedThemeDeleteProtected()) {
                            daw.themeStatusMessage = "Default, Default 2, and Default 3 cannot be deleted.";
                        } else {
                            std::string status;
                            if (!DawIOSystemLogic::RemoveThemeByIndex(baseSystem,
                                                                      daw.settingsSelectedTheme,
                                                                      status)) {
                                daw.themeStatusMessage = status.empty() ? "Delete failed." : status;
                            } else {
                                daw.themeStatusMessage = status;
                            }
                        }
                    } else if (cursorInRect(ui, settingsLayout.tabTheme)) {
                        daw.settingsTab = 0;
                    }
                    ui.consumeClick = true;
                }
            } else if (cursorInRect(ui, settingsLayout.panelRect)) {
                ui.consumeClick = true;
            }

            if (daw.themeCreateMode) {
                const bool shiftDown = isShiftDown(win);
                if (settingsKeyPressed(win, GLFW_KEY_TAB)) {
                    int next = daw.themeEditField;
                    if (next < 0 || next > 6) next = 0;
                    daw.themeEditField = (next + 1) % 7;
                }
                if (settingsKeyPressed(win, GLFW_KEY_ENTER) || settingsKeyPressed(win, GLFW_KEY_KP_ENTER)) {
                    std::string status;
                    if (DawIOSystemLogic::SaveThemeFromDraft(baseSystem,
                                                             daw.themeDraftName,
                                                             daw.themeDraftBackgroundHex,
                                                             daw.themeDraftPanelHex,
                                                             daw.themeDraftButtonHex,
                                                             daw.themeDraftPianoRollHex,
                                                             daw.themeDraftPianoRollAccentHex,
                                                             daw.themeDraftLaneHex,
                                                             status)) {
                        DawIOSystemLogic::ApplyThemeByIndex(baseSystem, daw.settingsSelectedTheme, true);
                        daw.themeCreateMode = false;
                        daw.themeEditField = -1;
                    }
                    daw.themeStatusMessage = status;
                } else {
                    updateThemeDraftTyping(win, daw, shiftDown);
                }
            } else {
                daw.themeEditField = -1;
                g_settingsKeyDown.fill(0u);
            }

            g_cmdEShortcutWasDown = false;
            g_cmdShiftMShortcutWasDown = false;
            g_cmdLShortcutWasDown = false;
            g_spaceShortcutWasDown = false;
            g_deleteShortcutWasDown = false;
            g_cmdPrevTakeShortcutWasDown = false;
            g_cmdNextTakeShortcutWasDown = false;
            g_rightMouseWasDown = (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
            ApplyLaneResizeCursor(win, false);
            return;
        }

        auto closeAutomationParamMenu = [&]() {
            daw.automationParamMenuOpen = false;
            daw.automationParamMenuTrack = -1;
            daw.automationParamMenuHoverIndex = -1;
            daw.automationParamMenuLabels.clear();
        };

        if (daw.automationParamMenuOpen) {
            const AutomationParamMenuLayout paramMenuLayout = computeAutomationParamMenuLayout(daw, layout);
            if (!paramMenuLayout.valid) {
                closeAutomationParamMenu();
            } else {
                const bool buttonHit = cursorInRect(ui, paramMenuLayout.buttonRect);
                const bool menuHit = cursorInRect(ui, paramMenuLayout.menuRect);
                daw.automationParamMenuHoverIndex = -1;
                if (menuHit) {
                    float localY = static_cast<float>(ui.cursorY) - paramMenuLayout.menuRect.top - paramMenuLayout.padding;
                    int hover = static_cast<int>(localY / paramMenuLayout.rowHeight);
                    if (hover >= 0 && hover < static_cast<int>(daw.automationParamMenuLabels.size())) {
                        daw.automationParamMenuHoverIndex = hover;
                    }
                }
                if (ui.uiLeftPressed) {
                    if (menuHit && daw.automationParamMenuHoverIndex >= 0) {
                        int trackIndex = daw.automationParamMenuTrack;
                        int slot = daw.automationParamMenuHoverIndex;
                        closeAutomationParamMenu();
                        if (ui.pendingActionType.empty()) {
                            ui.pendingActionType = "DawAutomationTrack";
                            ui.pendingActionKey = "target_param_pick";
                            ui.pendingActionValue = std::to_string(trackIndex) + ":" + std::to_string(slot);
                        }
                        ui.consumeClick = true;
                    } else if (buttonHit) {
                        // Let the button click toggle-close through the normal action pipeline.
                        ui.consumeClick = true;
                    } else {
                        closeAutomationParamMenu();
                        ui.consumeClick = true;
                    }
                } else if (menuHit || buttonHit) {
                    ui.consumeClick = true;
                }
            }
        }

        bool cmdDownNow = isCommandDown(win);
        bool shiftDownNow = isShiftDown(win);
        bool cmdEShortcutNow = cmdDownNow
            && !shiftDownNow
            && glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS;
        if (cmdEShortcutNow && !g_cmdEShortcutWasDown) {
            uint64_t splitSample = daw.playheadSample.load(std::memory_order_relaxed);
            if (!splitSelectedAudioClipAtPlayhead(baseSystem, daw, splitSample)) {
                splitSelectedMidiClipAtPlayhead(baseSystem, daw, splitSample);
            }
        }
        g_cmdEShortcutWasDown = cmdEShortcutNow;

        bool cmdShiftMShortcutNow = cmdDownNow
            && shiftDownNow
            && glfwGetKey(win, GLFW_KEY_M) == GLFW_PRESS;
        if (cmdShiftMShortcutNow && !g_cmdShiftMShortcutWasDown) {
            bool createdMidi = createMidiClipsFromTimelineSelection(baseSystem, daw);
            bool createdAutomation = createAutomationClipsFromTimelineSelection(baseSystem, daw);
            (void)createdMidi;
            (void)createdAutomation;
        }
        g_cmdShiftMShortcutWasDown = cmdShiftMShortcutNow;

        bool cmdLShortcutNow = cmdDownNow
            && !shiftDownNow
            && glfwGetKey(win, GLFW_KEY_L) == GLFW_PRESS;
        if (cmdLShortcutNow && !g_cmdLShortcutWasDown) {
            if (daw.timelineSelectionActive) {
                uint64_t selStart = std::min(daw.timelineSelectionStartSample, daw.timelineSelectionEndSample);
                uint64_t selEnd = std::max(daw.timelineSelectionStartSample, daw.timelineSelectionEndSample);
                if (selEnd > selStart) {
                    daw.loopStartSamples = selStart;
                    daw.loopEndSamples = selEnd;
                    daw.loopEnabled.store(true, std::memory_order_relaxed);
                }
            }
        }
        g_cmdLShortcutWasDown = cmdLShortcutNow;

        bool cmdPrevTakeShortcutNow = cmdDownNow
            && !shiftDownNow
            && glfwGetKey(win, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS;
        bool cmdNextTakeShortcutNow = cmdDownNow
            && !shiftDownNow
            && glfwGetKey(win, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS;
        auto updateTimelineSelectionForClip = [&](uint64_t start, uint64_t length, int laneType, int laneTrack) {
            int laneIndex = -1;
            if (!daw.laneOrder.empty()) {
                for (size_t i = 0; i < daw.laneOrder.size(); ++i) {
                    const auto& entry = daw.laneOrder[i];
                    if (entry.type == laneType && entry.trackIndex == laneTrack) {
                        laneIndex = static_cast<int>(i);
                        break;
                    }
                }
            } else {
                laneIndex = (laneType == 0)
                    ? laneTrack
                    : static_cast<int>(daw.tracks.size()) + laneTrack;
            }
            if (laneIndex < 0) laneIndex = 0;
            daw.timelineSelectionStartSample = start;
            daw.timelineSelectionEndSample = start + length;
            daw.timelineSelectionStartLane = laneIndex;
            daw.timelineSelectionEndLane = laneIndex;
            daw.timelineSelectionAnchorSample = start;
            daw.timelineSelectionAnchorLane = laneIndex;
            daw.timelineSelectionActive = (length > 0);
            daw.timelineSelectionDragActive = false;
            daw.timelineSelectionFromPlayhead = false;
        };
        auto applyAudioCompFromTake = [&](int trackIndex,
                                          int takeIndex,
                                          uint64_t startSample,
                                          uint64_t endSample) -> bool {
            if (trackIndex < 0 || trackIndex >= static_cast<int>(daw.tracks.size())) return false;
            DawTrack& track = daw.tracks[static_cast<size_t>(trackIndex)];
            if (takeIndex < 0 || takeIndex >= static_cast<int>(track.loopTakeClips.size())) return false;
            const DawClip& take = track.loopTakeClips[static_cast<size_t>(takeIndex)];
            if (take.length == 0) return false;
            uint64_t selStart = std::min(startSample, endSample);
            uint64_t selEnd = std::max(startSample, endSample);
            uint64_t takeStart = take.startSample;
            uint64_t takeEnd = take.startSample + take.length;
            uint64_t clipStart = std::max(selStart, takeStart);
            uint64_t clipEnd = std::min(selEnd, takeEnd);
            if (clipEnd <= clipStart) return false;
            DawClip compClip = take;
            compClip.startSample = clipStart;
            compClip.length = clipEnd - clipStart;
            compClip.sourceOffset = take.sourceOffset + (clipStart - takeStart);
            DawClipSystemLogic::TrimClipsForNewClip(track, compClip);
            track.clips.push_back(compClip);
            std::sort(track.clips.begin(), track.clips.end(), [](const DawClip& a, const DawClip& b) {
                if (a.startSample == b.startSample) return a.sourceOffset < b.sourceOffset;
                return a.startSample < b.startSample;
            });
            int selectedIndex = -1;
            for (int i = static_cast<int>(track.clips.size()) - 1; i >= 0; --i) {
                const DawClip& candidate = track.clips[static_cast<size_t>(i)];
                if (candidate.audioId == compClip.audioId
                    && candidate.startSample == compClip.startSample
                    && candidate.length == compClip.length
                    && candidate.sourceOffset == compClip.sourceOffset
                    && candidate.takeId == compClip.takeId) {
                    selectedIndex = i;
                    break;
                }
            }
            DawClipSystemLogic::RebuildTrackCacheFromClips(daw, track);
            daw.selectedClipTrack = trackIndex;
            daw.selectedClipIndex = selectedIndex;
            daw.selectedAutomationClipTrack = -1;
            daw.selectedAutomationClipIndex = -1;
            daw.selectedLaneType = 0;
            daw.selectedLaneTrack = trackIndex;
            if (baseSystem.midi) {
                baseSystem.midi->selectedClipTrack = -1;
                baseSystem.midi->selectedClipIndex = -1;
            }
            updateTimelineSelectionForClip(compClip.startSample, compClip.length, 0, trackIndex);
            return true;
        };
        auto applyMidiCompFromTake = [&](int trackIndex,
                                         int takeIndex,
                                         uint64_t startSample,
                                         uint64_t endSample) -> bool {
            if (!baseSystem.midi) return false;
            MidiContext& midi = *baseSystem.midi;
            if (trackIndex < 0 || trackIndex >= static_cast<int>(midi.tracks.size())) return false;
            MidiTrack& track = midi.tracks[static_cast<size_t>(trackIndex)];
            if (takeIndex < 0 || takeIndex >= static_cast<int>(track.loopTakeClips.size())) return false;
            const MidiClip& take = track.loopTakeClips[static_cast<size_t>(takeIndex)];
            if (take.length == 0) return false;
            uint64_t selStart = std::min(startSample, endSample);
            uint64_t selEnd = std::max(startSample, endSample);
            uint64_t takeStart = take.startSample;
            uint64_t takeEnd = take.startSample + take.length;
            uint64_t clipStart = std::max(selStart, takeStart);
            uint64_t clipEnd = std::min(selEnd, takeEnd);
            if (clipEnd <= clipStart) return false;
            MidiClip compClip = take;
            compClip.startSample = clipStart;
            compClip.length = clipEnd - clipStart;
            compClip.notes.clear();
            compClip.notes.reserve(take.notes.size());
            for (const auto& note : take.notes) {
                if (note.length == 0) continue;
                uint64_t noteAbsStart = takeStart + note.startSample;
                uint64_t noteAbsEnd = noteAbsStart + note.length;
                if (noteAbsEnd <= clipStart || noteAbsStart >= clipEnd) continue;
                uint64_t clippedStart = std::max(noteAbsStart, clipStart);
                uint64_t clippedEnd = std::min(noteAbsEnd, clipEnd);
                if (clippedEnd <= clippedStart) continue;
                MidiNote out = note;
                out.startSample = clippedStart - clipStart;
                out.length = clippedEnd - clippedStart;
                compClip.notes.push_back(out);
            }
            trimMidiClipsForNewClip(track, compClip);
            track.clips.push_back(compClip);
            sortMidiClipsByStart(track.clips);
            int selectedIndex = -1;
            for (int i = static_cast<int>(track.clips.size()) - 1; i >= 0; --i) {
                const MidiClip& candidate = track.clips[static_cast<size_t>(i)];
                if (candidate.startSample == compClip.startSample
                    && candidate.length == compClip.length
                    && candidate.takeId == compClip.takeId
                    && candidate.notes.size() == compClip.notes.size()) {
                    selectedIndex = i;
                    break;
                }
            }
            midi.selectedTrackIndex = trackIndex;
            midi.selectedClipTrack = trackIndex;
            midi.selectedClipIndex = selectedIndex;
            daw.selectedClipTrack = -1;
            daw.selectedClipIndex = -1;
            daw.selectedAutomationClipTrack = -1;
            daw.selectedAutomationClipIndex = -1;
            daw.selectedLaneType = 1;
            daw.selectedLaneTrack = trackIndex;
            updateTimelineSelectionForClip(compClip.startSample, compClip.length, 1, trackIndex);
            return true;
        };
        auto resolveTakeTarget = [&](int& laneType, int& laneTrack) {
            laneType = -1;
            laneTrack = -1;
            if (daw.selectedLaneType == 0 && daw.selectedLaneTrack >= 0
                && daw.selectedLaneTrack < static_cast<int>(daw.tracks.size())) {
                laneType = 0;
                laneTrack = daw.selectedLaneTrack;
                return;
            }
            if (baseSystem.midi && daw.selectedLaneType == 1 && daw.selectedLaneTrack >= 0
                && daw.selectedLaneTrack < static_cast<int>(baseSystem.midi->tracks.size())) {
                laneType = 1;
                laneTrack = daw.selectedLaneTrack;
                return;
            }
            if (daw.selectedClipTrack >= 0 && daw.selectedClipTrack < static_cast<int>(daw.tracks.size())) {
                laneType = 0;
                laneTrack = daw.selectedClipTrack;
                return;
            }
            if (baseSystem.midi && baseSystem.midi->selectedClipTrack >= 0
                && baseSystem.midi->selectedClipTrack < static_cast<int>(baseSystem.midi->tracks.size())) {
                laneType = 1;
                laneTrack = baseSystem.midi->selectedClipTrack;
                return;
            }
        };
        auto applyTakeCycle = [&](int direction) {
            int targetType = -1;
            int targetTrack = -1;
            resolveTakeTarget(targetType, targetTrack);
            if (targetType == 0 && targetTrack >= 0) {
                if (!DawClipSystemLogic::CycleTrackLoopTake(daw, targetTrack, direction)) return;
                DawTrack& track = daw.tracks[static_cast<size_t>(targetTrack)];
                int activeTake = track.activeLoopTakeIndex;
                if (activeTake < 0 || activeTake >= static_cast<int>(track.loopTakeClips.size())) return;
                const DawClip& activeClip = track.loopTakeClips[static_cast<size_t>(activeTake)];
                int selectedIndex = -1;
                for (size_t i = 0; i < track.clips.size(); ++i) {
                    const DawClip& candidate = track.clips[i];
                    if (candidate.audioId == activeClip.audioId
                        && candidate.startSample == activeClip.startSample
                        && candidate.length == activeClip.length
                        && candidate.sourceOffset == activeClip.sourceOffset
                        && candidate.takeId == activeClip.takeId) {
                        selectedIndex = static_cast<int>(i);
                        break;
                    }
                }
                if (selectedIndex < 0) return;
                daw.selectedClipTrack = targetTrack;
                daw.selectedClipIndex = selectedIndex;
                daw.selectedAutomationClipTrack = -1;
                daw.selectedAutomationClipIndex = -1;
                daw.selectedLaneType = 0;
                daw.selectedLaneTrack = targetTrack;
                if (baseSystem.midi) {
                    baseSystem.midi->selectedClipTrack = -1;
                    baseSystem.midi->selectedClipIndex = -1;
                }
                updateTimelineSelectionForClip(activeClip.startSample, activeClip.length, 0, targetTrack);
                return;
            }
            if (targetType == 1 && targetTrack >= 0 && baseSystem.midi) {
                MidiContext& midi = *baseSystem.midi;
                if (!MidiTransportSystemLogic::CycleTrackLoopTake(midi, targetTrack, direction)) return;
                MidiTrack& track = midi.tracks[static_cast<size_t>(targetTrack)];
                int activeTake = track.activeLoopTakeIndex;
                if (activeTake < 0 || activeTake >= static_cast<int>(track.loopTakeClips.size())) return;
                const MidiClip& activeClip = track.loopTakeClips[static_cast<size_t>(activeTake)];
                int selectedIndex = -1;
                for (size_t i = 0; i < track.clips.size(); ++i) {
                    const MidiClip& candidate = track.clips[i];
                    if (candidate.startSample != activeClip.startSample) continue;
                    if (candidate.length != activeClip.length) continue;
                    if (candidate.notes.size() != activeClip.notes.size()) continue;
                    if (candidate.takeId != activeClip.takeId) continue;
                    selectedIndex = static_cast<int>(i);
                    break;
                }
                if (selectedIndex < 0) return;
                midi.selectedTrackIndex = targetTrack;
                midi.selectedClipTrack = targetTrack;
                midi.selectedClipIndex = selectedIndex;
                daw.selectedClipTrack = -1;
                daw.selectedClipIndex = -1;
                daw.selectedAutomationClipTrack = -1;
                daw.selectedAutomationClipIndex = -1;
                daw.selectedLaneType = 1;
                daw.selectedLaneTrack = targetTrack;
                updateTimelineSelectionForClip(activeClip.startSample, activeClip.length, 1, targetTrack);
            }
        };
        if (cmdPrevTakeShortcutNow && !g_cmdPrevTakeShortcutWasDown) {
            applyTakeCycle(-1);
        }
        if (cmdNextTakeShortcutNow && !g_cmdNextTakeShortcutWasDown) {
            applyTakeCycle(+1);
        }
        g_cmdPrevTakeShortcutWasDown = cmdPrevTakeShortcutNow;
        g_cmdNextTakeShortcutWasDown = cmdNextTakeShortcutNow;

        bool deleteShortcutNow = glfwGetKey(win, GLFW_KEY_DELETE) == GLFW_PRESS
            || glfwGetKey(win, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
        if (deleteShortcutNow && !g_deleteShortcutWasDown) {
            if (!deleteSelectedAudioClip(baseSystem, daw)) {
                if (!deleteSelectedMidiClip(baseSystem, daw)) {
                    deleteSelectedAutomationClip(daw);
                }
            }
        }
        g_deleteShortcutWasDown = deleteShortcutNow;

        bool spaceShortcutNow = (!cmdDownNow)
            && glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spaceShortcutNow && !g_spaceShortcutWasDown) {
            if (ui.pendingActionType.empty()) {
                if (daw.transportPlaying.load(std::memory_order_relaxed)
                    || daw.transportRecording.load(std::memory_order_relaxed)) {
                    ui.pendingActionType = "DawTransport";
                    ui.pendingActionKey = "stop";
                    ui.pendingActionValue.clear();
                } else {
                    ui.pendingActionType = "DawTransport";
                    ui.pendingActionKey = "play";
                    ui.pendingActionValue.clear();
                }
            }
        }
        g_spaceShortcutWasDown = spaceShortcutNow;
        const bool rightMouseDownNow = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        const bool rightMousePressedNow = rightMouseDownNow && !g_rightMouseWasDown;

        bool bpmDragPressed = false;
        bool hasBpmDragControl = false;
        float bpmLeft = 0.0f;
        float bpmRight = 0.0f;
        float bpmTop = 0.0f;
        float bpmBottom = 0.0f;
        for (auto* instPtr : daw.tempoInstances) {
            if (!instPtr) continue;
            EntityInstance& inst = *instPtr;
            if (inst.actionKey != "bpm_drag") continue;
            glm::vec2 halfSize(inst.size.x, inst.size.y);
            bpmLeft = inst.position.x - halfSize.x;
            bpmRight = inst.position.x + halfSize.x;
            bpmTop = inst.position.y - halfSize.y;
            bpmBottom = inst.position.y + halfSize.y;
            hasBpmDragControl = true;
            break;
        }

        if (ui.uiLeftPressed && hasBpmDragControl
            && cursorInRect(ui, bpmLeft, bpmRight, bpmTop, bpmBottom)) {
            daw.bpmDragActive = true;
            daw.bpmDragLastY = ui.cursorY;
            bpmDragPressed = true;
            ui.consumeClick = true;
        }

        if (daw.bpmDragActive) {
            if (!ui.uiLeftDown) {
                daw.bpmDragActive = false;
            } else {
                double dy = ui.cursorY - daw.bpmDragLastY;
                daw.bpmDragLastY = ui.cursorY;
                double bpm = daw.bpm.load(std::memory_order_relaxed);
                // Positive dy means dragging down -> lower BPM.
                const double bpmPerPixel = 0.15;
                bpm -= dy * bpmPerPixel;
                bpm = std::clamp(bpm, 40.0, 240.0);
                daw.bpm.store(bpm, std::memory_order_relaxed);
                ui.consumeClick = true;
            }
        }

        bool playheadPressed = false;
        if (allowLaneInput && ui.uiLeftPressed && !daw.bpmDragActive && !bpmDragPressed
            && !daw.verticalRulerDragActive) {
            double playheadSec = static_cast<double>(daw.playheadSample.load(std::memory_order_relaxed))
                / static_cast<double>(daw.sampleRate);
            double offsetSec = static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate);
            double tNorm = secondsPerScreen > 0.0 ? (playheadSec - offsetSec) / secondsPerScreen : 0.0;
            tNorm = std::clamp(tNorm, 0.0, 1.0);
            float playheadX = static_cast<float>(laneLeft + (laneRight - laneLeft) * tNorm);
            float left = playheadX - handleHalf;
            float right = playheadX + handleHalf;
            float top = handleY - handleHalf;
            float bottom = handleY + handleHalf;
            if (cursorInRect(ui, left, right, top, bottom)) {
                daw.playheadDragActive = true;
                daw.playheadDragOffsetX = playheadX - static_cast<float>(ui.cursorX);
                playheadPressed = true;
                ui.consumeClick = true;
            }
        }

        bool rulerPressed = false;
        if (allowLaneInput && ui.uiLeftPressed && !playheadPressed && !daw.bpmDragActive && !bpmDragPressed
            && !daw.verticalRulerDragActive) {
            if (cursorInRect(ui, rulerLeft, rulerRight, rulerTopY, rulerBottomY)) {
                daw.rulerDragActive = true;
                daw.rulerDragStartY = ui.cursorY;
                daw.rulerDragLastX = ui.cursorX;
                daw.rulerDragLastY = ui.cursorY;
                daw.rulerDragStartSeconds = secondsPerScreen;
                daw.rulerDragAccumDY = 0.0;
                daw.rulerDragEdgeDirection = 0;
                double anchorT = (laneRight > laneLeft)
                    ? (static_cast<double>(ui.cursorX) - laneLeft) / (laneRight - laneLeft)
                    : 0.0;
                daw.rulerDragAnchorT = std::clamp(anchorT, 0.0, 1.0);
                double offsetSec = static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate);
                daw.rulerDragAnchorTimeSec = offsetSec + daw.rulerDragAnchorT * secondsPerScreen;
                rulerPressed = true;
                ui.consumeClick = true;
            }
        }
        bool verticalRulerPressed = false;
        if (allowLaneInput && ui.uiLeftPressed && !playheadPressed && !rulerPressed
            && !daw.bpmDragActive && !bpmDragPressed) {
            if (cursorInRect(ui, verticalRulerLeft, verticalRulerRight, verticalRulerTop, verticalRulerBottom)) {
                daw.verticalRulerDragActive = true;
                daw.verticalRulerDragLastX = ui.cursorX;
                daw.verticalRulerDragLastY = ui.cursorY;
                daw.verticalRulerDragStartLaneHeight = std::clamp(daw.timelineLaneHeight, 24.0f, 180.0f);
                daw.verticalRulerDragAccumDX = 0.0;
                daw.verticalRulerDragXEdgeDirection = 0;
                daw.verticalRulerDragEdgeDirection = 0;
                verticalRulerPressed = true;
                ui.consumeClick = true;
            }
        }

        const auto audioLaneIndex = DawLaneTimelineSystemLogic::BuildAudioLaneIndex(daw, audioTrackCount);
        ClipTrimHit trimHover;
        bool trimCursorWanted = false;
        if (allowLaneInput && !daw.clipDragActive && !daw.dragActive && !daw.dragPending) {
            trimHover = findAudioTrimHit(daw,
                                         ui,
                                         audioLaneIndex,
                                         audioTrackCount,
                                         laneLeft,
                                         laneRight,
                                         laneHalfH,
                                         rowSpan,
                                         startY,
                                         secondsPerScreen);
            trimCursorWanted = trimHover.valid;
        }

        if (laneCount > 0 && allowLaneInput && rightMousePressedNow
            && !daw.clipDragActive && !daw.clipTrimActive && !daw.dragActive && !daw.dragPending
            && !daw.takeCompDragActive) {
            int handleLaneIndex = -1;
            for (int laneIdx = 0; laneIdx < laneCount; ++laneIdx) {
                float centerY = startY + static_cast<float>(laneIdx) * rowSpan;
                if (cursorInTrackHandleRect(ui, laneLeft, laneRight, centerY, laneHalfH)) {
                    handleLaneIndex = laneIdx;
                    break;
                }
            }
            if (handleLaneIndex >= 0) {
                int laneType = 0;
                int laneTrack = handleLaneIndex;
                if (!daw.laneOrder.empty() && handleLaneIndex < static_cast<int>(daw.laneOrder.size())) {
                    const auto& entry = daw.laneOrder[static_cast<size_t>(handleLaneIndex)];
                    laneType = entry.type;
                    laneTrack = entry.trackIndex;
                } else if (handleLaneIndex >= audioTrackCount) {
                    laneType = 1;
                    laneTrack = handleLaneIndex - audioTrackCount;
                }
                bool wasExpanded = false;
                if (laneType == 0
                    && laneTrack >= 0
                    && laneTrack < static_cast<int>(daw.tracks.size())) {
                    wasExpanded = daw.tracks[static_cast<size_t>(laneTrack)].takeStackExpanded;
                } else if (laneType == 1 && baseSystem.midi
                           && laneTrack >= 0
                           && laneTrack < static_cast<int>(baseSystem.midi->tracks.size())) {
                    wasExpanded = baseSystem.midi->tracks[static_cast<size_t>(laneTrack)].takeStackExpanded;
                }
                for (auto& track : daw.tracks) {
                    track.takeStackExpanded = false;
                }
                if (baseSystem.midi) {
                    for (auto& track : baseSystem.midi->tracks) {
                        track.takeStackExpanded = false;
                    }
                }
                if (!wasExpanded) {
                    if (laneType == 0
                        && laneTrack >= 0
                        && laneTrack < static_cast<int>(daw.tracks.size())) {
                        daw.tracks[static_cast<size_t>(laneTrack)].takeStackExpanded = true;
                    } else if (laneType == 1 && baseSystem.midi
                               && laneTrack >= 0
                               && laneTrack < static_cast<int>(baseSystem.midi->tracks.size())) {
                        baseSystem.midi->tracks[static_cast<size_t>(laneTrack)].takeStackExpanded = true;
                    }
                }
                daw.selectedLaneIndex = handleLaneIndex;
                daw.selectedLaneType = laneType;
                daw.selectedLaneTrack = laneTrack;
            }
        }

        int takeHitType = -1;
        int takeHitTrack = -1;
        int takeHitIndex = -1;
        if (allowLaneInput && !daw.takeCompDragActive
            && !daw.clipDragActive && !daw.clipTrimActive
            && !daw.dragActive && !daw.dragPending
            && laneCount > 0
            && ui.cursorX >= laneLeft && ui.cursorX <= laneRight) {
            float rowHeight = takeRowHeight(laneHalfH);
            for (int t = 0; t < audioTrackCount && takeHitType < 0; ++t) {
                if (t < 0 || t >= static_cast<int>(daw.tracks.size())) continue;
                const DawTrack& track = daw.tracks[static_cast<size_t>(t)];
                if (!track.takeStackExpanded || track.loopTakeClips.empty()) continue;
                int laneIndex = (t >= 0 && t < static_cast<int>(audioLaneIndex.size()))
                    ? audioLaneIndex[static_cast<size_t>(t)]
                    : -1;
                if (laneIndex < 0) continue;
                int displayIndex = computeDisplayLaneIndex(laneIndex, 0);
                if (displayIndex < 0) continue;
                float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                float clipTop = 0.0f;
                float clipBottom = 0.0f;
                float lipBottom = 0.0f;
                computeClipRect(centerY, laneHalfH, clipTop, clipBottom, lipBottom);
                (void)clipTop;
                (void)lipBottom;
                float rowStartY = clipBottom + kTakeRowGap;
                for (size_t i = 0; i < track.loopTakeClips.size(); ++i) {
                    float top = rowStartY + static_cast<float>(i) * (rowHeight + kTakeRowSpacing);
                    float bottom = top + rowHeight;
                    if (ui.cursorY >= top && ui.cursorY <= bottom) {
                        takeHitType = 0;
                        takeHitTrack = t;
                        takeHitIndex = static_cast<int>(i);
                        break;
                    }
                }
            }
            if (takeHitType < 0 && baseSystem.midi) {
                for (int t = 0; t < midiTrackCount && takeHitType < 0; ++t) {
                    if (t < 0 || t >= static_cast<int>(baseSystem.midi->tracks.size())) continue;
                    const MidiTrack& track = baseSystem.midi->tracks[static_cast<size_t>(t)];
                    if (!track.takeStackExpanded || track.loopTakeClips.empty()) continue;
                    int laneIndex = (t >= 0 && t < static_cast<int>(midiLaneIndex.size()))
                        ? midiLaneIndex[static_cast<size_t>(t)]
                        : -1;
                    if (laneIndex < 0) continue;
                    int displayIndex = computeDisplayLaneIndex(laneIndex, 1);
                    if (displayIndex < 0) continue;
                    float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                    float clipTop = 0.0f;
                    float clipBottom = 0.0f;
                    float lipBottom = 0.0f;
                    computeClipRect(centerY, laneHalfH, clipTop, clipBottom, lipBottom);
                    (void)clipTop;
                    (void)lipBottom;
                    float rowStartY = clipBottom + kTakeRowGap;
                    for (size_t i = 0; i < track.loopTakeClips.size(); ++i) {
                        float top = rowStartY + static_cast<float>(i) * (rowHeight + kTakeRowSpacing);
                        float bottom = top + rowHeight;
                        if (ui.cursorY >= top && ui.cursorY <= bottom) {
                            takeHitType = 1;
                            takeHitTrack = t;
                            takeHitIndex = static_cast<int>(i);
                            break;
                        }
                    }
                }
            }
        }

        if (takeHitType >= 0
            && allowLaneInput
            && ui.uiLeftPressed
            && !ui.consumeClick
            && !playheadPressed
            && !rulerPressed
            && !verticalRulerPressed
            && !daw.bpmDragActive
            && !bpmDragPressed
            && !daw.clipDragActive
            && !daw.clipTrimActive
            && !daw.dragActive
            && !daw.dragPending) {
            bool cmdDown = isCommandDown(win);
            uint64_t anchor = sampleFromCursorX(baseSystem,
                                                daw,
                                                laneLeft,
                                                laneRight,
                                                secondsPerScreen,
                                                ui.cursorX,
                                                !cmdDown);
            daw.takeCompDragActive = true;
            daw.takeCompLaneType = takeHitType;
            daw.takeCompTrack = takeHitTrack;
            daw.takeCompTakeIndex = takeHitIndex;
            daw.takeCompStartSample = anchor;
            daw.takeCompEndSample = anchor;
            ui.consumeClick = true;
        }

        if (daw.takeCompDragActive) {
            if (!ui.uiLeftDown) {
                if (daw.takeCompLaneType == 0) {
                    applyAudioCompFromTake(daw.takeCompTrack,
                                           daw.takeCompTakeIndex,
                                           daw.takeCompStartSample,
                                           daw.takeCompEndSample);
                } else if (daw.takeCompLaneType == 1) {
                    applyMidiCompFromTake(daw.takeCompTrack,
                                          daw.takeCompTakeIndex,
                                          daw.takeCompStartSample,
                                          daw.takeCompEndSample);
                }
                daw.takeCompDragActive = false;
                daw.takeCompLaneType = -1;
                daw.takeCompTrack = -1;
                daw.takeCompTakeIndex = -1;
                daw.takeCompStartSample = 0;
                daw.takeCompEndSample = 0;
                ui.consumeClick = true;
            } else {
                bool cmdDown = isCommandDown(win);
                daw.takeCompEndSample = sampleFromCursorX(baseSystem,
                                                          daw,
                                                          laneLeft,
                                                          laneRight,
                                                          secondsPerScreen,
                                                          ui.cursorX,
                                                          !cmdDown);
                ui.consumeClick = true;
            }
        }

        if (laneCount > 0 && allowLaneInput && ui.uiLeftPressed && !ui.consumeClick
            && !playheadPressed && !rulerPressed && !verticalRulerPressed
            && !daw.bpmDragActive && !bpmDragPressed) {
            int handleLaneIndex = -1;
            for (int laneIdx = 0; laneIdx < laneCount; ++laneIdx) {
                float centerY = startY + static_cast<float>(laneIdx) * rowSpan;
                if (cursorInTrackHandleRect(ui, laneLeft, laneRight, centerY, laneHalfH)) {
                    handleLaneIndex = laneIdx;
                    break;
                }
            }
            if (handleLaneIndex >= 0) {
                daw.selectedLaneIndex = handleLaneIndex;
                if (!daw.laneOrder.empty() && handleLaneIndex < static_cast<int>(daw.laneOrder.size())) {
                    const auto& entry = daw.laneOrder[static_cast<size_t>(handleLaneIndex)];
                    daw.selectedLaneType = entry.type;
                    daw.selectedLaneTrack = entry.trackIndex;
                    daw.dragLaneType = entry.type;
                    daw.dragLaneTrack = entry.trackIndex;
                } else {
                    daw.selectedLaneType = 0;
                    daw.selectedLaneTrack = handleLaneIndex;
                    daw.dragLaneType = 0;
                    daw.dragLaneTrack = handleLaneIndex;
                }
                daw.dragLaneIndex = handleLaneIndex;
                daw.dragStartY = static_cast<float>(ui.cursorY);
                daw.dragPending = true;
                daw.dragActive = false;
                ui.consumeClick = true;
            }
        }

        if (!daw.clipDragActive && !daw.clipTrimActive
            && allowLaneInput && ui.uiLeftPressed && !ui.consumeClick
            && !playheadPressed && !rulerPressed && !verticalRulerPressed
            && !daw.bpmDragActive && !bpmDragPressed
            && trimHover.valid) {
            daw.clipTrimActive = true;
            daw.clipTrimLeftEdge = trimHover.leftEdge;
            daw.clipTrimTrack = trimHover.track;
            daw.clipTrimIndex = trimHover.clipIndex;
            daw.clipTrimOriginalStart = trimHover.clipStart;
            daw.clipTrimOriginalLength = trimHover.clipLength;
            daw.clipTrimOriginalSourceOffset = trimHover.clipSourceOffset;
            daw.clipTrimTargetStart = trimHover.clipStart;
            daw.clipTrimTargetLength = trimHover.clipLength;
            daw.clipTrimTargetSourceOffset = trimHover.clipSourceOffset;
            daw.selectedClipTrack = trimHover.track;
            daw.selectedClipIndex = trimHover.clipIndex;
            if (baseSystem.midi) {
                baseSystem.midi->selectedClipTrack = -1;
                baseSystem.midi->selectedClipIndex = -1;
            }
            ui.consumeClick = true;
            trimCursorWanted = true;
        }

        if (!daw.clipDragActive && !daw.clipTrimActive
            && allowLaneInput && ui.uiLeftPressed && !ui.consumeClick
            && !playheadPressed && !rulerPressed && !verticalRulerPressed
            && !daw.bpmDragActive && !bpmDragPressed) {
            double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
            double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples <= 0.0) windowSamples = 1.0;
            int hitTrack = -1;
            int hitClipIndex = -1;
            for (int t = 0; t < audioTrackCount; ++t) {
                const auto& clips = daw.tracks[static_cast<size_t>(t)].clips;
                if (clips.empty()) continue;
                int laneIndex = audioLaneIndex[static_cast<size_t>(t)];
                if (laneIndex < 0) continue;
                int displayIndex = laneIndex;
                if (daw.dragActive && daw.dragLaneType == 0 && daw.dragLaneIndex >= 0) {
                    int previewSlot = daw.dragDropIndex;
                    if (previewSlot >= 0 && laneIndex >= previewSlot) {
                        displayIndex += 1;
                    }
                } else if (daw.externalDropActive && daw.externalDropType == 0) {
                    int previewSlot = daw.externalDropIndex;
                    if (previewSlot >= 0 && laneIndex >= previewSlot) {
                        displayIndex += 1;
                    }
                }
                float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                float top = 0.0f;
                float bottom = 0.0f;
                float lipBottom = 0.0f;
                computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);
                if (ui.cursorY < top || ui.cursorY > bottom) continue;
                for (size_t ci = 0; ci < clips.size(); ++ci) {
                    const auto& clip = clips[ci];
                    if (clip.length == 0) continue;
                    double clipStart = static_cast<double>(clip.startSample);
                    double clipEnd = static_cast<double>(clip.startSample + clip.length);
                    if (clipEnd <= offsetSamples || clipStart >= offsetSamples + windowSamples) continue;
                    double visibleStart = std::max(clipStart, offsetSamples);
                    double visibleEnd = std::min(clipEnd, offsetSamples + windowSamples);
                    float t0 = static_cast<float>((visibleStart - offsetSamples) / windowSamples);
                    float t1 = static_cast<float>((visibleEnd - offsetSamples) / windowSamples);
                    float x0 = laneLeft + (laneRight - laneLeft) * t0;
                    float x1 = laneLeft + (laneRight - laneLeft) * t1;
                    expandAndClampClipX(laneLeft, laneRight, x0, x1);
                    if (x1 <= x0) continue;
                    if (ui.cursorX >= x0 && ui.cursorX <= x1
                        && ui.cursorY >= top && ui.cursorY <= lipBottom) {
                        hitTrack = t;
                        hitClipIndex = static_cast<int>(ci);
                        break;
                    }
                }
                if (hitTrack >= 0) break;
            }
            if (hitTrack >= 0 && hitClipIndex >= 0) {
                double cursorT = (laneRight > laneLeft)
                    ? (static_cast<double>(ui.cursorX) - laneLeft) / (laneRight - laneLeft)
                    : 0.0;
                cursorT = std::clamp(cursorT, 0.0, 1.0);
                double cursorSample = offsetSamples + cursorT * windowSamples;
                const auto& clip = daw.tracks[static_cast<size_t>(hitTrack)].clips[static_cast<size_t>(hitClipIndex)];
                daw.clipDragActive = true;
                daw.clipDragTrack = hitTrack;
                daw.clipDragIndex = hitClipIndex;
                daw.clipDragOffsetSamples = static_cast<int64_t>(std::llround(cursorSample)) - static_cast<int64_t>(clip.startSample);
                daw.clipDragTargetTrack = hitTrack;
                daw.clipDragTargetStart = clip.startSample;
                daw.selectedClipTrack = hitTrack;
                daw.selectedClipIndex = hitClipIndex;
                daw.selectedAutomationClipTrack = -1;
                daw.selectedAutomationClipIndex = -1;
                int hitLane = (hitTrack >= 0 && hitTrack < static_cast<int>(audioLaneIndex.size()))
                    ? audioLaneIndex[static_cast<size_t>(hitTrack)]
                    : hitTrack;
                if (hitLane < 0) hitLane = 0;
                daw.timelineSelectionStartSample = clip.startSample;
                daw.timelineSelectionEndSample = clip.startSample + clip.length;
                daw.timelineSelectionStartLane = hitLane;
                daw.timelineSelectionEndLane = hitLane;
                daw.timelineSelectionAnchorSample = clip.startSample;
                daw.timelineSelectionAnchorLane = hitLane;
                daw.timelineSelectionActive = (clip.length > 0);
                daw.timelineSelectionDragActive = false;
                daw.timelineSelectionFromPlayhead = false;
                if (baseSystem.midi) {
                    baseSystem.midi->selectedClipTrack = -1;
                    baseSystem.midi->selectedClipIndex = -1;
                }
                daw.selectedLaneIndex = -1;
                daw.selectedLaneType = -1;
                daw.selectedLaneTrack = -1;
                ui.consumeClick = true;
            }
        }

        if (daw.clipTrimActive) {
            trimCursorWanted = true;
            int trimTrack = daw.clipTrimTrack;
            int trimIndex = daw.clipTrimIndex;
            bool validTrimClip = trimTrack >= 0
                && trimTrack < audioTrackCount
                && trimIndex >= 0
                && trimIndex < static_cast<int>(daw.tracks[static_cast<size_t>(trimTrack)].clips.size());
            if (!validTrimClip) {
                daw.clipTrimActive = false;
                daw.clipTrimTrack = -1;
                daw.clipTrimIndex = -1;
            } else if (!ui.uiLeftDown) {
                DawTrack& track = daw.tracks[static_cast<size_t>(trimTrack)];
                DawClip& clip = track.clips[static_cast<size_t>(trimIndex)];
                clip.startSample = daw.clipTrimTargetStart;
                clip.length = daw.clipTrimTargetLength;
                clip.sourceOffset = daw.clipTrimTargetSourceOffset;
                DawClipSystemLogic::RebuildTrackCacheFromClips(daw, track);
                daw.clipTrimActive = false;
                daw.clipTrimTrack = -1;
                daw.clipTrimIndex = -1;
            } else {
                DawTrack& track = daw.tracks[static_cast<size_t>(trimTrack)];
                const DawClip& clip = track.clips[static_cast<size_t>(trimIndex)];
                uint64_t origStart = daw.clipTrimOriginalStart;
                uint64_t origLength = daw.clipTrimOriginalLength;
                uint64_t origEnd = origStart + origLength;
                uint64_t origSourceOffset = daw.clipTrimOriginalSourceOffset;
                uint64_t sourceSize = 0;
                if (clip.audioId >= 0 && clip.audioId < static_cast<int>(daw.clipAudio.size())) {
                    sourceSize = static_cast<uint64_t>(daw.clipAudio[clip.audioId].left.size());
                }

                uint64_t prevEnd = 0;
                uint64_t nextStart = UINT64_MAX;
                for (size_t i = 0; i < track.clips.size(); ++i) {
                    if (static_cast<int>(i) == trimIndex) continue;
                    const DawClip& other = track.clips[i];
                    if (other.length == 0) continue;
                    uint64_t otherStart = other.startSample;
                    uint64_t otherEnd = other.startSample + other.length;
                    if (otherEnd <= origStart) {
                        prevEnd = std::max(prevEnd, otherEnd);
                    } else if (otherStart >= origEnd) {
                        nextStart = std::min(nextStart, otherStart);
                    }
                }

                bool cmdDown = isCommandDown(win);
                uint64_t cursorSample = sampleFromCursorX(baseSystem,
                                                          daw,
                                                          laneLeft,
                                                          laneRight,
                                                          secondsPerScreen,
                                                          ui.cursorX,
                                                          !cmdDown);
                if (daw.clipTrimLeftEdge) {
                    uint64_t minStart = prevEnd;
                    uint64_t sourceMinStart = (origStart > origSourceOffset) ? (origStart - origSourceOffset) : 0;
                    minStart = std::max(minStart, sourceMinStart);
                    uint64_t maxStart = (origLength > kMinClipSamples)
                        ? (origStart + origLength - kMinClipSamples)
                        : origStart;
                    if (maxStart < minStart) maxStart = minStart;
                    uint64_t newStart = std::clamp(cursorSample, minStart, maxStart);
                    uint64_t newLength = origEnd - newStart;
                    uint64_t newSourceOffset = origSourceOffset + (newStart - origStart);
                    if (sourceSize > 0) {
                        if (newSourceOffset >= sourceSize) {
                            newSourceOffset = sourceSize - 1;
                            newStart = origStart + (newSourceOffset - origSourceOffset);
                            newLength = origEnd - newStart;
                        }
                        uint64_t maxBySource = sourceSize - newSourceOffset;
                        if (maxBySource < kMinClipSamples) maxBySource = kMinClipSamples;
                        if (newLength > maxBySource) {
                            newLength = maxBySource;
                            newStart = origEnd - newLength;
                            newSourceOffset = origSourceOffset + (newStart - origStart);
                        }
                    }
                    daw.clipTrimTargetStart = newStart;
                    daw.clipTrimTargetLength = std::max<uint64_t>(kMinClipSamples, newLength);
                    daw.clipTrimTargetSourceOffset = newSourceOffset;
                } else {
                    uint64_t minEnd = origStart + kMinClipSamples;
                    uint64_t maxEnd = (nextStart == UINT64_MAX) ? UINT64_MAX : nextStart;
                    if (sourceSize > 0 && origSourceOffset < sourceSize) {
                        uint64_t maxBySource = sourceSize - origSourceOffset;
                        maxEnd = std::min<uint64_t>(maxEnd, origStart + std::max<uint64_t>(kMinClipSamples, maxBySource));
                    }
                    if (maxEnd < minEnd) maxEnd = minEnd;
                    uint64_t newEnd = std::clamp(cursorSample, minEnd, maxEnd);
                    daw.clipTrimTargetStart = origStart;
                    daw.clipTrimTargetLength = std::max<uint64_t>(kMinClipSamples, newEnd - origStart);
                    daw.clipTrimTargetSourceOffset = origSourceOffset;
                }
                ui.consumeClick = true;
            }
        }

        if (allowLaneInput && ui.uiLeftPressed && !playheadPressed && !rulerPressed && !verticalRulerPressed
            && !daw.bpmDragActive && !bpmDragPressed) {
            if (cursorInRect(ui, rulerLeft, rulerRight, upperRulerTop, upperRulerBottom)) {
                double offsetSec = (daw.sampleRate > 0.0)
                    ? static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate)
                    : 0.0;
                double loopStartSec = (daw.sampleRate > 0.0)
                    ? static_cast<double>(daw.loopStartSamples) / static_cast<double>(daw.sampleRate)
                    : 0.0;
                double loopEndSec = (daw.sampleRate > 0.0)
                    ? static_cast<double>(daw.loopEndSamples) / static_cast<double>(daw.sampleRate)
                    : 0.0;
                float loopStartX = static_cast<float>(laneLeft + (laneRight - laneLeft) * ((loopStartSec - offsetSec) / secondsPerScreen));
                float loopEndX = static_cast<float>(laneLeft + (laneRight - laneLeft) * ((loopEndSec - offsetSec) / secondsPerScreen));
                if (loopEndX < loopStartX) std::swap(loopStartX, loopEndX);
                float loopLeft = std::clamp(loopStartX, rulerLeft, rulerRight);
                float loopRight = std::clamp(loopEndX, rulerLeft, rulerRight);
                float leftHandle = loopLeft - kLoopHandleWidth;
                float rightHandle = loopLeft + kLoopHandleWidth;
                float leftHandle2 = loopRight - kLoopHandleWidth;
                float rightHandle2 = loopRight + kLoopHandleWidth;
                if (cursorInRect(ui, leftHandle, rightHandle, upperRulerTop, upperRulerBottom)) {
                    daw.loopDragActive = true;
                    daw.loopDragMode = 1;
                    ui.consumeClick = true;
                } else if (cursorInRect(ui, leftHandle2, rightHandle2, upperRulerTop, upperRulerBottom)) {
                    daw.loopDragActive = true;
                    daw.loopDragMode = 2;
                    ui.consumeClick = true;
                } else if (cursorInRect(ui, loopLeft, loopRight, upperRulerTop, upperRulerBottom)) {
                    double sampleRate = (daw.sampleRate > 0.0) ? daw.sampleRate : 44100.0;
                    double cursorT = (laneRight > laneLeft)
                        ? (static_cast<double>(ui.cursorX) - laneLeft) / (laneRight - laneLeft)
                        : 0.0;
                    cursorT = std::clamp(cursorT, 0.0, 1.0);
                    double timeSec = offsetSec + cursorT * secondsPerScreen;
                    int64_t cursorSample = static_cast<int64_t>(std::llround(timeSec * sampleRate));
                    daw.loopDragActive = true;
                    daw.loopDragMode = 3;
                    daw.loopDragOffsetSamples = cursorSample - static_cast<int64_t>(daw.loopStartSamples);
                    daw.loopDragLengthSamples = (daw.loopEndSamples > daw.loopStartSamples)
                        ? (daw.loopEndSamples - daw.loopStartSamples)
                        : 0;
                    ui.consumeClick = true;
                }
            }
        }

        if (laneCount > 0
            && allowLaneInput
            && ui.uiLeftPressed
            && !ui.consumeClick
            && !playheadPressed
            && !rulerPressed
            && !daw.bpmDragActive
            && !bpmDragPressed
            && !daw.clipDragActive
            && !daw.clipTrimActive
            && cursorInLaneRect(ui,
                                laneLeft,
                                laneRight,
                                topBound - layout.laneGap,
                                laneBottomBound + layout.laneGap)) {
            int laneIdx = laneIndexFromCursorY(static_cast<float>(ui.cursorY), startY, laneHalfH, rowSpan, laneCount);
            if (laneIdx < 0) {
                laneIdx = laneIndexFromCursorYClamped(static_cast<float>(ui.cursorY), startY, rowSpan, laneCount);
            }
            if (laneIdx >= 0) {
                float centerY = startY + static_cast<float>(laneIdx) * rowSpan;
                float clipTop = 0.0f;
                float clipBottom = 0.0f;
                float lipBottom = 0.0f;
                computeClipRect(centerY, laneHalfH, clipTop, clipBottom, lipBottom);
                bool inTimelineBody = (static_cast<float>(ui.cursorY) > (lipBottom + 0.5f)
                    && static_cast<float>(ui.cursorY) <= clipBottom);
                if (inTimelineBody) {
                    bool cmdDown = isCommandDown(win);
                    uint64_t snappedSample = sampleFromCursorX(baseSystem, daw, laneLeft, laneRight, secondsPerScreen, ui.cursorX, true);
                    uint64_t rawSample = sampleFromCursorX(baseSystem, daw, laneLeft, laneRight, secondsPerScreen, ui.cursorX, false);
                    if (!cmdDown) {
                        daw.playheadSample.store(snappedSample, std::memory_order_relaxed);
                    }
                    daw.timelineSelectionDragActive = true;
                    daw.timelineSelectionFromPlayhead = cmdDown;
                    daw.timelineSelectionAnchorSample = cmdDown
                        ? daw.playheadSample.load(std::memory_order_relaxed)
                        : snappedSample;
                    daw.timelineSelectionAnchorLane = laneIdx;
                    daw.timelineSelectionStartSample = daw.timelineSelectionAnchorSample;
                    daw.timelineSelectionEndSample = cmdDown ? rawSample : daw.timelineSelectionAnchorSample;
                    daw.timelineSelectionStartLane = laneIdx;
                    daw.timelineSelectionEndLane = laneIdx;
                    daw.timelineSelectionActive = cmdDown && (daw.timelineSelectionStartSample != daw.timelineSelectionEndSample);
                    daw.selectedLaneIndex = -1;
                    daw.selectedLaneType = -1;
                    daw.selectedLaneTrack = -1;
                    daw.selectedClipTrack = -1;
                    daw.selectedClipIndex = -1;
                    daw.selectedAutomationClipTrack = -1;
                    daw.selectedAutomationClipIndex = -1;
                    if (baseSystem.midi) {
                        baseSystem.midi->selectedClipTrack = -1;
                        baseSystem.midi->selectedClipIndex = -1;
                    }
                    ui.consumeClick = true;
                }
            }
        }

        if (daw.timelineSelectionDragActive) {
            if (!ui.uiLeftDown) {
                daw.timelineSelectionDragActive = false;
                daw.timelineSelectionFromPlayhead = false;
                bool hasRange = (daw.timelineSelectionStartSample != daw.timelineSelectionEndSample)
                    || (daw.timelineSelectionStartLane != daw.timelineSelectionEndLane);
                daw.timelineSelectionActive = hasRange;
            } else {
                int laneIdx = laneIndexFromCursorY(static_cast<float>(ui.cursorY), startY, laneHalfH, rowSpan, laneCount);
                if (laneIdx < 0) {
                    laneIdx = laneIndexFromCursorYClamped(static_cast<float>(ui.cursorY), startY, rowSpan, laneCount);
                }
                if (laneIdx >= 0) {
                    bool cmdDown = isCommandDown(win);
                    uint64_t cursorSample = sampleFromCursorX(baseSystem,
                                                              daw,
                                                              laneLeft,
                                                              laneRight,
                                                              secondsPerScreen,
                                                              ui.cursorX,
                                                              !cmdDown);
                    daw.timelineSelectionStartSample = daw.timelineSelectionAnchorSample;
                    daw.timelineSelectionEndSample = cursorSample;
                    daw.timelineSelectionStartLane = daw.timelineSelectionAnchorLane;
                    daw.timelineSelectionEndLane = laneIdx;
                    bool hasRange = (daw.timelineSelectionStartSample != daw.timelineSelectionEndSample)
                        || (daw.timelineSelectionStartLane != daw.timelineSelectionEndLane);
                    daw.timelineSelectionActive = hasRange;
                    ui.consumeClick = true;
                }
            }
        }

        if (daw.clipDragActive) {
            if (!ui.uiLeftDown) {
                int srcTrack = daw.clipDragTrack;
                int srcIndex = daw.clipDragIndex;
                int dstTrack = daw.clipDragTargetTrack;
                if (srcTrack >= 0 && srcTrack < audioTrackCount
                    && dstTrack >= 0 && dstTrack < audioTrackCount) {
                    DawTrack& fromTrack = daw.tracks[static_cast<size_t>(srcTrack)];
                    if (srcIndex >= 0 && srcIndex < static_cast<int>(fromTrack.clips.size())) {
                        DawClip clip = fromTrack.clips[static_cast<size_t>(srcIndex)];
                        fromTrack.clips.erase(fromTrack.clips.begin() + srcIndex);
                        clip.startSample = daw.clipDragTargetStart;
                        DawTrack& toTrack = daw.tracks[static_cast<size_t>(dstTrack)];
                        DawClipSystemLogic::TrimClipsForNewClip(toTrack, clip);
                        toTrack.clips.push_back(clip);
                        std::sort(toTrack.clips.begin(), toTrack.clips.end(), [](const DawClip& a, const DawClip& b) {
                            if (a.startSample == b.startSample) return a.sourceOffset < b.sourceOffset;
                            return a.startSample < b.startSample;
                        });
                        int selectedIndex = -1;
                        for (size_t i = 0; i < toTrack.clips.size(); ++i) {
                            const DawClip& candidate = toTrack.clips[i];
                            if (candidate.audioId == clip.audioId
                                && candidate.startSample == clip.startSample
                                && candidate.length == clip.length
                                && candidate.sourceOffset == clip.sourceOffset
                                && candidate.takeId == clip.takeId) {
                                selectedIndex = static_cast<int>(i);
                                break;
                            }
                        }
                        daw.selectedClipTrack = dstTrack;
                        daw.selectedClipIndex = selectedIndex;
                        daw.selectedAutomationClipTrack = -1;
                        daw.selectedAutomationClipIndex = -1;
                        const DawClip* selectedClip = nullptr;
                        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(toTrack.clips.size())) {
                            selectedClip = &toTrack.clips[static_cast<size_t>(selectedIndex)];
                        } else {
                            selectedClip = &clip;
                        }
                        int selectedLane = (dstTrack >= 0 && dstTrack < static_cast<int>(audioLaneIndex.size()))
                            ? audioLaneIndex[static_cast<size_t>(dstTrack)]
                            : dstTrack;
                        if (selectedLane < 0) selectedLane = 0;
                        daw.timelineSelectionStartSample = selectedClip->startSample;
                        daw.timelineSelectionEndSample = selectedClip->startSample + selectedClip->length;
                        daw.timelineSelectionStartLane = selectedLane;
                        daw.timelineSelectionEndLane = selectedLane;
                        daw.timelineSelectionAnchorSample = selectedClip->startSample;
                        daw.timelineSelectionAnchorLane = selectedLane;
                        daw.timelineSelectionActive = (selectedClip->length > 0);
                        daw.timelineSelectionDragActive = false;
                        daw.timelineSelectionFromPlayhead = false;
                        if (baseSystem.midi) {
                            baseSystem.midi->selectedClipTrack = -1;
                            baseSystem.midi->selectedClipIndex = -1;
                        }
                        DawClipSystemLogic::RebuildTrackCacheFromClips(daw, toTrack);
                        if (srcTrack != dstTrack) {
                            DawClipSystemLogic::RebuildTrackCacheFromClips(daw, fromTrack);
                        }
                    }
                }
                daw.clipDragActive = false;
                daw.clipDragTrack = -1;
                daw.clipDragIndex = -1;
                daw.clipDragTargetTrack = -1;
                daw.clipDragTargetStart = 0;
                daw.clipDragOffsetSamples = 0;
            } else {
                double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                if (windowSamples <= 0.0) windowSamples = 1.0;
                double cursorT = (laneRight > laneLeft)
                    ? (static_cast<double>(ui.cursorX) - laneLeft) / (laneRight - laneLeft)
                    : 0.0;
                cursorT = std::clamp(cursorT, 0.0, 1.0);
                int64_t targetSample = static_cast<int64_t>(std::llround(offsetSamples + cursorT * windowSamples))
                    - daw.clipDragOffsetSamples;
                if (targetSample < 0) {
                    uint64_t shiftSamples = computeRebaseShiftSamples(daw, targetSample);
                    DawTimelineRebaseLogic::ShiftTimelineRight(baseSystem, shiftSamples);
                    targetSample += static_cast<int64_t>(shiftSamples);
                }
                bool cmdDown = glfwGetKey(win, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS
                    || glfwGetKey(win, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
                if (!cmdDown) {
                    double bpm = daw.bpm.load(std::memory_order_relaxed);
                    if (bpm <= 0.0) bpm = 120.0;
                    double secondsPerBeat = 60.0 / bpm;
                    double gridSeconds = DawLaneTimelineSystemLogic::GridSecondsForZoom(secondsPerScreen, secondsPerBeat);
                    if (gridSeconds > 0.0) {
                        uint64_t gridStepSamples = std::max<uint64_t>(1,
                            static_cast<uint64_t>(std::llround(gridSeconds * daw.sampleRate)));
                        targetSample = static_cast<int64_t>((static_cast<uint64_t>(targetSample) / gridStepSamples) * gridStepSamples);
                    }
                }
                daw.clipDragTargetStart = static_cast<uint64_t>(targetSample);

                int dstTrack = daw.clipDragTrack;
                if (!daw.laneOrder.empty()) {
                    int laneIdx = laneIndexFromCursorY(static_cast<float>(ui.cursorY), startY, laneHalfH, rowSpan, laneCount);
                    if (laneIdx >= 0 && laneIdx < static_cast<int>(daw.laneOrder.size())) {
                        const auto& entry = daw.laneOrder[static_cast<size_t>(laneIdx)];
                        if (entry.type == 0) {
                            dstTrack = entry.trackIndex;
                        }
                    }
                } else {
                    int laneIdx = laneIndexFromCursorY(static_cast<float>(ui.cursorY), startY, laneHalfH, rowSpan, laneCount);
                    if (laneIdx >= 0) dstTrack = laneIdx;
                }
                daw.clipDragTargetTrack = dstTrack;
                ui.consumeClick = true;
            }
        }

        if (daw.loopDragActive) {
            if (!ui.uiLeftDown) {
                daw.loopDragActive = false;
                daw.loopDragMode = 0;
            } else {
                double bpm = daw.bpm.load(std::memory_order_relaxed);
                if (bpm <= 0.0) bpm = 120.0;
                double secondsPerBeat = (bpm > 0.0) ? (60.0 / bpm) : 0.5;
                double gridSeconds = DawLaneTimelineSystemLogic::GridSecondsForZoom(secondsPerScreen, secondsPerBeat);
                double cursorT = (laneRight > laneLeft)
                    ? (static_cast<double>(ui.cursorX) - laneLeft) / (laneRight - laneLeft)
                    : 0.0;
                cursorT = std::clamp(cursorT, 0.0, 1.0);
                double offsetSec = (daw.sampleRate > 0.0)
                    ? static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate)
                    : 0.0;
                double sampleRate = (daw.sampleRate > 0.0) ? daw.sampleRate : 44100.0;
                double timeSec = offsetSec + cursorT * secondsPerScreen;
                long long rawSample = std::llround(timeSec * sampleRate);
                if (rawSample < 0) {
                    uint64_t shiftSamples = computeRebaseShiftSamples(daw, static_cast<int64_t>(rawSample));
                    DawTimelineRebaseLogic::ShiftTimelineRight(baseSystem, shiftSamples);
                    rawSample += static_cast<long long>(shiftSamples);
                }
                uint64_t targetSample = static_cast<uint64_t>(rawSample);
                bool cmdDown = glfwGetKey(win, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS
                    || glfwGetKey(win, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
                uint64_t gridStepSamples = 1;
                if (!cmdDown && gridSeconds > 0.0) {
                    gridStepSamples = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(gridSeconds * sampleRate)));
                    if (gridStepSamples > 0) {
                        targetSample = static_cast<uint64_t>(std::llround(static_cast<double>(targetSample) / gridStepSamples)) * gridStepSamples;
                    }
                }
                uint64_t loopStart = daw.loopStartSamples;
                uint64_t loopEnd = daw.loopEndSamples;
                if (loopEnd <= loopStart) {
                    loopEnd = loopStart + gridStepSamples;
                }
                uint64_t minLen = std::max<uint64_t>(1, gridStepSamples);
                if (daw.loopDragMode == 1) {
                    if (targetSample + minLen > loopEnd) {
                        targetSample = loopEnd - minLen;
                    }
                    daw.loopStartSamples = targetSample;
                } else if (daw.loopDragMode == 2) {
                    if (targetSample < loopStart + minLen) {
                        targetSample = loopStart + minLen;
                    }
                    daw.loopEndSamples = targetSample;
                } else if (daw.loopDragMode == 3) {
                    int64_t proposedStart = static_cast<int64_t>(targetSample) - daw.loopDragOffsetSamples;
                    if (proposedStart < 0) {
                        uint64_t shiftSamples = computeRebaseShiftSamples(daw, proposedStart);
                        DawTimelineRebaseLogic::ShiftTimelineRight(baseSystem, shiftSamples);
                        proposedStart += static_cast<int64_t>(shiftSamples);
                    }
                    uint64_t length = daw.loopDragLengthSamples;
                    if (length < minLen) length = minLen;
                    uint64_t newStart = static_cast<uint64_t>(proposedStart);
                    if (!cmdDown && gridStepSamples > 0) {
                        newStart = (newStart / gridStepSamples) * gridStepSamples;
                    }
                    daw.loopStartSamples = newStart;
                    daw.loopEndSamples = newStart + length;
                }
                uint64_t maxSamples = DawLaneTimelineSystemLogic::MaxTimelineSamples(daw);
                uint64_t windowSamples = static_cast<uint64_t>(std::max(0.0, secondsPerScreen * sampleRate));
                uint64_t maxAllowed = maxSamples + windowSamples;
                if (daw.loopStartSamples > maxAllowed) daw.loopStartSamples = maxAllowed;
                if (daw.loopEndSamples > maxAllowed) daw.loopEndSamples = maxAllowed;
            }
        }

        if (daw.rulerDragActive) {
            if (!ui.uiLeftDown) {
                daw.rulerDragActive = false;
            } else {
                double dx = ui.cursorX - daw.rulerDragLastX;
                daw.rulerDragLastX = ui.cursorX;
                double dy = ui.cursorY - daw.rulerDragLastY;
                daw.rulerDragLastY = ui.cursorY;

                if (dy < -0.001) {
                    daw.rulerDragEdgeDirection = -1;
                } else if (dy > 0.001) {
                    daw.rulerDragEdgeDirection = 1;
                }

                // Allow "drag into nothing" without cursor recentering by adding
                // virtual motion while pinned at window edges.
                const double edgeMarginPx = 1.0;
                const double edgePixelsPerSecond = 420.0;
                if (ui.cursorY <= edgeMarginPx && daw.rulerDragEdgeDirection < 0) {
                    dy -= edgePixelsPerSecond * static_cast<double>(dt);
                } else if (ui.cursorY >= layout.screenHeight - edgeMarginPx && daw.rulerDragEdgeDirection > 0) {
                    dy += edgePixelsPerSecond * static_cast<double>(dt);
                }

                daw.rulerDragAccumDY += dy;
                double scale = std::exp(-daw.rulerDragAccumDY * 0.01);
                double newSeconds = daw.rulerDragStartSeconds * scale;
                newSeconds = std::clamp(newSeconds, 2.0, 120.0);

                // Horizontal ruler drag pans timeline while zooming.
                if (laneRight > laneLeft) {
                    double secondsPerPixel = newSeconds / static_cast<double>(laneRight - laneLeft);
                    daw.rulerDragAnchorTimeSec -= dx * secondsPerPixel;
                }
                double newOffsetSec = daw.rulerDragAnchorTimeSec - daw.rulerDragAnchorT * newSeconds;
                daw.timelineSecondsPerScreen = newSeconds;
                daw.timelineOffsetSamples = static_cast<int64_t>(std::llround(newOffsetSec * daw.sampleRate));
                DawLaneTimelineSystemLogic::ClampTimelineOffset(daw);
            }
        }

        if (daw.verticalRulerDragActive) {
            if (!ui.uiLeftDown) {
                daw.verticalRulerDragActive = false;
            } else {
                double dx = ui.cursorX - daw.verticalRulerDragLastX;
                daw.verticalRulerDragLastX = ui.cursorX;
                double dy = ui.cursorY - daw.verticalRulerDragLastY;
                daw.verticalRulerDragLastY = ui.cursorY;

                if (dx < -0.001) {
                    daw.verticalRulerDragXEdgeDirection = -1;
                } else if (dx > 0.001) {
                    daw.verticalRulerDragXEdgeDirection = 1;
                }
                if (dy < -0.001) {
                    daw.verticalRulerDragEdgeDirection = -1;
                } else if (dy > 0.001) {
                    daw.verticalRulerDragEdgeDirection = 1;
                }

                // Keep vertical panning responsive when cursor is pinned against
                // the window top/bottom while dragging.
                const double edgeMarginPx = 1.0;
                const double edgePixelsPerSecond = 420.0;
                if (ui.cursorX <= edgeMarginPx && daw.verticalRulerDragXEdgeDirection < 0) {
                    dx -= edgePixelsPerSecond * static_cast<double>(dt);
                } else if (ui.cursorX >= layout.screenWidth - edgeMarginPx
                           && daw.verticalRulerDragXEdgeDirection > 0) {
                    dx += edgePixelsPerSecond * static_cast<double>(dt);
                }
                if (ui.cursorY <= edgeMarginPx && daw.verticalRulerDragEdgeDirection < 0) {
                    dy -= edgePixelsPerSecond * static_cast<double>(dt);
                } else if (ui.cursorY >= layout.screenHeight - edgeMarginPx
                           && daw.verticalRulerDragEdgeDirection > 0) {
                    dy += edgePixelsPerSecond * static_cast<double>(dt);
                }

                daw.verticalRulerDragAccumDX += dx;
                float oldLaneHeight = std::clamp(daw.timelineLaneHeight, 24.0f, 180.0f);
                float oldRowSpan = oldLaneHeight + layout.laneGap;
                float newLaneHeight = std::clamp(
                    static_cast<float>(daw.verticalRulerDragStartLaneHeight
                        * std::exp(-daw.verticalRulerDragAccumDX * 0.01)),
                    24.0f,
                    180.0f);
                float newRowSpan = newLaneHeight + layout.laneGap;
                float currentStartY = 100.0f + layout.scrollY + daw.timelineLaneOffset;
                float cursorRel = static_cast<float>(ui.cursorY) - currentStartY;
                float anchorDelta = 0.0f;
                if (oldRowSpan > 0.001f) {
                    anchorDelta = cursorRel * (1.0f - (newRowSpan / oldRowSpan));
                }
                daw.timelineLaneHeight = newLaneHeight;
                daw.timelineLaneOffset += anchorDelta + static_cast<float>(dy);
            }
        }

        if (allowLaneInput && ui.mainScrollDelta != 0.0
            && cursorInRect(ui, rulerLeft, rulerRight, rulerTopY, rulerBottomY)) {
            double zoomFactor = (ui.mainScrollDelta > 0.0) ? 1.1 : (1.0 / 1.1);
            double newSeconds = secondsPerScreen * zoomFactor;
            newSeconds = std::clamp(newSeconds, 2.0, 120.0);
            double cursorT = (laneRight > laneLeft)
                ? (static_cast<double>(ui.cursorX) - laneLeft) / (laneRight - laneLeft)
                : 0.0;
            cursorT = std::clamp(cursorT, 0.0, 1.0);
            double anchorTime = (static_cast<double>(daw.timelineOffsetSamples) / daw.sampleRate)
                + cursorT * secondsPerScreen;
            double newOffsetSec = anchorTime - cursorT * newSeconds;
            daw.timelineSecondsPerScreen = newSeconds;
            daw.timelineOffsetSamples = static_cast<int64_t>(std::llround(newOffsetSec * daw.sampleRate));
            DawLaneTimelineSystemLogic::ClampTimelineOffset(daw);
            ui.mainScrollDelta = 0.0;
        }

        if (allowLaneInput && ui.mainScrollDelta != 0.0
            && cursorInRect(ui, verticalRulerLeft, verticalRulerRight, verticalRulerTop, verticalRulerBottom)) {
            float oldLaneHeight = std::clamp(daw.timelineLaneHeight, 24.0f, 180.0f);
            float oldRowSpan = oldLaneHeight + layout.laneGap;
            float zoomFactor = (ui.mainScrollDelta > 0.0) ? 1.08f : (1.0f / 1.08f);
            float newLaneHeight = std::clamp(oldLaneHeight * zoomFactor, 24.0f, 180.0f);
            float newRowSpan = newLaneHeight + layout.laneGap;
            float currentStartY = 100.0f + layout.scrollY + daw.timelineLaneOffset;
            float cursorRel = static_cast<float>(ui.cursorY) - currentStartY;
            float anchorDelta = 0.0f;
            if (oldRowSpan > 0.001f) {
                anchorDelta = cursorRel * (1.0f - (newRowSpan / oldRowSpan));
            }
            daw.timelineLaneHeight = newLaneHeight;
            daw.timelineLaneOffset += anchorDelta;
            ui.mainScrollDelta = 0.0;
        }

        if (daw.playheadDragActive) {
            if (!ui.uiLeftDown) {
                daw.playheadDragActive = false;
            } else {
                float targetX = static_cast<float>(ui.cursorX) + daw.playheadDragOffsetX;
                float t = (laneRight > laneLeft) ? (targetX - laneLeft) / (laneRight - laneLeft) : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                if (windowSamples < 0.0) windowSamples = 0.0;
                double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                int64_t newSampleSigned = static_cast<int64_t>(std::llround(offsetSamples + t * windowSamples));
                if (newSampleSigned < 0) {
                    uint64_t shiftSamples = computeRebaseShiftSamples(daw, newSampleSigned);
                    DawTimelineRebaseLogic::ShiftTimelineRight(baseSystem, shiftSamples);
                    newSampleSigned += static_cast<int64_t>(shiftSamples);
                }
                uint64_t newSample = static_cast<uint64_t>(newSampleSigned);
                daw.playheadSample.store(newSample, std::memory_order_relaxed);
                ui.consumeClick = true;
            }
        }

        if (laneCount > 0 && allowLaneInput && daw.dragPending && ui.uiLeftDown) {
            float dy = std::abs(static_cast<float>(ui.cursorY) - daw.dragStartY);
            if (!daw.dragActive && dy > 4.0f) {
                daw.dragActive = true;
            }
        }

        if (laneCount > 0 && allowLaneInput && daw.dragActive) {
            float laneHeight = laneHalfH * 2.0f;
            float handleSize = std::min(kTrackHandleSize, std::max(14.0f, laneHeight));
            float dragRight = laneRight + kTrackHandleInset + handleSize + 4.0f;
            if (cursorInLaneRect(ui, laneLeft, dragRight, topBound - layout.laneGap, laneBottomBound + layout.laneGap)) {
                daw.dragDropIndex = dropSlotFromCursorY(static_cast<float>(ui.cursorY), startY, rowSpan, laneCount);
            } else {
                daw.dragDropIndex = -1;
            }
        }

        if (!ui.uiLeftDown && (daw.dragPending || daw.dragActive)) {
            if (daw.dragActive && daw.dragDropIndex >= 0 && daw.dragLaneIndex >= 0) {
                int fromIndex = daw.dragLaneIndex;
                // Keep commit semantics aligned with preview semantics:
                // previewSlot is computed on the lane list with the dragged lane
                // removed, so commit should target that reduced-list index directly.
                int toIndex = std::clamp(daw.dragDropIndex, 0, std::max(0, laneCount - 1));
                if (toIndex != fromIndex) {
                    if (DawTrackSystemLogic::MoveTrack(baseSystem, fromIndex, toIndex)) {
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
        if (!ui.uiLeftDown && !daw.dragPending && !daw.dragActive
            && daw.selectedLaneIndex < 0 && daw.selectedLaneType < 0 && daw.selectedLaneTrack < 0) {
            daw.dragDropIndex = -1;
        }

        g_rightMouseWasDown = rightMouseDownNow;
        ApplyLaneResizeCursor(win, trimCursorWanted || daw.clipTrimActive);
    }
}
