#include "IndirectDraw.h"
#include "PaperRenderer.h"

#include <algorithm>
#include <iostream>

namespace PaperRenderer
{
    std::unique_ptr<DeviceAllocation> CommonMeshGroup::drawDataAllocation;
    std::list<CommonMeshGroup*> CommonMeshGroup::commonMeshGroups;
    bool CommonMeshGroup::rebuild = false;

    CommonMeshGroup::CommonMeshGroup(RenderEngine* renderer, RenderPass const* renderPass)
        :rendererPtr(renderer),
        renderPassPtr(renderPass)
    {
        commonMeshGroups.push_back(this);
    }

    CommonMeshGroup::~CommonMeshGroup()
    {
        for(auto& [instance, meshes] : instanceMeshes)
        {
            removeInstanceMeshes(instance);
        }

        commonMeshGroups.remove(this);

        if(!commonMeshGroups.size())
        {
            drawDataBuffer.reset();
            drawDataAllocation.reset();
        }
    }

    std::vector<ModelInstance*> CommonMeshGroup::verifyBuffersSize(RenderEngine* renderer)
    {
        std::vector<ModelInstance *> returnInstances;
        if(rebuild)
        {
            returnInstances = rebuildAllocationAndBuffers(renderer);
        }
        
        rebuild = false;
        return returnInstances;
    }

    std::vector<ModelInstance*> CommonMeshGroup::rebuildAllocationAndBuffers(RenderEngine* renderer)
    {
        //rebuild buffers and get new size
        VkDeviceSize newAllocationSize = 0;
        for(const auto& commonMeshGroup : commonMeshGroups)
        {
            commonMeshGroup->rebuildBuffer();
            newAllocationSize += DeviceAllocation::padToMultiple(commonMeshGroup->drawDataBuffer->getMemoryRequirements().size, commonMeshGroup->drawDataBuffer->getMemoryRequirements().alignment);
        }

        //rebuild allocations
        DeviceAllocationInfo allocationInfo = {};
        allocationInfo.allocationSize = newAllocationSize;
        allocationInfo.allocFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT; 
        allocationInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        drawDataAllocation = std::make_unique<DeviceAllocation>(renderer->getDevice()->getDevice(), renderer->getDevice()->getGPU(), allocationInfo);

        //assign buffer memory and get instances to update
        std::vector<ModelInstance*> modifiedInstances;
        for(const auto& commonMeshGroup : commonMeshGroups)
        {
            commonMeshGroup->drawDataBuffer->assignAllocation(drawDataAllocation.get());
            commonMeshGroup->setDrawCommandData();
            for(auto& [instance, meshes] : commonMeshGroup->instanceMeshes)
            {
                modifiedInstances.push_back(instance);
            }
        }

        //remove duplicates
        std::sort(modifiedInstances.begin(), modifiedInstances.end());
        auto uniqueIndices = std::unique(modifiedInstances.begin(), modifiedInstances.end());
        modifiedInstances.erase(uniqueIndices, modifiedInstances.end());

        return modifiedInstances;
    }

    void CommonMeshGroup::rebuildBuffer()
    {
        VkDeviceSize dynamicOffset = 0;

        //draw commands size
        drawCommandCount = meshesData.size();
        dynamicOffset += DeviceAllocation::padToMultiple(drawCommandCount * sizeof(VkDrawIndexedIndirectCommand), rendererPtr->getDevice()->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment);
        
        //iterate meshes
        uint32_t meshIndex = 0;
        for(auto& [mesh, meshInstancesData] : meshesData)
        {
            //instance counts
            const uint32_t instanceCount = std::max((uint32_t)(meshesData.at(mesh).instanceCount * instanceCountOverhead), (uint32_t)8); //minimum of 8 instances to make things happy
            meshInstancesData.lastRebuildInstanceCount = instanceCount;

            //draw command offset
            meshInstancesData.drawCommandOffset = meshIndex;

            //output objects
            meshInstancesData.outputObjectsOffset = dynamicOffset;
            dynamicOffset += DeviceAllocation::padToMultiple(sizeof(ShaderOutputObject) * instanceCount, rendererPtr->getDevice()->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment);
            meshIndex++;
        }

        //build new buffer
        BufferInfo bufferInfo = {};
        bufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        bufferInfo.size = std::max(dynamicOffset, (VkDeviceSize)64);
        bufferInfo.usageFlags = VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        drawDataBuffer = std::make_unique<Buffer>(rendererPtr->getDevice()->getDevice(), bufferInfo);
    }

    void CommonMeshGroup::setDrawCommandData()
    {
        uint32_t drawCommandCount = meshesData.size();

        //draw command staging buffer
        DeviceAllocationInfo stagingAllocationInfo = {};
        stagingAllocationInfo.allocFlags = 0;
        stagingAllocationInfo.allocationSize = sizeof(VkDrawIndexedIndirectCommand) * drawCommandCount;
        stagingAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        DeviceAllocation stagingAllocation(rendererPtr->getDevice()->getDevice(), rendererPtr->getDevice()->getGPU(), stagingAllocationInfo);

        BufferInfo stagingBufferInfo = {};
        stagingBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        stagingBufferInfo.size = sizeof(VkDrawIndexedIndirectCommand) * drawCommandCount;
        stagingBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        Buffer stagingBuffer(rendererPtr->getDevice()->getDevice(), stagingBufferInfo);
        stagingBuffer.assignAllocation(&stagingAllocation);

        //fill draw command staging data
        uint32_t meshIndex = 0;
        for(auto& [mesh, MeshInstancesData] : meshesData)
        {
            VkDrawIndexedIndirectCommand drawCommand = {};
            drawCommand.indexCount = mesh->indexCount;
            drawCommand.instanceCount = 0;
            drawCommand.firstIndex = mesh->iboOffset;
            drawCommand.vertexOffset = mesh->vboOffset;
            drawCommand.firstInstance = 0;

            memcpy((char*)stagingBuffer.getHostDataPtr() + sizeof(VkDrawIndexedIndirectCommand) * meshIndex, &drawCommand, sizeof(VkDrawIndexedIndirectCommand));

            meshIndex++;
        }

        //copy staging data
        VkCommandBuffer transferCmdBuffer = Commands::getCommandBuffer(rendererPtr->getDevice()->getDevice(), TRANSFER);

        VkCommandBufferBeginInfo bufferBeginInfo = {};
        bufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bufferBeginInfo.pNext = NULL;
        bufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        bufferBeginInfo.pInheritanceInfo = NULL;

        vkBeginCommandBuffer(transferCmdBuffer, &bufferBeginInfo);

        VkBufferCopy2 bufferCopy = {};
        bufferCopy.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
        bufferCopy.pNext = NULL;
        bufferCopy.srcOffset = 0;
        bufferCopy.dstOffset = 0;
        bufferCopy.size = sizeof(VkDrawIndexedIndirectCommand) * drawCommandCount;

        VkCopyBufferInfo2 bufferCopyInfo = {};
        bufferCopyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        bufferCopyInfo.pNext = NULL;
        bufferCopyInfo.regionCount = 1;
        bufferCopyInfo.pRegions = &bufferCopy;
        bufferCopyInfo.srcBuffer = stagingBuffer.getBuffer();
        bufferCopyInfo.dstBuffer = drawDataBuffer->getBuffer();
        
        vkCmdCopyBuffer2(transferCmdBuffer, &bufferCopyInfo);
    
        vkEndCommandBuffer(transferCmdBuffer);

        SynchronizationInfo syncInfo = {};
        syncInfo.queueType = TRANSFER;
        syncInfo.fence = Commands::getUnsignaledFence(rendererPtr->getDevice()->getDevice());

        Commands::submitToQueue(rendererPtr->getDevice()->getDevice(), syncInfo, { transferCmdBuffer });

        rendererPtr->recycleCommandBuffer({ transferCmdBuffer, TRANSFER });

        vkWaitForFences(rendererPtr->getDevice()->getDevice(), 1, &syncInfo.fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(rendererPtr->getDevice()->getDevice(), syncInfo.fence, nullptr);
    }

    void CommonMeshGroup::addInstanceMeshes(ModelInstance* instance, const std::vector<LODMesh const*>& instanceMeshesData)
    {
        addAndRemoveLock.lock();
        
        for(LODMesh const* meshData : instanceMeshesData)
        {
            if(!meshesData.count(meshData))
            {
                meshesData[meshData].parentModelPtr = instance->getParentModelPtr();
                rebuild = true;
            }

            meshesData.at(meshData).instanceCount++;

            if(meshesData.at(meshData).instanceCount > meshesData.at(meshData).lastRebuildInstanceCount) rebuild = true;
        }

        this->instanceMeshes[instance].insert(this->instanceMeshes[instance].end(), instanceMeshesData.begin(), instanceMeshesData.end());
        
        addAndRemoveLock.unlock();
    }

    void CommonMeshGroup::removeInstanceMeshes(ModelInstance *instance)
    {
        addAndRemoveLock.lock();
        
        if(instanceMeshes.count(instance))
        {
            for(LODMesh const* meshData : instanceMeshes.at(instance))
            {
                meshesData.at(meshData).instanceCount--;

                //remove if 0 instances
                if(meshesData.at(meshData).instanceCount < 1)
                {
                    meshesData.erase(meshData);
                }
            }
            instanceMeshes.erase(instance);
        }
        
        addAndRemoveLock.unlock();
    }

    void CommonMeshGroup::draw(const VkCommandBuffer &cmdBuffer, const RasterPipeline& pipeline)
    {
        for(const auto& [mesh, meshData] : meshesData)
        {
            if(!meshData.parentModelPtr) continue; //null safety

            //get new descriptor set
            VkDescriptorSet objDescriptorSet = rendererPtr->getDescriptorAllocator()->allocateDescriptorSet(pipeline.getDescriptorSetLayouts().at(DescriptorScopes::RASTER_OBJECT));
            
            //write uniforms
            VkDescriptorBufferInfo descriptorInfo = {};
            descriptorInfo.buffer = drawDataBuffer->getBuffer();
            descriptorInfo.offset = meshData.outputObjectsOffset;
            descriptorInfo.range = sizeof(ShaderOutputObject) * meshData.instanceCount;
            BuffersDescriptorWrites write = {};
            write.binding = 0;
            write.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.infos.push_back(descriptorInfo);

            DescriptorWrites descriptorWritesInfo = {};
            descriptorWritesInfo.bufferWrites = { write };
            DescriptorAllocator::writeUniforms(rendererPtr->getDevice()->getDevice(), objDescriptorSet, descriptorWritesInfo);

            //bind set
            DescriptorBind bindingInfo = {};
            bindingInfo.descriptorScope = DescriptorScopes::RASTER_OBJECT;
            bindingInfo.set = objDescriptorSet;
            bindingInfo.layout = pipeline.getLayout();
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            DescriptorAllocator::bindSet(rendererPtr->getDevice()->getDevice(), cmdBuffer, bindingInfo);

            //bind vbo and ibo and send draw calls (draw calls should be computed in the performCulling() function)
            meshData.parentModelPtr->bindBuffers(cmdBuffer);
            vkCmdDrawIndexedIndirect(
                cmdBuffer,
                drawDataBuffer->getBuffer(),
                meshData.drawCommandOffset * sizeof(VkDrawIndexedIndirectCommand),
                1,
                sizeof(VkDrawIndexedIndirectCommand)
            );
        }
    }
    void CommonMeshGroup::clearDrawCommand(const VkCommandBuffer &cmdBuffer)
    {
        //clear instance count
        uint32_t drawCountDefaultValue = 1;
        for(uint32_t i = 0; i < drawCommandCount; i++)
        {
            
            vkCmdFillBuffer(
                cmdBuffer,
                drawDataBuffer->getBuffer(),
                offsetof(VkDrawIndexedIndirectCommand, instanceCount) + (sizeof(VkDrawIndexedIndirectCommand) * i),
                sizeof(VkDrawIndexedIndirectCommand::instanceCount),
                drawCountDefaultValue
            );
        }
    }
}