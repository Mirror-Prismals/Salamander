#pragma once

// Factory function to create all the entity prototypes
std::vector<Entity> createAllEntityPrototypes() {
    std::vector<Entity> entityPrototypes;

    // --- Block Prototypes (0-24) ---
    for (int i = 0; i < NUM_BLOCK_PROTOTYPES; ++i) {
        Entity p;
        p.prototypeID = entityPrototypes.size();
        p.isRenderable = true;
        p.blockType = i;
        p.name = "BlockType_" + std::to_string(i);
        entityPrototypes.push_back(p);
    }
    
    // --- Special Container Prototypes ---
    Entity worldProto;
    worldProto.prototypeID = entityPrototypes.size();
    worldProto.name = "World";
    worldProto.isWorld = true;
    entityPrototypes.push_back(worldProto);
    
    Entity starProto;
    starProto.prototypeID = entityPrototypes.size();
    starProto.name = "Star";
    starProto.isStar = true;
    entityPrototypes.push_back(starProto);

    // --- Audicle Prototype ---
    // A dedicated Audicle for creating our debug world.
    // It's empty by default; the Application will fill its "script" at runtime.
    Entity worldGenAudicle;
    worldGenAudicle.prototypeID = entityPrototypes.size();
    worldGenAudicle.name = "DebugWorldGenerator";
    worldGenAudicle.isAudicle = true;
    entityPrototypes.push_back(worldGenAudicle);
    
    // Inside Entities.cpp, at the end of createAllEntityPrototypes()

    // ... (after the DebugWorldGenerator Audicle)
    // example of how to add new content
    // NEW: Audicle to spawn and animate a RED block
    /* Entity redBlockAudicle;
    redBlockAudicle.prototypeID = entityPrototypes.size();
    redBlockAudicle.name = "RedBlockMover";
    redBlockAudicle.isAudicle = true;
    entityPrototypes.push_back(redBlockAudicle); */


    return entityPrototypes;
}
