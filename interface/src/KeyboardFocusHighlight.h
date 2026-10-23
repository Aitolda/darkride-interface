//
//  KeyboardFocusHighlight.h
//  interface/src
//
//  The keyboard-focus highlight: a local entity drawn around the entity that has keyboard focus. Application.cpp
//  defines these and makes and moves the highlight (setKeyboardFocusHighlight, setKeyboardFocusEntity);
//  ApplicationFrame.cpp follows the focused entity each frame; ApplicationShutdown.cpp deletes it.
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#pragma once
#ifndef hifi_KeyboardFocusHighlight_h
#define hifi_KeyboardFocusHighlight_h

#include <QtCore/QUuid>

extern QUuid _keyboardFocusHighlightID;             // the highlight entity, UNKNOWN_ENTITY_ID when there is none
extern const float FOCUS_HIGHLIGHT_EXPANSION_FACTOR; // the highlight's size relative to the entity's

#endif // hifi_KeyboardFocusHighlight_h
