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
        ShaderHitGroup const* hitGroup = NULL;
        uint32_t customIndex:24 = 0;
        uint32_t mask:8 = 0xFF;
        VkGeometryInstanceFlagsKHR flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

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
            std::vector<AccelerationStructureInstanceData> instanceDatas = {};
            std::deque<AccelerationStructureInstanceData> toUpdateInstances = {};
        };
        std::unordered_map<TLAS*, TLASInstanceData> tlasData = {};

        //shaders
        std::vector<uint32_t> raygenShader;
        std::vector<std::vector<uint32_t>> missShaders;
        std::vector<std::vector<uint32_t>> callableShaders;
        std::unordered_map<ShaderHitGroup const*, uint32_t> materialReferences; //uint32_t is the number of instances using it

        void rebuildPipeline();
        void assignResourceOwner(const Queue& queue);
        
        class RenderEngine& renderer;

        friend TLAS;

    public:
        RayTraceRender(
            RenderEngine& renderer,
            const std::vector<uint32_t>& raygenShader,
            const std::vector<std::vector<uint32_t>>& missShaders,
            const std::vector<std::vector<uint32_t>>& callableShaders,
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
        void addInstance(const std::unordered_map<TLAS*, AccelerationStructureInstanceData>& asDatas);
        void removeInstance(const std::unordered_map<TLAS*, AccelerationStructureInstanceData>& asDatas);

        const RTPipeline& getPipeline() const { return *pipeline; }
        const std::unordered_map<TLAS*, TLASInstanceData>& getTLASData() const { return tlasData; }
    };
}