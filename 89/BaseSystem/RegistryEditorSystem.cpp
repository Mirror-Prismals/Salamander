#pragma once

#include "Host/PlatformInput.h"
#include <algorithm>
#include <cctype>

namespace RegistryEditorSystemLogic {
    namespace {
        void clearPendingAction(UIContext& ui) {
            ui.pendingActionType.clear();
            ui.pendingActionKey.clear();
            ui.pendingActionValue.clear();
        }

        std::string toLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool tryParseBoolString(const std::string& raw, bool& outValue) {
            const std::string value = toLowerCopy(raw);
            if (value == "1" || value == "true" || value == "yes" || value == "on") {
                outValue = true;
                return true;
            }
            if (value == "0" || value == "false" || value == "no" || value == "off") {
                outValue = false;
                return true;
            }
            return false;
        }

        std::variant<bool, std::string> parseRegistryValue(const std::map<std::string, std::variant<bool, std::string>>& registry,
                                                            const std::string& key,
                                                            const std::string& rawValue) {
            auto existing = registry.find(key);
            if (existing != registry.end() && std::holds_alternative<bool>(existing->second)) {
                bool parsed = false;
                if (tryParseBoolString(rawValue, parsed)) return parsed;
                return std::get<bool>(existing->second);
            }
            if (existing != registry.end() && std::holds_alternative<std::string>(existing->second)) {
                return rawValue;
            }
            bool parsed = false;
            if (tryParseBoolString(rawValue, parsed)) return parsed;
            return rawValue;
        }

        void requestShaderReload(std::map<std::string, std::variant<bool, std::string>>& registry, const std::string& key) {
            const std::string targetKey = key.empty() ? "ReloadShaders" : key;
            registry[targetKey] = true;
        }
    }

    void UpdateRegistry(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes; (void)dt; (void)win;
        if (!baseSystem.ui || !baseSystem.registry || !baseSystem.reloadRequested || !baseSystem.reloadTarget) return;
        UIContext& ui = *baseSystem.ui;
        auto& registry = *baseSystem.registry;

        std::string currentLevel;
        auto regIt = registry.find("level");
        if (regIt != registry.end() && std::holds_alternative<std::string>(regIt->second)) {
            currentLevel = std::get<std::string>(regIt->second);
        }
        bool isMenuLevel = (currentLevel == "menu");
        if (isMenuLevel) ui.active = true;

        if (ui.actionDelayFrames > 0) {
            ui.actionDelayFrames -= 1;
            if (ui.actionDelayFrames == 0 && !ui.pendingActionType.empty()) {
                if (ui.pendingActionType == "SetRegistry" && !ui.pendingActionKey.empty()) {
                    registry[ui.pendingActionKey] = parseRegistryValue(registry, ui.pendingActionKey, ui.pendingActionValue);
                    if (ui.pendingActionKey == "level") {
                        ui.levelSwitchPending = true;
                        ui.levelSwitchTarget = ui.pendingActionValue;
                    }
                    clearPendingAction(ui);
                } else if (ui.pendingActionType == "SetRegistry") {
                    clearPendingAction(ui);
                } else if (ui.pendingActionType == "ShaderReload" || ui.pendingActionType == "ReloadShaders") {
                    requestShaderReload(registry, ui.pendingActionKey);
                    clearPendingAction(ui);
                }
            }
        }
    }
}
