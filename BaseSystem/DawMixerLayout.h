#pragma once

namespace DawMixerLayout {
    constexpr float kPaddingX = 40.0f;
    constexpr float kPaddingY = 24.0f;
    constexpr float kColumnGap = 30.0f;
    constexpr float kMeterWidth = 5.0f;
    constexpr float kMeterGap = 0.0f;
    constexpr float kMeterToFaderGap = 10.0f;
    constexpr float kFaderTrackWidth = 20.0f;
    constexpr float kFaderHousingPadding = 10.0f;
    constexpr float kFaderHousingDepth = 6.0f;
    constexpr float kFaderTrackInset = 12.0f;
    constexpr float kKnobHalfSize = 12.5f;
    constexpr float kPanelTopInset = 12.0f;
    constexpr float kMixerScissorBleed = 8.0f;
    constexpr float kNavButtonSize = 28.0f;
    constexpr float kNavButtonGap = 10.0f;
    constexpr float kNavRowGap = 8.0f;

    inline float meterBlockWidth() {
        return kMeterWidth * 2.0f + kMeterGap;
    }

    inline float faderHousingWidth() {
        return kFaderTrackWidth + kFaderHousingPadding * 2.0f;
    }

    inline float columnWidth() {
        return meterBlockWidth() + kMeterToFaderGap + faderHousingWidth();
    }

    inline float columnSpacing() {
        return columnWidth() + kColumnGap;
    }

    inline float navBlockWidth() {
        return kNavButtonSize * 2.0f + kNavButtonGap + 12.0f;
    }

    inline float navBlockHeight() {
        return kNavButtonSize * 2.0f + kNavRowGap + 12.0f;
    }
}
