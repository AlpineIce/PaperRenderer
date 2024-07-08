#pragma once
#include "RHI/Device.h"
#include "RHI/Descriptor.h"

namespace PaperRenderer
{
    struct RayTraceRenderInfo
    {
        PaperMemory::Image& image;
        VkDependencyInfo const* preRenderBarriers = NULL;
        VkDependencyInfo const* postRenderBarriers = NULL;
    };

    class RayTraceRender
    {
    private:
        std::unique_ptr<class RTPipeline> pipeline;
        DescriptorWrites rtDescriptorWrites = {};

        void buildPipeline();

        class RenderEngine* rendererPtr;
        class AccelerationStructure* accelerationStructurePtr;
        class Camera* cameraPtr;

    public:
        RayTraceRender(RenderEngine* renderer, AccelerationStructure* accelerationStructure, Camera* camera, const struct RTPipelineProperties& pipelineProperties, const RayTraceRenderInfo& rtRenderInfo);
        ~RayTraceRender();

        void render(const PaperMemory::SynchronizationInfo& syncInfo);
    };
}