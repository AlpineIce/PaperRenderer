#pragma once
#include "Memory/VulkanResources.h"
#include "Descriptor.h"
#include "Pipeline.h"
#include "glm/gtx/quaternion.hpp"

#include <unordered_map>

namespace PaperRenderer
{
    struct ShaderDrawCommand
    {
        VkDrawIndexedIndirectCommand command;
    };

    struct AABB
    {
        float posX = 0.0f;
        float negX = 0.0f;
        float posY = 0.0f;
        float negY = 0.0f;
        float posZ = 0.0f;
        float negZ = 0.0f;
    };

    struct ShaderInputObject
    {
        //transformation
        glm::vec4 position;
        glm::vec4 scale; 
        glm::mat4 rotation; //quat -> mat4... could possibly be a mat3
        AABB bounds;
        uint32_t lodCount;
        uint32_t lodsOffset;
    };

    struct ShaderLOD
    {
        uint32_t meshCount;
        uint32_t meshesLocationOffset;
    };

    struct LODMesh
    {
        uint32_t vboOffset;
        uint32_t vertexCount;
        uint32_t iboOffset;
        uint32_t indexCount;

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

    struct ModelTransform
    {
        glm::vec3 position = glm::vec3(0.0f); //world position
        glm::vec3 scale = glm::vec3(1.0f); //local scale
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); //local rotation
    };

    struct DrawBufferObject
    {
        LODMesh* parentMesh;
        ShaderLOD* parentLOD;
        class Model const* parentModel; //forward declaration of parent model used for vbo/ibo binding (too lazy to figure out function pointers)
        ModelTransform const* objectTransform;
        bool const* isVisible;
        uint64_t selfIndex;
    };
    
    struct IndirectRenderingData
    {
        uint32_t objectCount;
        VkBufferCopy LODOffsetsRegion;
        VkBufferCopy meshLODOffsetsRegion;
        VkBufferCopy meshDrawCountsRegion;
        VkBufferCopy meshDrawCommandsRegion;
        VkBufferCopy meshOutputObjectsRegion;
        VkBufferCopy inputObjectsRegion;

        std::vector<char> stagingData;
        std::unique_ptr<PaperMemory::DeviceAllocation> bufferAllocation;
        std::unique_ptr<PaperMemory::Buffer> bufferData; //THE UBER-BUFFER
    };

    class IndirectDrawContainer
    {
    private:
        std::unordered_map<LODMesh*, std::vector<DrawBufferObject*>> drawCallTree;
        std::vector<uint32_t> outputObjectsLocations;
        std::vector<uint32_t> drawCommandsLocations;
        uint32_t drawCountsLocation;
        
        Device* devicePtr;
        DescriptorAllocator* descriptorsPtr;
        RasterPipeline const* pipelinePtr;

    public:
        IndirectDrawContainer(Device *device, DescriptorAllocator* descriptor, RasterPipeline const* pipeline);
        ~IndirectDrawContainer();

        void addElement(DrawBufferObject& object);
        void removeElement(DrawBufferObject& object);

        uint32_t getOutputObjectSize(uint32_t currentBufferSize);
        uint32_t getDrawCommandsSize(uint32_t currentBufferSize);
        uint32_t getDrawCountsSize(uint32_t currentBufferSize);

        void draw(const VkCommandBuffer& cmdBuffer, const IndirectRenderingData& renderData, uint32_t currentFrame);
        const std::unordered_map<LODMesh*, std::vector<DrawBufferObject*>>& getDrawCallTree() const { return drawCallTree; }
    };
}