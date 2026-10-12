//
//  JoltSupport.cpp
//  libraries/physics/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "JoltSupport.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include "PhysicsLogging.h"

namespace {
    std::once_flag initOnce;
    std::atomic<bool> initialized { false };

    void joltTrace(const char* format, ...) {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        qCDebug(physics) << "Jolt:" << buffer;
    }

#ifdef JPH_ENABLE_ASSERTS
    // Log and carry on: a failed assert inside physics must never take the whole client down.
    bool joltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line) {
        qCWarning(physics) << "Jolt assert failed:" << expression << (message ? message : "") << "at" << file << ":" << line;
        return false;
    }
#endif
}

bool JoltSupport::initialize() {
    std::call_once(initOnce, []() {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = joltTrace;
#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = joltAssertFailed;
#endif
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        initialized = true;
        qCDebug(physics) << "Jolt Physics" << QString("%1.%2.%3").arg(JPH_VERSION_MAJOR).arg(JPH_VERSION_MINOR).arg(JPH_VERSION_PATCH) << "ready";
    });
    return initialized;
}

void JoltSupport::shutdown() {
    if (initialized.exchange(false)) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

bool JoltSupport::isInitialized() {
    return initialized;
}
