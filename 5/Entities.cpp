#pragma once

// Factory function to create all the entity prototypes
std::vector<Entity> createAllEntityPrototypes() {
    std::vector<Entity> entityPrototypes;

    for (int i = 0; i < NUM_BLOCK_PROTOTYPES; ++i) {
        Entity p;
        p.prototypeID = i;
        p.isRenderable = true;
        p.blockType = i;
        entityPrototypes.push_back(p);
    }
    
    Entity worldProto;
    worldProto.prototypeID = entityPrototypes.size();
    worldProto.isWorld = true;
    entityPrototypes.push_back(worldProto);
    
    Entity starProto;
    starProto.prototypeID = entityPrototypes.size();
    starProto.isStar = true;
    entityPrototypes.push_back(starProto);

    return entityPrototypes;
}
