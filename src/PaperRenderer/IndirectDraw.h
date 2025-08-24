#pragma once
#include "Descriptor.h"
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
        //shader structs
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

        //buffer helper functions
        std::vector<class ModelInstance*> rebuildBuffer();
        BufferSizeRequirements getBuffersRequirements();
        void setDrawCommandData() const;

        //descriptors
        const ResourceDescriptor descriptorSet;

        //other
        uint32_t drawCommandCount = 0;
        bool rebuild = true;
        std::unordered_map<class ModelInstance const*, std::unordered_map<struct LODMesh const*, MeshInstancesData>> instanceMeshesData = {};
        std::unordered_map<class ModelInstance*, std::vector<struct LODMesh const*>> instanceMeshes;

        //references
        class RenderEngine& renderer;
        const class RenderPass& renderPass;
        const class Material& material;

    public:
        CommonMeshGroup(class RenderEngine& renderer, const class RenderPass& renderPass, const class Material& material);
        ~CommonMeshGroup();
        CommonMeshGroup(const CommonMeshGroup&) = delete;

        std::vector<class ModelInstance*> verifyBufferSize();

        void addInstanceMesh(ModelInstance& instance, const LODMesh& instanceMeshData);
        void removeInstanceMeshes(class ModelInstance& instance);

        void draw(const VkCommandBuffer& cmdBuffer) const;
        void clearDrawCommand(const VkCommandBuffer& cmdBuffer) const;
        void addOwner(Queue& queue);

        //const Buffer& getModelMatricesBuffer() { return *modelMatricesBuffer; }
        const Buffer& getDrawCommandsBuffer() const { return *drawCommandsBuffer; }
        const Buffer& getModelMatricesBuffer() const { return *modelMatricesBuffer; }
        const std::unordered_map<class ModelInstance const*, std::unordered_map<struct LODMesh const*, MeshInstancesData>>& getInstanceMeshesData() const { return instanceMeshesData; }
        
    };
}