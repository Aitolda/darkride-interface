//
//  TransmissionSource.cpp
//  libraries/render-utils/src
//
//  Created by Ben Brennan on 2026-10-23.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "TransmissionSource.h"

#include <gpu/Batch.h>
#include <gpu/Context.h>

gpu::TexturePointer CopyTransmissionSource::_frameSource;
std::atomic<uint32_t> CopyTransmissionSource::_requests { 0 };

void CopyTransmissionSource::run(const render::RenderContextPointer& renderContext, const Inputs& source) {
    // asked for since this view's last frame: by a transmissive surface of this view or another
    uint32_t requests = _requests;
    bool wanted = requests != _requestsSeen;
    _requestsSeen = requests;

    if (!wanted || !source || !source->getRenderBuffer(0)) {
        // nothing transmissive in sight: no copy, and no memory held for one
        _frameSource.reset();
        _copy.reset();
        return;
    }

    auto size = source->getSize();
    if (!_copy || _copy->getSize() != size) {
        // the frame's own format (the forward and deferred paths' R11G11B10 float colour), with every mip: a rough
        // surface reads a coarser one. Clamped, so the blur at the frame's edges does not wrap to the other side
        gpu::Sampler::Desc samplerDesc;
        samplerDesc._filter = gpu::Sampler::FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc._wrapModeU = gpu::Sampler::WRAP_CLAMP;
        samplerDesc._wrapModeV = gpu::Sampler::WRAP_CLAMP;
        auto texture = gpu::Texture::createRenderBuffer(source->getRenderBuffer(0)->getTexelFormat(),
            (gpu::uint16)size.x, (gpu::uint16)size.y, gpu::Texture::MAX_NUM_MIPS, gpu::Sampler(samplerDesc));
        _copy = gpu::FramebufferPointer(gpu::Framebuffer::create("transmissionSource"));
        _copy->setRenderBuffer(0, texture);
    }

    // the forward path's frame is multisampled: the copy resolves it
    gpu::Vec4i rect(0, 0, (int)size.x, (int)size.y);
    gpu::doInBatch("CopyTransmissionSource", renderContext->args->_context, [&](gpu::Batch& batch) {
        batch.blit(source, rect, _copy, rect);
        batch.generateTextureMips(_copy->getRenderBuffer(0));
    });
    _frameSource = _copy->getRenderBuffer(0);
}
