//
//  ImpostorCard.h
//  libraries/entities-renderer/src
//
//  An entity's impostor card (IMPOSTORS.md 2.3): the render item that stands in for the entity's models at a
//  distance, a square of side two radii facing the camera, drawn by impostor.slf from the two atlases the bake wrote
//  (ImpostorBaker, the "impostor" entry of the entity's userData). The base EntityRenderer owns one for every entity
//  with an entry and keeps it a second item in the scene beside the entity's own; the card's ImpostorSwitch decides,
//  per view, whether the card or the models draw, and the models the card stands for (the entity's own and its
//  descendants') hold the same switch (Model::setImpostorSwitch).
//
//  Created by the editor session on 2026-10-18.
//

#ifndef hifi_ImpostorCard_h
#define hifi_ImpostorCard_h

#include <memory>
#include <mutex>

#include <QtCore/QString>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <gpu/Batch.h>
#include <render/Item.h>
#include <render/Args.h>
#include <RenderHifi.h>
#include <TextureCache.h>
#include <ImpostorSwitch.h>

// The "impostor" entry of an entity's userData (IMPOSTORS.md 2.1), as the baker writes it.
struct ImpostorEntry {
    bool valid { false };
    QString spec;           // the entry's own JSON, compact: equal specs are the same entry
    QString albedoUrl;
    QString normalUrl;
    int size { 256 };       // a frame's side, texels
    float radius { 0.0f };  // the bounding sphere in the entity's frame at bake time
    glm::vec3 center { 0.0f };
    float roughness { 0.7f };
    float metallic { 0.0f };

    static ImpostorEntry parse(const QString& userData);
};

class ImpostorCard : public render::PayloadProxyInterface {
public:
    ImpostorCard();

    // main thread (the entity renderer's synchronous update): what the card is and where it stands
    void setEntry(const ImpostorEntry& entry);
    void setFrame(const glm::vec3& positionWS, const glm::quat& rotationWS);
    void setVisible(bool visible);
    void setKeyBits(render::hifi::Tag tagMask, render::hifi::Layer layer, bool subMetaCulled);
    // the atlases are still on their way (the renderer asks for another update until they are here or have failed)
    bool waitingForTextures() const;
    const ImpostorSwitchPointer& getSwitch() const { return _switch; }

    // render thread, from the item's update: takes what the main thread set, readies the switch and the parameters
    void update();

    // render::PayloadProxyInterface
    render::ItemKey getKey() override;
    render::ShapeKey getShapeKey() override;
    render::Item::Bound getBound() override;
    void render(RenderArgs* args) override;
    uint32_t metaFetchMetaSubItems(render::ItemIDs& subItems) const override { return 0; }

private:
    struct State {
        ImpostorEntry entry;
        glm::vec3 position { 0.0f };
        glm::quat rotation;
        bool visible { false };
        render::hifi::Tag tagMask { render::hifi::TAG_MAIN_VIEW };
        render::hifi::Layer layer { render::hifi::LAYER_3D };
        bool subMetaCulled { false };
    };

    mutable std::mutex _mutex;     // over _pending and the network textures
    State _pending;
    NetworkTexturePointer _albedo;
    NetworkTexturePointer _normal;

    // the render thread's
    State _live;
    gpu::TexturePointer _albedoTexture;
    gpu::TexturePointer _normalTexture;
    bool _ready { false };
    glm::vec3 _centerWS { 0.0f };
    render::Item::Bound _bound;
    gpu::BufferPointer _params;
    int _debugMode { 0 };
    ImpostorSwitchPointer _switch;
};

#endif // hifi_ImpostorCard_h
