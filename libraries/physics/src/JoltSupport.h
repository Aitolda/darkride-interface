//
//  JoltSupport.h
//  libraries/physics/src
//
//  Jolt Physics needs a process-wide setup once (its allocator, its type factory, its trace and assert hooks)
//  before any shape or body exists. This does it once, and routes Jolt's messages into the physics log.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_JoltSupport_h
#define hifi_JoltSupport_h

namespace JoltSupport {
    // Idempotent and thread-safe. Returns true when Jolt is ready to use.
    bool initialize();
    // For the end of the process; a no-op unless initialize() ran.
    void shutdown();
    bool isInitialized();
}

#endif // hifi_JoltSupport_h
