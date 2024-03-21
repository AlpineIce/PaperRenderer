#pragma once
#include "Buffer.h"
#include "Descriptor.h"
#include "Pipeline.h"
#include "glm/gtx/quaternion.hpp"

#include <unordered_map>

namespace Renderer
{
    struct CullingFrustum
    {
        glm::vec4 frustum; //(left, right, top, bottom)
        glm::vec2 zPlanes; //(near, far)
    };

    struct CullingInputData
    {
        VkDeviceAddress bufferAddress; //used with offsets to make LOD selection possible in a compute shader
        VkDeviceAddress asInstancesAddress;
        glm::vec4 camPos;
        glm::mat4 projection;
        glm::mat4 view;
        uint32_t objectCount;
        glm::vec3 padding2;
        CullingFrustum frustumData;
    };

    struct ShaderDrawCommand
    {
        VkDrawIndexedIndirectCommand command;
    };

    struct ShaderInputObject
    {
        //transformation
        glm::vec4 position;
        glm::mat4 rotation; //quat -> mat4... could possibly be a mat3
        glm::vec4 scale;
        
        uint32_t lodCount;
        uint32_t lodsOffset;
        VkDeviceAddress blasReference;
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
        float const* sphericalBounds;
        std::list<DrawBufferObject*>::iterator reference;
    };
    
    struct IndirectRenderingData
    {
        uint32_t lightsOffset;
        uint32_t lightCount;
        uint32_t objectCount;
        VkBufferCopy fragmentInputRegion;
        VkBufferCopy LODOffsetsRegion;
        VkBufferCopy meshLODOffsetsRegion;
        VkBufferCopy meshDrawCountsRegion;
        VkBufferCopy meshDrawCommandsRegion;
        VkBufferCopy meshOutputObjectsRegion;
        VkBufferCopy inputObjectsRegion;
        VkBufferCopy modelLODsRegion;

        std::vector<char> stagingData;
        std::shared_ptr<StorageBuffer> bufferData; //THE UBER-BUFFER
    };

    class IndirectDrawContainer
    {
    private:
        std::unordered_map<LODMesh*, std::list<DrawBufferObject*>> drawCallTree;
        std::vector<uint32_t> outputObjectsLocations;
        std::vector<uint32_t> drawCommandsLocations;
        uint32_t drawCountsLocation;
        
        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;
        DescriptorAllocator* descriptorsPtr;
        RasterPipeline const* pipelinePtr;

    public:
        IndirectDrawContainer(Device *device, CmdBufferAllocator *commands, DescriptorAllocator* descriptor, RasterPipeline const* pipeline);
        ~IndirectDrawContainer();

        void addElement(DrawBufferObject& object);
        void removeElement(DrawBufferObject& object);

        uint32_t getOutputObjectSize(uint32_t currentBufferSize);
        uint32_t getDrawCommandsSize(uint32_t currentBufferSize);
        uint32_t getDrawCountsSize(uint32_t currentBufferSize);

        void draw(const VkCommandBuffer& cmdBuffer, IndirectRenderingData const* renderData, uint32_t currentFrame);
        const std::unordered_map<LODMesh*, std::list<DrawBufferObject*>>& getDrawCallTree() const { return drawCallTree; }
    };
}