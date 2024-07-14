#pragma once
#include "Memory/VulkanResources.h"
#include "glm/gtc/quaternion.hpp"

#include <unordered_map>
#include <list>

namespace PaperRenderer
{
    class CommonMeshGroup
    {
    private:
        struct ShaderMesh
        {
            uint32_t drawCountsOffset;
            uint32_t drawCommandsOffset;
            uint32_t outputObjectsOffset;
            uint32_t padding;
        };

        struct ShaderOutputObject
        {
            glm::mat4 modelMatrix;
            glm::mat4 transformMatrix;
        };

        struct MeshInstancesData
        {
            class Model const* parentModelPtr = NULL;
            uint32_t lastRebuildInstanceCount = 0; //includes extra overhead
            uint32_t instanceCount = 0;
            uint32_t drawCountsOffset = 0;
            uint32_t drawCommandsOffset = 0;
            uint32_t outputObjectsOffset = 0;
        };

        //buffers and allocation
        static std::unique_ptr<PaperMemory::DeviceAllocation> drawDataAllocation;
        static std::list<CommonMeshGroup*> commonMeshGroups;
        std::unique_ptr<PaperMemory::Buffer> drawDataBuffer; //no need for a host visible copy since this is only written by compute shaders and read by the graphics pipeline. Draw counts does get reset to 0 though

        uint32_t drawCountsRange = 0;
        std::vector<VkDeviceSize> bufferFrameOffsets;
        float instanceCountOverhead = 1.3;
        static std::vector<class ModelInstance*> rebuildAllocationAndBuffers(class RenderEngine* renderer);
        static bool rebuild;
        void rebuildBuffer();

        std::mutex addAndRemoveLock;
        std::unordered_map<struct LODMesh const*, struct MeshInstancesData> meshesData;
        std::unordered_map<class ModelInstance*, std::vector<struct LODMesh const*>> instanceMeshes;

        class RenderEngine* rendererPtr;
        class RenderPass const* renderPassPtr;
        class RasterPipeline const* pipelinePtr;

    public:
        CommonMeshGroup(class RenderEngine* renderer, class RenderPass const* renderPass, RasterPipeline const* pipeline);
        ~CommonMeshGroup();

        static std::vector<class ModelInstance*> verifyBuffersSize(RenderEngine* renderer);

        void addInstanceMeshes(class ModelInstance* instance, const std::vector<struct LODMesh const*>& instanceMeshesData);
        void removeInstanceMeshes(class ModelInstance* instance);

        void draw(const VkCommandBuffer& cmdBuffer, uint32_t currentFrame);
        void clearDrawCounts(const VkCommandBuffer& cmdBuffer);

        const std::vector<VkDeviceSize>& getBufferFrameOffsets() const { return bufferFrameOffsets; }
        VkDeviceAddress getBufferAddress() const { return (drawDataBuffer) ? drawDataBuffer->getBufferDeviceAddress() : NULL; }
        const std::unordered_map<struct LODMesh const*, MeshInstancesData>& getMeshesData() const { return meshesData; }
    };
}