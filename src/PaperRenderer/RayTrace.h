#pragma once
#include "Pipeline.h"
#include "Descriptor.h"

namespace PaperRenderer
{
    struct RayTraceRenderInfo
    {
        VkBuildAccelerationStructureModeKHR tlasBuildMode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        VkBuildAccelerationStructureFlagsKHR tlasBuildFlags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
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
        std::unordered_map<uint32_t, PaperRenderer::DescriptorSet> descriptorSets;
        const std::vector<VkPushConstantRange> pcRanges;
        bool queuePipelineBuild = true;


        std::unordered_map<RTMaterial const*, uint32_t> materialReferences; //uint32_t is the number of instances using it
        std::list<ShaderDescription> generalShaders;

        void rebuildPipeline();
        
        class RenderEngine& renderer;
        TLAS& tlas;

    public:
        RayTraceRender(
            RenderEngine& renderer,
            TLAS& accelerationStructure,
            const std::unordered_map<uint32_t, PaperRenderer::DescriptorSet>& descriptorSets,
            const std::vector<VkPushConstantRange>& pcRanges
        );
        ~RayTraceRender();
        RayTraceRender(const RayTraceRender&) = delete;

        void render(RayTraceRenderInfo rtRenderInfo, SynchronizationInfo syncInfo);

        void addInstance(class ModelInstance* instance, class RTMaterial const* material);
        void removeInstance(class ModelInstance* instance);

        void addGeneralShaders(const std::vector<ShaderDescription>& shaders);
        void removeGeneralShader(const ShaderDescription& shader);

        const RTPipeline& getPipeline() const { return *pipeline; }
    };
}