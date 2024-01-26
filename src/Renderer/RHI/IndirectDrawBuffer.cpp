#include "IndirectDrawBuffer.h"

namespace Renderer
{
    IndirectDrawBuffer::IndirectDrawBuffer(Device *device, CmdBufferAllocator *commands, DescriptorAllocator* descriptor, Pipeline const* pipeline)
        :devicePtr(device),
        commandsPtr(commands),
        descriptorsPtr(descriptor),
        pipelinePtr(pipeline)
    {
        /*VkBufferDeviceAddressInfoKHR addressInfo = {};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
        addressInfo.pNext = NULL;
        addressInfo.buffer = VK_NULL_HANDLE;
		VkDeviceAddress srcPtr = vkGetBufferDeviceAddressKHR(devicePtr->getDevice(), &addressInfo);*/
    }

    IndirectDrawBuffer::~IndirectDrawBuffer()
    {
        for(int i = 0; i < CmdBufferAllocator::getFrameCount(); i++)
        {
            for(auto& [mesh, buffers] : drawCallTree)
            {
                delete buffers.dataBuffers.at(i);
                delete buffers.drawBuffers.at(i);
            }
        }
    }

    void IndirectDrawBuffer::addElement(DrawBufferObject &object)
    {
        if(!drawCallTree.count(object.mesh))
        {
            drawCallTree[object.mesh].dataBuffers.resize(CmdBufferAllocator::getFrameCount());
            drawCallTree[object.mesh].drawBuffers.resize(CmdBufferAllocator::getFrameCount());
        }

        drawCallTree[object.mesh].objects.push_back(&object);
        object.reference = drawCallTree.at(object.mesh).objects.end();
        object.reference--;

        for(int i = 0; i < CmdBufferAllocator::getFrameCount(); i++)
        {
            delete[] drawCallTree.at(object.mesh).dataBuffers.at(i);
            drawCallTree.at(object.mesh).dataBuffers.at(i) = 
                new StorageBuffer(devicePtr, commandsPtr, (VkDeviceSize)(drawCallTree.at(object.mesh).objects.size() * sizeof(DrawBufferData)));
            
            delete[] drawCallTree.at(object.mesh).drawBuffers.at(i);
            drawCallTree.at(object.mesh).drawBuffers.at(i) = 
                new StorageBuffer(devicePtr, commandsPtr, (VkDeviceSize)(drawCallTree.at(object.mesh).objects.size() * sizeof(VkDrawIndexedIndirectCommand)));
        }
    }

    void IndirectDrawBuffer::removeElement(DrawBufferObject &object)
    {
        drawCallTree.at(object.mesh).objects.erase(object.reference);
        object.reference = std::list<Renderer::DrawBufferObject*>::iterator();
        //buffers really only need to be updated whenever an object is added
    }

    std::vector<QueueReturn> IndirectDrawBuffer::draw(const VkCommandBuffer& cmdBuffer, uint32_t currentFrame)
    {
        std::vector<QueueReturn> returnFences;
        for(auto& [mesh, node] : drawCallTree)
        {
            //add object data
            std::vector<DrawBufferData> stagingData(node.objects.size());
            uint32_t i = 0;
            for(auto object = node.objects.begin(); object != node.objects.end(); object++)
            {
                stagingData.at(i) = {
                    .modelMatrix = *(*object)->modelMatrix
                };
                i++;
            }
            
            StagingBuffer objDataStaging(devicePtr, commandsPtr, stagingData.size() * sizeof(DrawBufferData));
            objDataStaging.mapData(stagingData.data(), 0, stagingData.size() * sizeof(DrawBufferData));
            returnFences.push_back(std::move(node.dataBuffers.at(currentFrame)->setDataFromStaging(objDataStaging, stagingData.size() * sizeof(DrawBufferData))));

            //add in draw commands
            std::vector<VkDrawIndexedIndirectCommand> drawCommands(node.objects.size());
            i = 0;
            for(auto object = node.objects.begin(); object != node.objects.end(); object++)
            {
                //add draw command
                VkDrawIndexedIndirectCommand command = {};
                command.indexCount = (*object)->mesh->getIndexBuffer().getLength();
                command.instanceCount = 1;
                command.firstIndex = 0;
                command.vertexOffset = 0;
                command.firstInstance = i;//

                drawCommands.at(i) = command;
                i++;
            }

            StagingBuffer objDrawStaging(devicePtr, commandsPtr, drawCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
            objDrawStaging.mapData(drawCommands.data(), 0, drawCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
            returnFences.push_back(std::move(node.drawBuffers.at(currentFrame)->setDataFromStaging(objDrawStaging, drawCommands.size() * sizeof(VkDrawIndexedIndirectCommand))));

            //update descriptor set (2nd non-global descriptor set)
            VkDescriptorSet objDescriptorSet = descriptorsPtr->allocateDescriptorSet(pipelinePtr->getDescriptorSetLayouts().at(2), currentFrame);

            descriptorsPtr->writeUniform(
                node.dataBuffers.at(currentFrame)->getBuffer(),
                sizeof(DrawBufferData) * node.objects.size(),
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

            //bind vbo and send draw calls
            VkDeviceSize offset[1] = {0};

            vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &((*(node.objects.begin()))->mesh->getVertexBuffer().getBuffer()), offset);
            vkCmdBindIndexBuffer(cmdBuffer, ((*(node.objects.begin()))->mesh->getIndexBuffer().getBuffer()), 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexedIndirect(cmdBuffer, node.drawBuffers.at(currentFrame)->getBuffer(), 0, node.objects.size(), sizeof(VkDrawIndexedIndirectCommand));
        }

        return returnFences;
    }
}