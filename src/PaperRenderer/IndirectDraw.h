#pragma once
#include "VulkanResources.h"
#include "gtc/quaternion.hpp"

#include <unordered_map>
#include <list>

namespace PaperRenderer
{
    //draw command requires 8 byte alignment because of the use of buffer addressing
    struct DrawCommand
    {
        VkDrawIndexedIndirectCommand command = {};
        float padding = 0.0f;
    };

    struct ShaderOutputObject
    {
        glm::mat3x4 modelMatrix;
    };

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

        struct MeshInstancesData
        {
            class Model const* parentModelPtr = NULL;
            uint32_t lastRebuildInstanceCount = 0; //includes extra overhead
            uint32_t instanceCount = 0;
            uint32_t drawCommandIndex = 0;
            uint32_t matricesStartIndex = 0;
        };

        //buffers and allocation
        std::unique_ptr<Buffer> modelMatricesBuffer;
        std::unique_ptr<Buffer> drawCommandsBuffer;
        std::array<std::deque<std::unique_ptr<Buffer>>, 2> destructionQueue;

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
        std::vector<class ModelInstance*> rebuildBuffer();
        bool rebuild;
        BufferSizeRequirements getBuffersRequirements();
        void setDrawCommandData() const;

        std::unordered_map<struct LODMesh const*, struct MeshInstancesData> meshesData;
        std::unordered_map<class ModelInstance*, std::vector<struct LODMesh const*>> instanceMeshes;

        class RenderEngine& renderer;
        class RenderPass const* renderPassPtr;

    public:
        CommonMeshGroup(class RenderEngine& renderer, class RenderPass const* renderPass);
        ~CommonMeshGroup();
        CommonMeshGroup(const CommonMeshGroup&) = delete;

        std::vector<class ModelInstance*> verifyBufferSize();

        void addInstanceMesh(ModelInstance& instance, const LODMesh& instanceMeshData);
        void removeInstanceMeshes(class ModelInstance& instance);

        void draw(const VkCommandBuffer& cmdBuffer, const class Material& material) const;
        void clearDrawCommand(const VkCommandBuffer& cmdBuffer) const;
        void readInstanceCounts(VkCommandBuffer cmdBuffer, Buffer& buffer, uint32_t startIndex) const;

        //const Buffer& getModelMatricesBuffer() { return *modelMatricesBuffer; }
        const Buffer& getDrawCommandsBuffer() const { return *drawCommandsBuffer; }
        const Buffer& getModelMatricesBuffer() const { return *modelMatricesBuffer; }
        const std::unordered_map<struct LODMesh const*, MeshInstancesData>& getMeshesData() const { return meshesData; }
        
    };
}