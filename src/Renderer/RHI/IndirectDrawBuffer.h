#pragma once
#include "Buffer.h"
#include "Descriptor.h"
#include "Pipeline.h"

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
        glm::mat4 projection;
        glm::mat4 view;
        CullingFrustum frustumData;
        uint32_t matrixCount;
        uint32_t drawCountIndex;
        uint32_t indexCount;
        uint32_t padding;
    };

    struct ShaderDrawCommand
    {
        VkDrawIndexedIndirectCommand command;
    };

    struct ShaderInputObject
    {
        glm::mat4 modelMatrix;
        glm::vec4 position;
    };

    struct ShaderOutputObject
    {
        glm::mat4 modelMatrix;
        glm::mat4 transformMatrix;
    };

    struct DrawBufferObject
    {
        glm::mat4 const* modelMatrix;
        glm::vec3 const* position;
        Mesh const* mesh;
        std::list<DrawBufferObject*>::iterator reference;
    };
    
    struct DrawBufferTreeNode
    {
        std::list<DrawBufferObject*> objects;
        std::vector<std::shared_ptr<UniformBuffer>> cullingInputData; //CullingInputData
    };
    
    struct IndirectRenderingData
    {
        uint32_t lightsOffset;
        uint32_t lightCount;
        std::vector<char> stagingData;
        VkBufferCopy fragmentInputRegion;
        VkBufferCopy preprocessInputRegion;
        VkBufferCopy drawCountsRegion;
        std::shared_ptr<StorageBuffer> bufferData; //THE UBER-BUFFER
    };

    class IndirectDrawContainer
    {
    private:
        //VkDeviceAddress GPUaddress;
        std::unordered_map<Mesh const*, DrawBufferTreeNode> drawCallTree;
        std::vector<uint32_t> inputObjectsLocations;
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
        std::vector<ShaderInputObject> getInputObjects(uint32_t currentBufferSize);
        uint32_t getDrawCommandsSize(uint32_t currentBufferSize);
        
        uint32_t getDrawCountsSize(uint32_t currentBufferSize);
        void dispatchCulling(const VkCommandBuffer& cmdBuffer, ComputePipeline const* cullingPipeline, const CullingFrustum& frustum, StorageBuffer const* renderData, glm::mat4 projection, glm::mat4 view, uint32_t currentFrame);
        void draw(const VkCommandBuffer& cmdBuffer, IndirectRenderingData const* renderData, uint32_t currentFrame);
        const std::unordered_map<Mesh const*, DrawBufferTreeNode>& getDrawCallTree() const { return drawCallTree; }
    };
}