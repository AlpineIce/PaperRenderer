#pragma once
#include "Pipeline.h"
#include "Descriptor.h"

namespace PaperRenderer
{
    struct RayTraceRenderInfo
    {
        const Image& image;
        const class Camera& camera;
        VkDependencyInfo const* preRenderBarriers = NULL;
        VkDependencyInfo const* postRenderBarriers = NULL;
        DescriptorWrites rtDescriptorWrites = {};
    };

    class RayTraceRender
    {
    private:
        std::unique_ptr<RTPipeline> pipeline;
        RTPipelineProperties pipelineProperties = {};

        void buildPipeline(const std::unordered_map<uint32_t, PaperRenderer::DescriptorSet>& descriptorSets);

        class RenderEngine* rendererPtr;
        class AccelerationStructure* accelerationStructurePtr;

    public:
        RayTraceRender(RenderEngine* renderer, AccelerationStructure* accelerationStructure, const std::unordered_map<uint32_t, PaperRenderer::DescriptorSet>& descriptorSets);
        ~RayTraceRender();

        void render(const RayTraceRenderInfo& rtRenderInfo, const SynchronizationInfo& syncInfo);
    };
}