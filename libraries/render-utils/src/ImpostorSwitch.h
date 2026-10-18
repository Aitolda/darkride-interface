//
//  ImpostorSwitch.h
//  libraries/render-utils/src
//
//  The switch between a model and its impostor card (IMPOSTORS.md 2.4), the level-of-detail switch of an entity
//  with an "impostor" entry: one per such entity, shared by the card's render item and by the render items of the
//  models the card stands for (the entity's own model and its descendants'). Each item asks it, per view, what to
//  draw: the card where the entity's bounding sphere subtends less than the impostor angle (the LOD manager's
//  impostorAngleDeg, carried as RenderArgs::_impostorAngleHalfTanSq, the same measure LODManager::shouldRender culls
//  by) once the card's atlases are loaded, the model otherwise, and never the card with the camera inside the
//  sphere. The main view keeps a hysteresis band around the angle so a camera resting at the boundary does not
//  flicker, and crosses between the two by a short dissolve (the fade effect's noise, FADE_LOD_SWITCH, the model's
//  parts clipped where the noise is below the card's share and the card where it is above, so that where the two
//  surfaces coincide the clips are complementary); the shadow pass follows the main view; the other views (mirrors,
//  the secondary camera) take the plain threshold. Read and written on the render thread only.
//
//  Created by the editor session on 2026-10-18.
//

#ifndef hifi_ImpostorSwitch_h
#define hifi_ImpostorSwitch_h

#include <memory>

#include <glm/glm.hpp>

#include <gpu/Buffer.h>
#include <render/Args.h>

#include "FadeObjectParams.shared.slh"

class ImpostorSwitch {
public:
    // the dissolve's length, seconds
    static const float CROSSFADE_SECONDS;

    // the entity's bounding sphere in world space (the entry's centre taken through the entity's transform)
    void setSphere(const glm::vec3& centerWS, float radius) { _center = centerWS; _radius = radius; }
    const glm::vec3& getCenter() const { return _center; }
    float getRadius() const { return _radius; }

    // both atlases loaded: until then the model draws whatever the angle
    void setCardReady(bool ready) { _cardReady = ready; }
    bool isCardReady() const { return _cardReady; }

    // What this view draws of the entity: its card, its model, or during the main view's dissolve both, each clipped
    // to its share (share: the card's, 0 none of it, 1 all of it).
    struct View {
        bool card { false };
        bool model { true };
        float share { 0.0f };
    };
    View evaluate(const RenderArgs* args);
    // the main view's dissolve is under way: the model's parts take their fade pipeline for it
    bool isFading() const { return _share > 0.0f && _share < 1.0f; }
    // the dissolve's parameters for this view (FadeObjectParams: the LOD category, the threshold the card's share),
    // to bind at RENDER_UTILS_BUFFER_FADE_OBJECT_PARAMS
    const gpu::BufferView& fadeParams(const RenderArgs* args);

private:
    bool wantsCardAt(float distanceSq, float thresholdSq) const;

    glm::vec3 _center { 0.0f };
    float _radius { 0.0f };
    bool _cardReady { false };
    bool _mainViewShowsCard { false };   // the hysteresis state: what the main view's dissolve goes to
    float _share { 0.0f };               // the card's share of the main view's dissolve
    float _fadeFrom { 0.0f };
    quint64 _fadeStart { 0 };
    gpu::StructBuffer<FadeObjectParams> _mainParams;    // the main view's and the shadow pass's
    gpu::StructBuffer<FadeObjectParams> _plainParams;   // the other views': no dissolve
    float _mainParamsShare { -1.0f };
    bool _plainParamsSet { false };
};

using ImpostorSwitchPointer = std::shared_ptr<ImpostorSwitch>;

#endif // hifi_ImpostorSwitch_h
