#pragma once
#include "Pipeline.h"
#include "AccelerationStructure.h"
#include "Descriptor.h"

namespace PaperRenderer
{
    struct RayTraceRenderInfo
    {
        VkBuildAccelerationStructureModeKHR tlasBuildMode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        VkBuildAccelerationStructureFlagsKHR tlasBuildFlags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        const Image& image;
        const class Camera& camera;
        const std::vector<SetBinding>& descriptorBindings = {};
        VkDependencyInfo const* preRenderBarriers = NULL;
        VkDependencyInfo const* postRenderBarriers = NULL;
    };

    struct AccelerationStructureInstanceData
    {
        class ModelInstance* instancePtr = NULL;
        uint32_t customIndex:24 = 0;
        uint32_t mask:8 = 0xAA;
        VkGeometryInstanceFlagsKHR flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        std::vector<TLAS*> owners = {}; //instance will only be used in the TLAS' specified
        
        bool operator<(const AccelerationStructureInstanceData& other) const
        {
            return instancePtr < other.instancePtr;
        }
        bool operator==( const AccelerationStructureInstanceData& other) const
        {
            return instancePtr == other.instancePtr;
        }
    };

    class RayTraceRender
    {
    private:
        const std::vector<VkPushConstantRange> pcRanges;
        const std::unordered_map<uint32_t, VkDescriptorSetLayout> setLayouts;
        const RTPipelineProperties pipelineProperties;
        std::unique_ptr<RTPipeline> pipeline;

        bool queuePipelineBuild = true;

        //TLAS instance data
        struct TLASInstanceData
        {
            std::vector<AccelerationStructureInstanceData> instances = {};
            std::deque<AccelerationStructureInstanceData> toUpdateInstances = {};
        };
        std::unordered_map<TLAS*, TLASInstanceData> tlasData = {};

        //instances
        std::vector<AccelerationStructureInstanceData> asInstances;

        //shaders
        const ShaderDescription raygenShader;
        const std::vector<ShaderDescription> missShaders;
        const std::vector<ShaderDescription> callableShaders;
        std::unordered_map<RTMaterial const*, uint32_t> materialReferences; //uint32_t is the number of instances using it

        void rebuildPipeline();
        void assignResourceOwner(const Queue& queue);
        
        class RenderEngine& renderer;

        friend TLAS;

    public:
        RayTraceRender(
            RenderEngine& renderer,
            const ShaderDescription& raygenShader,
            const std::vector<ShaderDescription>& missShaders,
            const std::vector<ShaderDescription>& callableShaders,
            const std::unordered_map<uint32_t, VkDescriptorSetLayout>& setLayouts,
            const RTPipelineProperties& pipelineProperties,
            const std::vector<VkPushConstantRange>& pcRanges
        );
        ~RayTraceRender();
        RayTraceRender(const RayTraceRender&) = delete;

        //Invokes vkCmdTraceRaysKHR at the listed entryTLAS. All acceleration structures used should be updated with updateTLAS() before rendering
        const Queue& render(const RayTraceRenderInfo& rtRenderInfo, const SynchronizationInfo& syncInfo);
        //Updates the transformation, addition/removal, and sbt offsets of all instances used
        const Queue& updateTLAS(TLAS& tlas, const VkBuildAccelerationStructureModeKHR mode, const VkBuildAccelerationStructureFlagsKHR flags, const SynchronizationInfo& syncInfo);

        //Please keep track of the return value; ownership is transfered to return value
        [[nodiscard]] std::unique_ptr<TLAS> addNewTLAS();
        void addInstance(AccelerationStructureInstanceData instanceData, const class RTMaterial& material);
        void removeInstance(class ModelInstance& instance);

        const RTPipeline& getPipeline() const { return *pipeline; }
        const std::vector<AccelerationStructureInstanceData>& getInstanceData() const { return asInstances; }
        const std::unordered_map<TLAS*, TLASInstanceData>& getTLASData() const { return tlasData; }
    };
}