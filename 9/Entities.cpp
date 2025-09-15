#pragma once

// Factory function to create all the entity prototypes
std::vector<Entity> createAllEntityPrototypes(BaseSystem& baseSystem) {
    std::vector<Entity> entityPrototypes;

    // Use the numBlockPrototypes value loaded from JSON
    for (int i = 0; i < baseSystem.numBlockPrototypes; ++i) {
        Entity p;
        p.prototypeID = entityPrototypes.size();
        p.isRenderable = true;
        p.blockType = i;
        p.name = "BlockType_" + std::to_string(i);
        entityPrototypes.push_back(p);
    }
    
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

    Entity worldGenAudicle;
    worldGenAudicle.prototypeID = entityPrototypes.size();
    worldGenAudicle.name = "DebugWorldGenerator";
    worldGenAudicle.isAudicle = true;
    entityPrototypes.push_back(worldGenAudicle);

    return entityPrototypes;
}


