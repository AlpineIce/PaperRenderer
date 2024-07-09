#pragma once
#include "RHI/Pipeline.h"
#include "RHI/Descriptor.h"

namespace PaperRenderer
{
    struct RayTraceRenderInfo
    {
        PaperMemory::Image& image;
        const class Camera& camera;
        VkDependencyInfo const* preRenderBarriers = NULL;
        VkDependencyInfo const* postRenderBarriers = NULL;
    };

    class RayTraceRender
    {
    private:
        std::unique_ptr<RTPipeline> pipeline;

    protected:
        DescriptorWrites rtDescriptorWrites = {};
        std::unordered_map<uint32_t, PaperRenderer::DescriptorSet> rtDescriptorSets;
        RTPipelineProperties pipelineProperties = {};

        void buildPipeline();

        class RenderEngine* rendererPtr;
        class AccelerationStructure* accelerationStructurePtr;

    public:
        RayTraceRender(RenderEngine* renderer, AccelerationStructure* accelerationStructure);
        ~RayTraceRender();

        void render(const RayTraceRenderInfo& rtRenderInfo, const PaperMemory::SynchronizationInfo& syncInfo);
    };
}