#pragma once
#include "Buffer.h"
#include "Descriptor.h"
#include "Pipeline.h"

namespace Renderer
{
    struct DrawBufferData
    {
        glm::mat4 modelMatrix;
    };

    struct DrawBufferObject
    {
        Mesh const* mesh;
        glm::mat4 const* modelMatrix;

        void* heapLocation = NULL;
        std::list<DrawBufferObject*>::iterator reference;
    };
    
    class IndirectDrawBuffer
    {
    private:
        StorageBuffer drawBuffer;
        StorageBuffer dataBuffer;
        VkDeviceSize capacity;
        //VkDeviceAddress GPUaddress;
        std::list<DrawBufferObject*> storedReferences;
        void* dataPtr;

        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;
        DescriptorAllocator* descriptorsPtr;
        Pipeline const* pipelinePtr;

    public:
        IndirectDrawBuffer(Device *device, CmdBufferAllocator *commands, DescriptorAllocator* descriptor, Pipeline const* pipeline, uint32_t capacity);
        ~IndirectDrawBuffer();

        void addElement(DrawBufferObject& object);
        void removeElement(DrawBufferObject& object);
        void updateBuffers(const VkCommandBuffer& cmdBuffer, uint32_t currentFrame);
        void draw(const VkCommandBuffer& cmdBuffer);

        uint32_t getDrawCount() const { return storedReferences.size(); }
        const VkBuffer& getDrawBuffer() const { return drawBuffer.getBuffer(); }
        const VkBuffer& getDataBuffer() const { return dataBuffer.getBuffer(); }
    };
}