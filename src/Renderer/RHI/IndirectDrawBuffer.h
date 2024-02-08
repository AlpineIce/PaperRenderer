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
        std::vector<std::shared_ptr<StorageBuffer>> bufferData; //DrawBufferData
    };
    
    class IndirectDrawBuffer
    {
    private:
        //VkDeviceAddress GPUaddress;
        std::unordered_map<Mesh const*, DrawBufferTreeNode> drawCallTree;
        std::vector<std::shared_ptr<StorageBuffer>> drawCountBuffer;
        
        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;
        DescriptorAllocator* descriptorsPtr;
        RasterPipeline const* pipelinePtr;

    public:
        IndirectDrawBuffer(Device *device, CmdBufferAllocator *commands, DescriptorAllocator* descriptor, RasterPipeline const* pipeline);
        ~IndirectDrawBuffer();

        void addElement(DrawBufferObject& object);
        void removeElement(DrawBufferObject& object);
        std::vector<QueueReturn> performCulling(const VkCommandBuffer& cmdBuffer, ComputePipeline* cullingPipeline, const CullingFrustum& frustumData, glm::mat4 projection, glm::mat4 view, uint32_t currentFrame);
        void draw(const VkCommandBuffer& cmdBuffer, uint32_t currentFrame);
        const std::unordered_map<Mesh const*, DrawBufferTreeNode>& getDrawCallTree() const { return drawCallTree; }
    };
}