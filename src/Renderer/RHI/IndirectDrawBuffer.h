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
    };

    struct DrawBufferData
    {
        glm::mat4 modelMatrix;
        glm::vec4 position;
    };

    struct ShaderDrawCommand
    {
        VkDrawIndexedIndirectCommand command;
        float padding;
    };

    struct ObjectPreprocessStride
    {
        DrawBufferData inputObject;
        glm::mat4 outputTransform;
        ShaderDrawCommand inputCommand = {};
        ShaderDrawCommand outputCommand = {};
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
        std::shared_ptr<StorageBuffer> bufferData; //THE UBER-BUFFER
    };

    class IndirectDrawContainer
    {
    private:
        //VkDeviceAddress GPUaddress;
        std::unordered_map<Mesh const*, DrawBufferTreeNode> drawCallTree;
        std::vector<uint32_t> objectGroupLocations;
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
        std::vector<std::vector<ObjectPreprocessStride>> getObjectSizes(uint32_t currentBufferSize);
        uint32_t getDrawCountsSize(uint32_t currentBufferSize);
        void dispatchCulling(const VkCommandBuffer& cmdBuffer, ComputePipeline const* cullingPipeline, const CullingFrustum& frustum, StorageBuffer const* renderData, glm::mat4 projection, glm::mat4 view, uint32_t currentFrame);
        void draw(const VkCommandBuffer& cmdBuffer, IndirectRenderingData const* renderData, uint32_t currentFrame);
        const std::unordered_map<Mesh const*, DrawBufferTreeNode>& getDrawCallTree() const { return drawCallTree; }
    };
}