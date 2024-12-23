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

    struct AccelerationStructureInstanceData
    {
        class ModelInstance* instancePtr = NULL;
        uint32_t customIndex:24 = 0;
        uint32_t mask:8 = 0xAA;
        VkGeometryInstanceFlagsKHR flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    };

    class RayTraceRender
    {
    private:
        const std::vector<VkPushConstantRange> pcRanges;
        const std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>> descriptorSets;
        const RTPipelineProperties pipelineProperties;
        std::unique_ptr<RTPipeline> pipeline;
        
        bool queuePipelineBuild = true;

        std::unordered_map<RTMaterial const*, uint32_t> materialReferences; //uint32_t is the number of instances using it
        std::vector<AccelerationStructureInstanceData> asInstances;
        const ShaderDescription raygenShader;
        const std::vector<ShaderDescription> missShaders;
        const std::vector<ShaderDescription> callableShaders;

        void rebuildPipeline();
        
        class RenderEngine& renderer;
        TLAS& tlas;

    public:
        RayTraceRender(
            RenderEngine& renderer,
            TLAS& accelerationStructure,
            const ShaderDescription raygenShader,
            const std::vector<ShaderDescription> missShaders,
            const std::vector<ShaderDescription> callableShaders,
            const std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>>& descriptors,
            const RTPipelineProperties& pipelineProperties,
            const std::vector<VkPushConstantRange>& pcRanges
        );
        ~RayTraceRender();
        RayTraceRender(const RayTraceRender&) = delete;

        void render(RayTraceRenderInfo rtRenderInfo, SynchronizationInfo syncInfo);
        void updateTLAS(VkBuildAccelerationStructureModeKHR mode, VkBuildAccelerationStructureFlagsKHR flags, SynchronizationInfo syncInfo); //MUST CALL BEFORE RENDERING TO REFIT TLAS TO THIS RENDER PASS

        void addInstance(AccelerationStructureInstanceData instanceData, const class RTMaterial& material);
        void removeInstance(class ModelInstance& instance);

        const RTPipeline& getPipeline() const { return *pipeline; }
        const std::vector<AccelerationStructureInstanceData>& getTLASInstanceData() const { return asInstances; }
        const TLAS& getTLAS() const { return tlas; } //this class doesn't own TLAS, but it can be useful to retrieve which TLAS it is referencing
    };
}