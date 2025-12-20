#define GLM_ENABLE_EXPERIMENTAL
#include "Host.h"

// --- Include System Implementations for Single Translation Unit Build ---
#include "BaseEntity.cpp"
#include "BaseSystem/SpawnSystem.cpp"
#include "BaseSystem/CollisionSystem.cpp"
#include "BaseSystem/SkyboxSystem.cpp" // <-- ADD THIS LINE
#include "BaseSystem/KeyboardInputSystem.cpp"
#include "BaseSystem/MouseInputSystem.cpp"
#include "BaseSystem/UAVSystem.cpp"
#include "BaseSystem/AudioSystem.cpp"
#include "BaseSystem/RayTracedAudioSystem.cpp"
#include "BaseSystem/AudioRayVisualizerSystem.cpp"
#include "BaseSystem/PinkNoiseSystem.cpp"
#include "BaseSystem/AudicleSystem.cpp"
#include "BaseSystem/CameraSystem.cpp"
#include "BaseSystem/RenderSystem.cpp"
#include "BaseSystem/CloudSystem.cpp"
#include "BaseSystem/AuroraSystem.cpp"
#include "BaseSystem/VolumeFillSystem.cpp"
#include "BaseSystem/GlyphSystem.cpp"
#include "BaseSystem/BlockSelectionSystem.cpp"
#include "BaseSystem/BlockChargeSystem.cpp"
#include "BaseSystem/BuildSystem.cpp"
#include "BaseSystem/StructurePlacementSystem.cpp"
#include "BaseSystem/StructureCaptureSystem.cpp"
#include "BaseSystem/HUDSystem.cpp"
#include "BaseSystem/TerrainGenerationSystem.cpp"
#include "BaseSystem/UIScreenSystem.cpp"
#include "BaseSystem/ComputerCursorSystem.cpp"
#include "BaseSystem/ButtonSystem.cpp"
#include "BaseSystem/ChucKSystem.cpp"
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
