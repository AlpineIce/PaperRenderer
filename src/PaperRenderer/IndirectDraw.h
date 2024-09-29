#pragma once
#include "VulkanResources.h"
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
        };

        struct MeshInstancesData
        {
            class Model const* parentModelPtr = NULL;
            uint32_t lastRebuildInstanceCount = 0; //includes extra overhead
            uint32_t instanceCount = 0;
            uint32_t drawCommandIndex = 0;
            uint32_t matricesStartIndex = 0;
        };

        //buffers and allocation
        static std::unique_ptr<Buffer> modelMatricesBuffer;
        static std::unique_ptr<Buffer> drawCommandsBuffer;
        static std::list<CommonMeshGroup*> commonMeshGroups;

        struct BufferSizeRequirements
        {
            uint32_t drawCommandCount = 0;
            uint32_t matricesCount = 0;

            void operator +=(BufferSizeRequirements sizeRequirements)
            {
                drawCommandCount += sizeRequirements.drawCommandCount;
                matricesCount += sizeRequirements.matricesCount;
            }
        };

        uint32_t drawCommandCount = 0;
        float instanceCountOverhead = 1.3;
        static std::vector<class ModelInstance*> rebuildBuffers(class RenderEngine* renderer);
        static bool rebuild;
        BufferSizeRequirements getBuffersRequirements(const BufferSizeRequirements currentSizes);
        void setDrawCommandData(const Buffer& stagingBuffer) const;

        std::mutex addAndRemoveLock;
        std::unordered_map<struct LODMesh const*, struct MeshInstancesData> meshesData;
        std::unordered_map<class ModelInstance*, std::vector<struct LODMesh const*>> instanceMeshes;

        class RenderEngine* rendererPtr;
        class RenderPass const* renderPassPtr;

    public:
        CommonMeshGroup(class RenderEngine* renderer, class RenderPass const* renderPass);
        ~CommonMeshGroup();
        CommonMeshGroup(const CommonMeshGroup&) = delete;

        static std::vector<class ModelInstance*> verifyBuffersSize(RenderEngine* renderer);

        void addInstanceMeshes(class ModelInstance* instance, const std::vector<struct LODMesh const*>& instanceMeshesData);
        void removeInstanceMeshes(class ModelInstance* instance);

        void draw(const VkCommandBuffer& cmdBuffer, const class RasterPipeline& pipeline);
        void clearDrawCommand(const VkCommandBuffer& cmdBuffer);

        static Buffer const* getModelMatricesBuffer() { return modelMatricesBuffer.get(); }
        static Buffer const* getDrawCommandsBuffer() { return drawCommandsBuffer.get(); }
        const std::unordered_map<struct LODMesh const*, MeshInstancesData>& getMeshesData() const { return meshesData; }
    };
}