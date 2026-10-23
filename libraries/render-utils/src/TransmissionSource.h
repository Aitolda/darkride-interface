//
//  TransmissionSource.h
//  libraries/render-utils/src
//
//  The scene behind the translucent surfaces, for the transmissive ones (glTF's KHR_materials_transmission: glass,
//  water, a fridge's door). Once the opaque surfaces, the background and the haze are drawn, and before the translucent
//  surfaces are, the frame's colour is copied with its mips; a transmissive surface reads the copy at its own pixel, at
//  the mip its roughness picks, for the light that comes through it (model.slf), so frosted or smudged glass blurs what
//  is behind it. A translucent surface behind a transmissive one is not in the copy, as in Unity's and Filament's.
//
//  The copy is made only while a transmissive surface is drawn: binding its material asks for one
//  (RenderPipelines::bindMaterials), and the next frame of each view makes it. In the frame a transmissive surface
//  first appears in, and in a view with no copy job, it is drawn blended instead.
//
//  Created by Ben Brennan on 2026-10-23.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_TransmissionSource_h
#define hifi_TransmissionSource_h

#include <atomic>

#include <gpu/Framebuffer.h>
#include <render/Engine.h>

class CopyTransmissionSource {
public:
    using Inputs = gpu::FramebufferPointer;
    using JobModel = render::Job::ModelI<CopyTransmissionSource, Inputs>;

    void run(const render::RenderContextPointer& renderContext, const Inputs& source);

    // the copy the view's translucent surfaces read, or null when it made none this frame
    static const gpu::TexturePointer& getFrameSource() { return _frameSource; }
    // a transmissive surface is being drawn: the views copy their scenes from the next frame
    static void request() { _requests++; }

private:
    gpu::FramebufferPointer _copy;
    uint32_t _requestsSeen { 0 };

    static gpu::TexturePointer _frameSource;
    static std::atomic<uint32_t> _requests;

    friend class EndTransmissionSource;
};

// After the view's last translucent layer: no other view or pass reads its copy, which is the wrong size for them
class EndTransmissionSource {
public:
    using JobModel = render::Job::Model<EndTransmissionSource>;

    void run(const render::RenderContextPointer& renderContext) { CopyTransmissionSource::_frameSource.reset(); }
};

#endif // hifi_TransmissionSource_h
