//
//  ApplicationSettingNames.h
//  interface/src
//
//  The setting keys Application.cpp defines and ApplicationShutdown.cpp reads too: the active display plug-in's
//  name, kept at shutdown and read at the next start, and "keep me logged in".
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#pragma once
#ifndef hifi_ApplicationSettingNames_h
#define hifi_ApplicationSettingNames_h

#include <QtCore/QString>

extern const QString ACTIVE_DISPLAY_PLUGIN_SETTING_NAME;
extern const QString KEEP_ME_LOGGED_IN_SETTING_NAME;

#endif // hifi_ApplicationSettingNames_h
