#pragma once

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
