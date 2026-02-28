#pragma once

#include <cmath>

void Host::processMouseInput(double xpos, double ypos) {
    if (baseSystem.player) {
        PlayerContext& player = *baseSystem.player;
        if (player.firstMouse) { player.lastX = xpos; player.lastY = ypos; player.firstMouse = false; }
        player.mouseOffsetX = xpos - player.lastX;
        player.mouseOffsetY = player.lastY - ypos;
        player.lastX = xpos;
        player.lastY = ypos;
    }
}

void Host::processScroll(double xoffset, double yoffset) {
    if (baseSystem.player) {
        PlayerContext& player = *baseSystem.player;
        // Trackpads often emit horizontal wheel motion on xoffset while yoffset stays 0.
        // Mirror the piano-roll prototype behavior by falling back to xoffset.
        double delta = (std::fabs(yoffset) > 0.0) ? yoffset : xoffset;
        player.scrollYOffset += delta;
    }
}
