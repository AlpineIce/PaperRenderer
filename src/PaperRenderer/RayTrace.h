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
            const ShaderPair& raygenShaderPair,
            const std::vector<std::vector<ShaderPair>>& shaderGroups,
            std::vector<VkPushConstantRange> pcRanges
        );
        ~RayTraceRender();

        void render(const RayTraceRenderInfo& rtRenderInfo, const SynchronizationInfo& syncInfo);
    };
}