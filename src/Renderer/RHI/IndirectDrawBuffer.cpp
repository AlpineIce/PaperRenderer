#include "IndirectDrawBuffer.h"

namespace Renderer
{
    IndirectDrawBuffer::IndirectDrawBuffer(Device *device, CmdBufferAllocator *commands, DescriptorAllocator* descriptor, Pipeline const* pipeline, uint32_t capacity)
        :devicePtr(device),
        commandsPtr(commands),
        descriptorsPtr(descriptor),
        pipelinePtr(pipeline),
        capacity(capacity),
        dataPtr(NULL),
        drawBuffer(device, commands, capacity * sizeof(VkDrawIndirectCommand)),
        dataBuffer(device, commands, capacity * sizeof(DrawBufferData))
    {
        /*VkBufferDeviceAddressInfoKHR addressInfo = {};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
        addressInfo.pNext = NULL;
        addressInfo.buffer = VK_NULL_HANDLE;
		VkDeviceAddress srcPtr = vkGetBufferDeviceAddressKHR(devicePtr->getDevice(), &addressInfo);*/
    }

    IndirectDrawBuffer::~IndirectDrawBuffer()
    {
    }

    void IndirectDrawBuffer::addElement(DrawBufferObject &object)
    {
        if(storedReferences.size() < capacity)
        {
            object.heapLocation = (DrawBufferData*)dataPtr + storedReferences.size();
            storedReferences.push_back(&object);
            object.reference = storedReferences.end();
            object.reference--;
        }
        else throw std::runtime_error("Implementation for over " + std::to_string(capacity) + " objects in a draw buffer not yet implemented.");
    }

    void IndirectDrawBuffer::removeElement(DrawBufferObject &object)
    {
        storedReferences.erase(object.reference);
        object.heapLocation = NULL;
        object.reference = std::list<Renderer::DrawBufferObject*>::iterator();
    }

    void IndirectDrawBuffer::updateBuffers(const VkCommandBuffer& cmdBuffer, uint32_t currentFrame)
    {
        //add object data
        std::vector<DrawBufferData> stagingData(storedReferences.size());
        uint32_t i = 0;
        for(auto object = storedReferences.begin(); object != storedReferences.end(); object++)
        {
            stagingData.at(i) = {
                .modelMatrix = *(*object)->modelMatrix
            };
            i++;
        }
        
        StagingBuffer objDataStaging(devicePtr, commandsPtr, stagingData.size() * sizeof(DrawBufferData));
        objDataStaging.mapData(stagingData.data(), 0, stagingData.size() * sizeof(DrawBufferData));
        
        dataBuffer.setDataFromStaging(objDataStaging, stagingData.size() * sizeof(DrawBufferData));

        //update draw calls (honestly pretty inneficient, could be optimized to update only the required bits)
        std::vector<VkDrawIndexedIndirectCommand> drawCommands(storedReferences.size());
        i = 0;
        for(auto object = storedReferences.begin(); object != storedReferences.end(); object++)
        {
            //add draw command
            VkDrawIndexedIndirectCommand command = {};
            command.indexCount = (*object)->mesh->getIndexBufferSize();
            command.instanceCount = 1;
            command.firstIndex = 0;
            command.vertexOffset = 0;
            command.firstInstance = i;//

            drawCommands.at(i) = command;
            i++;
        }

        StagingBuffer objDrawStaging(devicePtr, commandsPtr, drawCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
        objDrawStaging.mapData(drawCommands.data(), 0, drawCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
        
        drawBuffer.setDataFromStaging(objDrawStaging, drawCommands.size() * sizeof(VkDrawIndexedIndirectCommand));

        //update descriptor set (2nd non-global descriptor set)
        VkDescriptorSet objDescriptorSet = descriptorsPtr->allocateDescriptorSet(pipelinePtr->getDescriptorSetLayouts().at(2), currentFrame);

        descriptorsPtr->writeUniform(
            dataBuffer.getBuffer(),
            sizeof(DrawBufferData) * storedReferences.size(),
            0,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            objDescriptorSet);
        
        vkCmdBindDescriptorSets(
            cmdBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelinePtr->getLayout(),
            2, //object bind point
            1,
            &objDescriptorSet,
            0,
            0);
    }

    void IndirectDrawBuffer::draw(const VkCommandBuffer& cmdBuffer)
    {
        VkDeviceSize offset[1] = {0};

        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &((*(storedReferences.begin()))->mesh->getVertexBuffer()), offset);
        vkCmdBindIndexBuffer(cmdBuffer, ((*(storedReferences.begin()))->mesh->getIndexBuffer()), 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexedIndirect(cmdBuffer, drawBuffer.getBuffer(), 0, storedReferences.size(), sizeof(VkDrawIndexedIndirectCommand));
    }
}