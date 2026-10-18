//
//  ImpostorSwitch.h
//  libraries/render-utils/src
//
//  The switch between a model and its impostor card (IMPOSTORS.md 2.4), the level-of-detail switch of an entity
//  with an "impostor" entry: one per such entity, shared by the card's render item and by the render items of the
//  models the card stands for (the entity's own model and its descendants'). Each item asks it, per view, whether
//  the card shows: when the entity's bounding sphere subtends less than the impostor angle (the LOD manager's
//  impostorAngleDeg, carried as RenderArgs::_impostorAngleHalfTanSq, the same measure LODManager::shouldRender culls
//  by) and the card's atlases are loaded. The main view keeps a hysteresis band around the angle so a camera resting
//  at the boundary does not flicker; the other views (mirrors, the secondary camera) take the plain threshold. A
//  shadow pass always draws the model (the card casts no shadow yet). Read and written on the render thread only.
//
//  Created by the editor session on 2026-10-18.
//

#ifndef hifi_ImpostorSwitch_h
#define hifi_ImpostorSwitch_h

#include <memory>

#include <glm/glm.hpp>

#include <render/Args.h>

class ImpostorSwitch {
public:
    // the entity's bounding sphere in world space (the entry's centre taken through the entity's transform)
    void setSphere(const glm::vec3& centerWS, float radius) { _center = centerWS; _radius = radius; }
    const glm::vec3& getCenter() const { return _center; }
    float getRadius() const { return _radius; }

    // both atlases loaded: until then the model draws whatever the angle
    void setCardReady(bool ready) { _cardReady = ready; }
    bool isCardReady() const { return _cardReady; }

    // true when the card stands in for the model in this view
    bool showsCard(const RenderArgs* args);
    // true when the model's parts must not draw in this view
    bool hidesModel(const RenderArgs* args) { return showsCard(args); }

private:
    glm::vec3 _center { 0.0f };
    float _radius { 0.0f };
    bool _cardReady { false };
    bool _mainViewShowsCard { false };
};

using ImpostorSwitchPointer = std::shared_ptr<ImpostorSwitch>;

#endif // hifi_ImpostorSwitch_h
