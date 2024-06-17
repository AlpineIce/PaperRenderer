#include "IndirectDraw.h"
#include "../PaperRenderer.h"

namespace PaperRenderer
{
    std::unique_ptr<PaperMemory::DeviceAllocation> CommonMeshGroup::drawDataAllocation;
    std::list<CommonMeshGroup*> CommonMeshGroup::commonMeshGroups;

    CommonMeshGroup::CommonMeshGroup(class RenderEngine* renderer, class RenderPass const* renderPass, RasterPipeline const* pipeline)
        :rendererPtr(renderer),
        renderPassPtr(renderPass),
        pipelinePtr(pipeline)
    {
        commonMeshGroups.push_back(this);
        rebuildAllocationAndBuffers(rendererPtr);
    }

    CommonMeshGroup::~CommonMeshGroup()
    {
        for(auto& [instance, meshes] : instanceMeshes)
        {
            removeInstanceMeshes(instance);
        }
    }

    void CommonMeshGroup::rebuildAllocationAndBuffers(RenderEngine* renderer)
    {
        VkDeviceSize newAllocationSize = 0;

        //rebuild buffers and get new size
        for(const auto& commonMeshGroup : commonMeshGroups)
        {
            commonMeshGroup->rebuildBuffer();
            newAllocationSize += PaperMemory::DeviceAllocation::padToMultiple(commonMeshGroup->drawDataBuffer->getMemoryRequirements().size, commonMeshGroup->drawDataBuffer->getMemoryRequirements().alignment);
        }

        //rebuild allocations
        PaperMemory::DeviceAllocationInfo allocationInfo = {};
        allocationInfo.allocationSize = newAllocationSize;
        allocationInfo.allocFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        allocationInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        drawDataAllocation = std::make_unique<PaperMemory::DeviceAllocation>(renderer->getDevice()->getDevice(), renderer->getDevice()->getGPU(), allocationInfo);

        //assign buffer memory
        for(const auto& commonMeshGroup : commonMeshGroups)
        {
            commonMeshGroup->drawDataBuffer->assignAllocation(drawDataAllocation.get());  
        }

        //redo instance data
        for(const auto& commonMeshGroup : commonMeshGroups)
        {
            //new data
            std::vector<ModelInstance*> modifiedInstances;
            for(auto& [instance, meshes] : commonMeshGroup->instanceMeshes)
            {
                instance->setRenderPassInstanceData(commonMeshGroup->renderPassPtr);
                modifiedInstances.push_back(instance);
            }

            //callback function (if not null)
            if(commonMeshGroup->rebuildCallbackFunction) commonMeshGroup->rebuildCallbackFunction(modifiedInstances);
        }
    }

    void CommonMeshGroup::rebuildBuffer()
    {
        VkDeviceSize newSize = 0;
        VkDeviceSize dynamicOffset = 0;

        //draw counts size
        drawCountsRange = sizeof(uint32_t) * meshesData.size();
        dynamicOffset += PaperMemory::DeviceAllocation::padToMultiple(drawCountsRange, rendererPtr->getDevice()->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment);
        
        //iterate meshes
        uint32_t meshIndex = 0;
        for(auto& [mesh, meshInstancesData] : meshesData)
        {
            const uint32_t instanceCount = std::max((uint32_t)(meshesData.at(mesh).instanceCount * instanceCountOverhead), (uint32_t)8); //minimum of 8 instances to make things happy
            meshInstancesData.lastRebuildInstanceCount = instanceCount;

            //draw counts
            meshInstancesData.drawCountsOffset =  meshIndex * sizeof(uint32_t);

            //draw commands
            meshInstancesData.drawCommandsOffset = dynamicOffset;
            dynamicOffset += PaperMemory::DeviceAllocation::padToMultiple(sizeof(VkDrawIndexedIndirectCommand) * instanceCount, rendererPtr->getDevice()->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment);

            //output objects
            meshInstancesData.outputObjectsOffset = dynamicOffset;
            dynamicOffset += PaperMemory::DeviceAllocation::padToMultiple(sizeof(ShaderOutputObject) * instanceCount, rendererPtr->getDevice()->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment);
            meshIndex++;
        }

        //build new buffer
        PaperMemory::BufferInfo bufferInfo = {};
        bufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        bufferInfo.size = std::max(dynamicOffset, (VkDeviceSize)64);
        bufferInfo.usageFlags = VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        drawDataBuffer = std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), bufferInfo);
    }

    void CommonMeshGroup::addInstanceMeshes(ModelInstance* instance, const std::vector<LODMesh const*>& instanceMeshesData)
    {
        addAndRemoveLock.lock();
        
        bool rebuild = false;
        
        this->instanceMeshes[instance].insert(this->instanceMeshes[instance].end(), instanceMeshesData.begin(), instanceMeshesData.end());
        for(LODMesh const* meshData : instanceMeshesData)
        {
            if(!meshesData.count(meshData))
            {
                meshesData[meshData].parentModelPtr = instance->getParentModelPtr();
                rebuild = true;
            }

            instance->renderPassSelfReferences.at(renderPassPtr).meshGroupReferences[meshData] = this;
            meshesData.at(meshData).instanceCount++;

            if(meshesData.at(meshData).instanceCount > meshesData.at(meshData).lastRebuildInstanceCount) rebuild = true;
        }
        
        if(rebuild) rebuildAllocationAndBuffers(rendererPtr);

        addAndRemoveLock.unlock();
    }

    void CommonMeshGroup::removeInstanceMeshes(ModelInstance *instance)
    {
        addAndRemoveLock.lock();
        
        for(LODMesh const* meshData : this->instanceMeshes.at(instance))
        {
            meshesData.at(meshData).instanceCount--;

            //remove if 0 instances
            if(meshesData.at(meshData).instanceCount < 1)
            {
                meshesData.erase(meshData);
            }
        }
        instanceMeshes.erase(instance);

        addAndRemoveLock.unlock();
    }

    void CommonMeshGroup::draw(const VkCommandBuffer &cmdBuffer, uint32_t currentFrame)
    {
        for(const auto& [mesh, meshData] : meshesData)
        {
            if(!meshData.parentModelPtr) continue; //null safety

            //get new descriptor set
            VkDescriptorSet objDescriptorSet = rendererPtr->getDescriptorAllocator()->allocateDescriptorSet(pipelinePtr->getDescriptorSetLayouts().at(DescriptorScopes::RASTER_OBJECT), currentFrame);
            
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
            bindingInfo.layout = pipelinePtr->getLayout();
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            DescriptorAllocator::bindSet(rendererPtr->getDevice()->getDevice(), cmdBuffer, bindingInfo);

            //bind vbo and ibo and send draw calls (draw calls should be computed in the performCulling() function)
            meshData.parentModelPtr->bindBuffers(cmdBuffer);
            vkCmdDrawIndexedIndirectCount(
                cmdBuffer,
                drawDataBuffer->getBuffer(),
                meshData.drawCommandsOffset,
                drawDataBuffer->getBuffer(),
                meshData.drawCountsOffset,
                meshData.instanceCount,
                sizeof(VkDrawIndexedIndirectCommand));
        }
    }
    void CommonMeshGroup::clearDrawCounts(const VkCommandBuffer &cmdBuffer)
    {
        //memory barrier to clear draw counts
        VkBufferMemoryBarrier2 drawCountBarrier = {};
        drawCountBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        drawCountBarrier.pNext = NULL;
        drawCountBarrier.offset = 0;
        drawCountBarrier.size = drawCountsRange;
        drawCountBarrier.buffer = drawDataBuffer->getBuffer();
        drawCountBarrier.srcAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        drawCountBarrier.srcStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        drawCountBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        drawCountBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

        VkDependencyInfo drawCountDependency = {};
        drawCountDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        drawCountDependency.pNext = NULL;
        drawCountDependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        drawCountDependency.bufferMemoryBarrierCount = 1;
        drawCountDependency.pBufferMemoryBarriers = &drawCountBarrier;

        vkCmdPipelineBarrier2(cmdBuffer, &drawCountDependency);

        //clear draw counts region
        uint32_t drawCountDefaultValue = 0;
        vkCmdFillBuffer(cmdBuffer, drawDataBuffer->getBuffer(), 0, drawCountsRange, drawCountDefaultValue);
    }
}