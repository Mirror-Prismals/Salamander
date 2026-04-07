#pragma once

#include "Host/PlatformInput.h"

namespace OreMiningSystemLogic { bool IsMiningActive(const BaseSystem& baseSystem); }
namespace GroundCraftingSystemLogic { bool IsRitualActive(const BaseSystem& baseSystem); }
namespace GemChiselSystemLogic { bool IsChiselActive(const BaseSystem& baseSystem); }

namespace MouseInputSystemLogic {
    void UpdateCameraRotationFromMouse(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.player) return;
        PlayerContext& player = *baseSystem.player;
        bool uiActive = (baseSystem.ui && baseSystem.ui->active)
            || OreMiningSystemLogic::IsMiningActive(baseSystem)
            || GroundCraftingSystemLogic::IsRitualActive(baseSystem)
            || GemChiselSystemLogic::IsChiselActive(baseSystem);
        if (!uiActive) {
            const float sensitivity = 0.1f;
            player.cameraYaw += player.mouseOffsetX * sensitivity;
            player.cameraPitch += player.mouseOffsetY * sensitivity;
            if (player.cameraPitch > 89.0f) player.cameraPitch = 89.0f;
            if (player.cameraPitch < -89.0f) player.cameraPitch = -89.0f;
        }

        if (win) {
            bool newRightDown = PlatformInput::IsMouseButtonDown(win, PlatformInput::MouseButton::Right);
            bool newLeftDown = PlatformInput::IsMouseButtonDown(win, PlatformInput::MouseButton::Left);
            bool newMiddleDown = PlatformInput::IsMouseButtonDown(win, PlatformInput::MouseButton::Middle);

            player.rightMousePressed = (!player.rightMouseDown && newRightDown);
            player.leftMousePressed = (!player.leftMouseDown && newLeftDown);
            player.middleMousePressed = (!player.middleMouseDown && newMiddleDown);
            player.rightMouseReleased = (player.rightMouseDown && !newRightDown);
            player.leftMouseReleased = (player.leftMouseDown && !newLeftDown);
            player.middleMouseReleased = (player.middleMouseDown && !newMiddleDown);

            player.rightMouseDown = newRightDown;
            player.leftMouseDown = newLeftDown;
            player.middleMouseDown = newMiddleDown;
        } else {
            player.rightMousePressed = player.leftMousePressed = player.middleMousePressed = false;
            player.rightMouseReleased = player.leftMouseReleased = player.middleMouseReleased = false;
            player.rightMouseDown = player.leftMouseDown = player.middleMouseDown = false;
        }
    }
}
