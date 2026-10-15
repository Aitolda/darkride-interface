//
//  RenderForwardTask.h
//  render-utils/src/
//
//  Created by Zach Pomerantz on 12/13/2016.
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_RenderForwardTask_h
#define hifi_RenderForwardTask_h

#include <gpu/Pipeline.h>
#include <render/RenderFetchCullSortTask.h>
#include "AssembleLightingStageTask.h"
#include "LightingModel.h"
#include "RenderShadowTask.h"

class RenderForwardTaskConfig : public render::Task::Config {
    Q_OBJECT
    Q_PROPERTY(float resolutionScale MEMBER resolutionScale NOTIFY dirty)
public:
    float resolutionScale{ 1.f };

signals:
    void dirty();
};

// The forward path (Forward+): everything shaded in one multisampled pass, each fragment over the sun, the ambient
// light and the point and spot lights of the cluster it falls in, with the frame's shadows and cookies, which were
// drawn before the deferred/forward choice (RenderViewTask) and come in as the shadow task's output
class RenderForwardTask {
public:
    using Input = render::VaryingSet4<RenderFetchCullSortTask::Output, LightingModelPointer, AssembleLightingStageTask::Output, RenderShadowTask::Output>;
    using Config = RenderForwardTaskConfig;
    using JobModel = render::Task::ModelI<RenderForwardTask, Input, Config>;

    RenderForwardTask() {}

    void configure(const Config& config);
    void build(JobModel& task, const render::Varying& input, render::Varying& output);
};


class PreparePrimaryFramebufferMSAAConfig : public render::Job::Config {
    Q_OBJECT
    Q_PROPERTY(float resolutionScale  WRITE setResolutionScale READ getResolutionScale NOTIFY dirty)
    Q_PROPERTY(int numSamples WRITE setNumSamples READ getNumSamples NOTIFY dirty)
public:
    float getResolutionScale() const { return resolutionScale; }
    void setResolutionScale(float scale);

    int getNumSamples() const { return numSamples; }
    void setNumSamples(int num);

signals:
    void dirty();

protected:
    float resolutionScale{ 1.0f };
    int numSamples{ 4 };
};

class PreparePrimaryFramebufferMSAA {
public:
    using Output = gpu::FramebufferPointer;
    using Config = PreparePrimaryFramebufferMSAAConfig;
    using JobModel = render::Job::ModelO<PreparePrimaryFramebufferMSAA, Output, Config>;

    void configure(const Config& config);
    void run(const render::RenderContextPointer& renderContext,
            gpu::FramebufferPointer& framebuffer);

private:
    gpu::FramebufferPointer _framebuffer;
    float _resolutionScale{ 1.0f };
    int _numSamples;

    static gpu::FramebufferPointer createFramebuffer(const char* name, const glm::uvec2& frameSize, int numSamples);
};

class PrepareForward {
public:
    using Inputs = render::VaryingSet2 <gpu::FramebufferPointer, LightStage::FramePointer>;
    using JobModel = render::Job::ModelI<PrepareForward, Inputs>;

    void run(const render::RenderContextPointer& renderContext,
        const Inputs& inputs);

private:
};

class DrawForward{
public:
    using Inputs = render::VaryingSet3<render::ItemBounds, LightingModelPointer, HazeStage::FramePointer>;
    using JobModel = render::Job::ModelI<DrawForward, Inputs>;

    DrawForward(const render::ShapePlumberPointer& shapePlumber, bool opaquePass) : _shapePlumber(shapePlumber), _opaquePass(opaquePass) {}
    void run(const render::RenderContextPointer& renderContext,
            const Inputs& inputs);

private:
    render::ShapePlumberPointer _shapePlumber;
    bool _opaquePass;
};

#endif // hifi_RenderForwardTask_h
