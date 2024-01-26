#pragma once
#include "Buffer.h"
#include "Descriptor.h"
#include "Pipeline.h"

#include <unordered_map>

namespace Renderer
{
    struct DrawBufferData
    {
        glm::mat4 modelMatrix;
    };

    struct DrawBufferObject
    {
        glm::mat4 const* modelMatrix;
        Mesh const* mesh;
        std::list<DrawBufferObject*>::iterator reference;
    };
    
    struct DrawBufferTreeNode
    {
        std::vector<StorageBuffer*> drawBuffers; //raw ptr
        std::vector<StorageBuffer*> dataBuffers; //raw ptr
        std::list<DrawBufferObject*> objects;
    };
    
    class IndirectDrawBuffer
    {
    private:
        //VkDeviceAddress GPUaddress;
        std::unordered_map<Mesh const*, DrawBufferTreeNode> drawCallTree;
        
        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;
        DescriptorAllocator* descriptorsPtr;
        Pipeline const* pipelinePtr;

    public:
        IndirectDrawBuffer(Device *device, CmdBufferAllocator *commands, DescriptorAllocator* descriptor, Pipeline const* pipeline);
        ~IndirectDrawBuffer();

        void addElement(DrawBufferObject& object);
        void removeElement(DrawBufferObject& object);
        std::vector<QueueReturn> draw(const VkCommandBuffer& cmdBuffer, uint32_t currentFrame);
        const std::unordered_map<Mesh const*, DrawBufferTreeNode>& getDrawCallTree() const { return drawCallTree; }
    };
}