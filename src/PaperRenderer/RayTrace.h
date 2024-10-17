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

        class RenderEngine* rendererPtr;
        class TLAS* accelerationStructurePtr;

    public:
        RayTraceRender(
            RenderEngine* renderer,
            TLAS* accelerationStructure,
            const std::unordered_map<uint32_t, PaperRenderer::DescriptorSet>& descriptorSets,
            const std::vector<class RTMaterial*>& materials,
            const std::unordered_map<VkShaderStageFlagBits, const std::unique_ptr<Shader>&>& generalShaders,
            std::vector<VkPushConstantRange> pcRanges
        );
        ~RayTraceRender();
        RayTraceRender(const RayTraceRender&) = delete;

        void render(const RayTraceRenderInfo& rtRenderInfo, const SynchronizationInfo& syncInfo);
    };
}