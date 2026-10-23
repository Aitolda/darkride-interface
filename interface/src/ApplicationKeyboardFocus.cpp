//
//  ApplicationKeyboardFocus.cpp
//  interface/src
//
//  Application's keyboard focus: which entity has it (getKeyboardFocusEntity, setKeyboardFocusEntity: the focused
//  entity recorded, its renderable given the window, the keyboard-mouse device's focus-out, the sign-in dialog's
//  special case) and the highlight drawn around it, a local line-box entity made on demand and moved to the entity
//  (setKeyboardFocusHighlight), with the highlight's id and expansion factor that KeyboardFocusHighlight.h declares
//  (the frame loop follows the focused entity with it; the shutdown deletes it). The members as they stood in
//  Application.cpp, in their order; a home of their own until an owner takes them (ARCHITECTURE.md §5.4 step 4).
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#include "Application.h"
#include "KeyboardFocusHighlight.h"
#include <glm/glm.hpp>
#include <EntityScriptingInterface.h>
#include <MainWindow.h>
#include <RenderableEntityItem.h>

using namespace std;

// the highlight's id and expansion factor, declared in KeyboardFocusHighlight.h; ApplicationFrame.cpp and
// ApplicationShutdown.cpp read them
const float FOCUS_HIGHLIGHT_EXPANSION_FACTOR = 1.05f;   // declared in KeyboardFocusHighlight.h
// FIXME move to header, or better yet, design some kind of UI manager
// to take care of highlighting keyboard focused items, rather than
// continuing to overburden Application.cpp
QUuid _keyboardFocusHighlightID;   // declared in KeyboardFocusHighlight.h

void Application::setKeyboardFocusHighlight(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& dimensions) {
    if (qApp->getLoginDialogPoppedUp()) {
        return;
    }

    auto entityScriptingInterface = DependencyManager::get<EntityScriptingInterface>();
    if (_keyboardFocusHighlightID == UNKNOWN_ENTITY_ID || !entityScriptingInterface->isAddedEntity(_keyboardFocusHighlightID)) {
        EntityItemProperties properties;
        properties.setType(EntityTypes::Box);
        properties.setAlpha(1.0f);
        properties.setColor({ 0xFF, 0xEF, 0x00 });
        properties.setPrimitiveMode(PrimitiveMode::LINES);
        properties.getPulse().setMin(0.5);
        properties.getPulse().setMax(1.0f);
        properties.getPulse().setColorMode(PulseMode::IN_PHASE);
        properties.setIgnorePickIntersection(true);
        _keyboardFocusHighlightID = entityScriptingInterface->addEntityInternal(properties, entity::HostType::LOCAL);
    }

    // Position focus
    EntityItemProperties properties;
    properties.setPosition(position);
    properties.setRotation(rotation);
    properties.setDimensions(dimensions);
    properties.setVisible(true);
    entityScriptingInterface->editEntity(_keyboardFocusHighlightID, properties);
}

QUuid Application::getKeyboardFocusEntity() const {
    return _keyboardFocusedEntity.get();
}

void Application::setKeyboardFocusEntity(const QUuid& id) {
    if (_keyboardFocusedEntity.get() != id) {
        if (qApp->getLoginDialogPoppedUp() && !_loginDialogID.isNull()) {
            if (id == _loginDialogID) {
                emit loginDialogFocusEnabled();
            } else if (!_keyboardFocusWaitingOnRenderable) {
                // that's the only entity we want in focus;
                return;
            }
        }

        _keyboardFocusedEntity.set(id);

        auto entityScriptingInterface = DependencyManager::get<EntityScriptingInterface>();
        if (id != UNKNOWN_ENTITY_ID) {
            EntityPropertyFlags desiredProperties;
            desiredProperties += PROP_VISIBLE;
            desiredProperties += PROP_LOCALLY_VISIBLE; // TIVOLI new
            desiredProperties += PROP_SHOW_KEYBOARD_FOCUS_HIGHLIGHT;
            auto properties = entityScriptingInterface->getEntityProperties(id);
            if (properties.getVisible()) {
                auto entities = getEntities();
                auto entityId = _keyboardFocusedEntity.get();
                auto entityItemRenderable = entities->renderableForEntityId(entityId);
                if (!entityItemRenderable) {
                    _keyboardFocusWaitingOnRenderable = true;
                } else if (entityItemRenderable->wantsKeyboardFocus()) {
                    entities->setProxyWindow(entityId, _window->windowHandle());
                    if (_keyboardMouseDevice->isActive()) {
                        _keyboardMouseDevice->pluginFocusOutEvent();
                    }
                    _lastAcceptedKeyPress = usecTimestampNow();

                    if (properties.getShowKeyboardFocusHighlight()) {
                        if (auto entity = entities->getEntity(entityId)) {
                            setKeyboardFocusHighlight(entity->getWorldPosition(), entity->getWorldOrientation(),
                                entity->getScaledDimensions() * FOCUS_HIGHLIGHT_EXPANSION_FACTOR);
                            return;
                        }
                    }
                }
            }
        }

        EntityItemProperties properties;
        properties.setVisible(false);
        entityScriptingInterface->editEntity(_keyboardFocusHighlightID, properties);
    }
}
