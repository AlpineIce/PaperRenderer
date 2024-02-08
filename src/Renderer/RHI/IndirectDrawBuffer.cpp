#include "IndirectDrawBuffer.h"

namespace Renderer
{
    IndirectDrawBuffer::IndirectDrawBuffer(Device *device, CmdBufferAllocator *commands, DescriptorAllocator* descriptor, RasterPipeline const* pipeline)
        :devicePtr(device),
        commandsPtr(commands),
        descriptorsPtr(descriptor),
        pipelinePtr(pipeline),
        drawCountBuffer(commands->getFrameCount())
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
        if(drawCallTree.count(object.mesh) == 0)
        {
            drawCallTree[object.mesh].cullingInputData.resize(commandsPtr->getFrameCount());
            drawCallTree[object.mesh].bufferData.resize(commandsPtr->getFrameCount());
        }
        drawCallTree[object.mesh].objects.push_back(&object);
        object.reference = drawCallTree.at(object.mesh).objects.end();
        object.reference--;
    }

    void IndirectDrawBuffer::removeElement(DrawBufferObject &object)
    {
        drawCallTree.at(object.mesh).objects.erase(object.reference);
        object.reference = std::list<Renderer::DrawBufferObject*>::iterator();
    }

    std::vector<QueueReturn> IndirectDrawBuffer::performCulling(const VkCommandBuffer &cmdBuffer, ComputePipeline *cullingPipeline, const CullingFrustum& frustumData, glm::mat4 projection, glm::mat4 view, uint32_t currentFrame)
    {
        std::vector<QueueReturn> returnFences;

        std::vector<uint32_t> drawCounts(drawCallTree.size());
        for(uint32_t& count : drawCounts) count = 0;
        
        drawCountBuffer.at(currentFrame) = std::make_shared<StorageBuffer>(devicePtr, commandsPtr, sizeof(uint32_t) * drawCallTree.size());
        StagingBuffer drawCountsStaging(devicePtr, commandsPtr, drawCounts.size() * sizeof(uint32_t));
        drawCountsStaging.mapData(drawCounts.data(), 0, drawCounts.size() * sizeof(uint32_t));
        returnFences.push_back(std::move(drawCountBuffer.at(currentFrame)->setDataFromStaging(drawCountsStaging, drawCounts.size() * sizeof(uint32_t))));

        //set 0
        VkDescriptorSet set0Descriptor = descriptorsPtr->allocateDescriptorSet(cullingPipeline->getDescriptorSetLayouts().at(0), currentFrame);

        //set 0 - binding 0
        descriptorsPtr->writeUniform(
            drawCountBuffer.at(currentFrame)->getBuffer(),
            sizeof(uint32_t) * drawCallTree.size(),
            0,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            set0Descriptor);

        vkCmdBindDescriptorSets(cmdBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            cullingPipeline->getLayout(),
            0, //bind point
            1,
            &set0Descriptor,
            0,
            0);

        
        uint32_t drawCountIndex = 0;
        for(auto& [mesh, node] : drawCallTree) //per vertex/index buffer instance (mesh) group culling
        {
            //uniform buffer
            CullingInputData inputData = {
                .projection = projection,
                .view = view,
                .frustumData = frustumData,
                .matrixCount = (uint32_t)node.objects.size(),
                .drawCountIndex = drawCountIndex
            };
            node.cullingInputData.at(currentFrame) = std::make_shared<UniformBuffer>(devicePtr, commandsPtr, sizeof(CullingInputData));
            node.cullingInputData.at(currentFrame)->updateUniformBuffer(&inputData, sizeof(CullingInputData));

            //storage buffer
            std::vector<ObjectPreprocessStride> bufferData(node.objects.size());
            node.bufferData.at(currentFrame) = std::make_shared<StorageBuffer>(devicePtr, commandsPtr, node.objects.size() * sizeof(ObjectPreprocessStride));

            //add necessary data
            uint32_t i = 0;
            for(auto object = node.objects.begin(); object != node.objects.end(); object++)
            {
                //objects
                bufferData.at(i).inputObject = {
                    .modelMatrix = *(*object)->modelMatrix,
                    .position = glm::vec4(*(*object)->position, (*object)->mesh->getSphericalBounding()) //w component of shader position is the spherical bounds;
                };

                //draw commands
                ShaderDrawCommand command = {};
                command.command.indexCount = (*object)->mesh->getIndexBuffer().getLength();
                command.command.instanceCount = 1; //TODO INSTANCING
                command.command.firstIndex = 0;
                command.command.vertexOffset = 0;
                command.command.firstInstance = i;

                bufferData.at(i).inputCommand = command;

                i++;
            }
            
            //set buffer data from staging
            StagingBuffer dataStagingBuffer(devicePtr, commandsPtr, bufferData.size() * sizeof(ObjectPreprocessStride));
            dataStagingBuffer.mapData(bufferData.data(), 0, bufferData.size() * sizeof(ObjectPreprocessStride));
            returnFences.push_back(std::move(node.bufferData.at(currentFrame)->setDataFromStaging(dataStagingBuffer, bufferData.size() * sizeof(ObjectPreprocessStride))));

            //set1
            VkDescriptorSet set1Descriptor = descriptorsPtr->allocateDescriptorSet(cullingPipeline->getDescriptorSetLayouts().at(1), currentFrame);

            //set1 - binding 0
            descriptorsPtr->writeUniform(
                node.cullingInputData.at(currentFrame)->getBuffer(),
                sizeof(CullingInputData),
                0,
                0,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                set1Descriptor);
            
            //set1 - binding 1
            descriptorsPtr->writeUniform(
                node.bufferData.at(currentFrame)->getBuffer(),
                sizeof(ObjectPreprocessStride) * node.objects.size(),
                0,
                1,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                set1Descriptor);

            vkCmdBindDescriptorSets(cmdBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                cullingPipeline->getLayout(),
                1, //bind point
                1,
                &set1Descriptor,
                0,
                0);

            int groupcount = ((node.objects.size()) / 256) + 1;
            vkCmdDispatch(cmdBuffer, groupcount, 1, 1);
            drawCountIndex++;

            /*//memory barrier
            VkBufferMemoryBarrier2 barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.pNext = NULL;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT_KHR;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            barrier.srcQueueFamilyIndex = devicePtr->getQueueFamilies().computeFamilyIndex;
            barrier.dstQueueFamilyIndex = devicePtr->getQueueFamilies().graphicsFamilyIndex;
            barrier.buffer = node.bufferData.at(currentFrame)->getBuffer();
            barrier.offset = 0;
            barrier.size = sizeof(ObjectPreprocessStride) * node.objects.size();

            VkDependencyInfo dependencyInfo = {};
            dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependencyInfo.pNext = NULL;
            dependencyInfo.dependencyFlags = VK_DEPENDENCY_DEVICE_GROUP_BIT;
            dependencyInfo.bufferMemoryBarrierCount = 1;
            dependencyInfo.pBufferMemoryBarriers = &barrier;
            dependencyInfo.imageMemoryBarrierCount = 0;
            dependencyInfo.pImageMemoryBarriers = NULL;
            dependencyInfo.memoryBarrierCount = 0;
            dependencyInfo.pMemoryBarriers = NULL;

            vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);*/
        }

        return returnFences;
    }

    void IndirectDrawBuffer::draw(const VkCommandBuffer& cmdBuffer, uint32_t currentFrame)
    {
        uint32_t drawCountIndex = 0;
        for(auto& [mesh, node] : drawCallTree)
        {
            VkDescriptorSet objDescriptorSet = descriptorsPtr->allocateDescriptorSet(pipelinePtr->getDescriptorSetLayouts().at(2), currentFrame);

            descriptorsPtr->writeUniform(
                node.bufferData.at(currentFrame)->getBuffer(),
                sizeof(ObjectPreprocessStride) * node.objects.size(),
                0, //idk how tf to get offset to work without a stride
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

            //bind vbo and send draw calls (draw calls should be computed in the performCulling() function)
            VkDeviceSize offset[1] = {0};
            vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &((*(node.objects.begin()))->mesh->getVertexBuffer().getBuffer()), offset);
            vkCmdBindIndexBuffer(cmdBuffer, ((*(node.objects.begin()))->mesh->getIndexBuffer().getBuffer()), 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexedIndirectCount(
                cmdBuffer,
                node.bufferData.at(currentFrame)->getBuffer(),
                offsetof(ObjectPreprocessStride, outputCommand),
                drawCountBuffer.at(currentFrame)->getBuffer(),
                drawCountIndex * sizeof(uint32_t),
                node.objects.size(),
                sizeof(ObjectPreprocessStride));

            drawCountIndex++;
        }
    }
}