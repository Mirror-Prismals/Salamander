#define GLM_ENABLE_EXPERIMENTAL
#include "Host.h"

// --- Include System Implementations for Single Translation Unit Build ---
#include "BaseEntity.cpp"
#include "BaseSystem/TesseractSystem.cpp"
#include "BaseSystem/AudioSystem.cpp"
#include "BaseSystem/RayTracedAudioSystem.cpp"
#include "BaseSystem/PinkNoiseSystem.cpp"
#include "BaseSystem/AudicleSystem.cpp"
#include "BaseSystem/CameraSystem.cpp"
#include "BaseSystem/PlayerControlSystem.cpp" // <-- This was likely the missing line
#include "BaseSystem/RenderSystem.cpp"
#include "Host/HostShader.cpp"
#include "Host/HostUtilities.cpp"
#include "Host/Startup.cpp"
#include "Host/HostInput.cpp"
#include "Host/HostLoader.cpp"
#include "Host/Host.cpp"

int main() {
    Host cardinal;
    cardinal.run();
    return 0;
}
