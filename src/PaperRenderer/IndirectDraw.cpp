#include "IndirectDraw.h"
#include "PaperRenderer.h"

#include <algorithm>
#include <iostream>

namespace PaperRenderer
{
    std::unique_ptr<DeviceAllocation> CommonMeshGroup::modelMatricesAllocation;
    std::unique_ptr<DeviceAllocation> CommonMeshGroup::drawCommandsAllocation;
    std::unique_ptr<Buffer> CommonMeshGroup::modelMatricesBuffer;
    std::unique_ptr<Buffer> CommonMeshGroup::drawCommandsBuffer;
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
            modelMatricesBuffer.reset();
            drawCommandsBuffer.reset();
            modelMatricesAllocation.reset();
            drawCommandsAllocation.reset();
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
        BufferSizeRequirements bufferSizeRequirements = {};
        for(const auto& commonMeshGroup : commonMeshGroups)
        {
            BufferSizeRequirements sizeRequirements = commonMeshGroup->getBuffersRequirements(bufferSizeRequirements);
            bufferSizeRequirements += sizeRequirements;
        }

        //rebuild buffers
        BufferInfo matricesBufferInfo = {};
        matricesBufferInfo.size = bufferSizeRequirements.matricesCount * sizeof(ShaderOutputObject);
        matricesBufferInfo.usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        modelMatricesBuffer = std::make_unique<Buffer>(renderer, matricesBufferInfo);

        BufferInfo drawCommandsBufferInfo = {};
        drawCommandsBufferInfo.size = bufferSizeRequirements.drawCommandCount * sizeof(VkDrawIndexedIndirectCommand);
        drawCommandsBufferInfo.usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT_KHR;
        drawCommandsBuffer = std::make_unique<Buffer>(renderer, drawCommandsBufferInfo);

        //rebuild allocations and assign memory
        DeviceAllocationInfo modelMatricesAllocInfo = {};
        modelMatricesAllocInfo.allocationSize = modelMatricesBuffer->getMemoryRequirements().size;
        modelMatricesAllocInfo.allocFlags = 0; 
        modelMatricesAllocInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        modelMatricesAllocation = std::make_unique<DeviceAllocation>(renderer->getDevice()->getDevice(), renderer->getDevice()->getGPU(), modelMatricesAllocInfo);
        modelMatricesBuffer->assignAllocation(modelMatricesAllocation.get());

        DeviceAllocationInfo drawCommandsAllocationInfo = {};
        drawCommandsAllocationInfo.allocationSize = drawCommandsBuffer->getMemoryRequirements().size;
        drawCommandsAllocationInfo.allocFlags = 0; 
        drawCommandsAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        drawCommandsAllocation = std::make_unique<DeviceAllocation>(renderer->getDevice()->getDevice(), renderer->getDevice()->getGPU(), drawCommandsAllocationInfo);
        drawCommandsBuffer->assignAllocation(drawCommandsAllocation.get());

        //staging buffer to add draw commands
        DeviceAllocationInfo stagingAllocationInfo = {};
        stagingAllocationInfo.allocationSize = bufferSizeRequirements.drawCommandCount * sizeof(VkDrawIndexedIndirectCommand);
        stagingAllocationInfo.allocFlags = 0; 
        stagingAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        DeviceAllocation stagingAllocation(renderer->getDevice()->getDevice(), renderer->getDevice()->getGPU(), stagingAllocationInfo);

        BufferInfo stagingBufferInfo = {};
        stagingBufferInfo.size = bufferSizeRequirements.drawCommandCount * sizeof(VkDrawIndexedIndirectCommand);
        stagingBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        Buffer stagingBuffer(renderer, stagingBufferInfo);
        stagingBuffer.assignAllocation(&stagingAllocation);

        for(const auto& commonMeshGroup : commonMeshGroups)
        {
            commonMeshGroup->setDrawCommandData(stagingBuffer);
        }

        //copy staging data
        VkBufferCopy drawCommandsRegion = {};
        drawCommandsRegion.dstOffset = 0;
        drawCommandsRegion.srcOffset = 0;
        drawCommandsRegion.size = stagingBuffer.getSize();

        SynchronizationInfo syncInfo = {};
        syncInfo.queueType = TRANSFER;
        syncInfo.fence = Commands::getUnsignaledFence(renderer);

        drawCommandsBuffer->copyFromBufferRanges(stagingBuffer, { drawCommandsRegion }, syncInfo);

        //get instances to update
        std::vector<ModelInstance*> modifiedInstances;
        for(const auto& commonMeshGroup : commonMeshGroups)
        {
            for(auto& [instance, meshes] : commonMeshGroup->instanceMeshes)
            {
                modifiedInstances.push_back(instance);
            }
        }

        //remove duplicates
        std::sort(modifiedInstances.begin(), modifiedInstances.end());
        auto uniqueIndices = std::unique(modifiedInstances.begin(), modifiedInstances.end());
        modifiedInstances.erase(uniqueIndices, modifiedInstances.end());

        //wait for transfer operation
        vkWaitForFences(renderer->getDevice()->getDevice(), 1, &syncInfo.fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(renderer->getDevice()->getDevice(), syncInfo.fence, nullptr);

        return modifiedInstances;
    }

    CommonMeshGroup::BufferSizeRequirements CommonMeshGroup::getBuffersRequirements(const BufferSizeRequirements currentSizes)
    {
        BufferSizeRequirements sizeRequirements = {};

        //draw commands count
        drawCommandCount = meshesData.size();
        sizeRequirements.drawCommandCount = drawCommandCount;

        //model matrices and offsets/indices
        uint32_t meshIndex = 0;
        for(auto& [mesh, meshInstancesData] : meshesData)
        {
            //draw commands count offset
            meshInstancesData.drawCommandIndex = currentSizes.drawCommandCount + meshIndex;

            //output objects
            const uint32_t instanceCount = std::max((uint32_t)(meshInstancesData.instanceCount * instanceCountOverhead), (uint32_t)8); //minimum of 8 instances to make things happy
            meshInstancesData.lastRebuildInstanceCount = instanceCount;
            meshInstancesData.matricesStartIndex =  currentSizes.matricesCount + sizeRequirements.matricesCount;
            sizeRequirements.matricesCount += instanceCount;

            meshIndex++;
        }

        return sizeRequirements;
    }

    void CommonMeshGroup::setDrawCommandData(const Buffer &stagingBuffer) const
    {
        for(auto& [mesh, meshInstancesData] : meshesData)
        {
            VkDrawIndexedIndirectCommand* command = ((VkDrawIndexedIndirectCommand*)(stagingBuffer.getHostDataPtr()) + meshInstancesData.drawCommandIndex);
            command->indexCount = mesh->indexCount;
            command->instanceCount = 0;
            command->firstIndex = mesh->iboOffset;
            command->vertexOffset = mesh->vboOffset;
            command->firstInstance = 0;
        }
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
            descriptorInfo.buffer = modelMatricesBuffer->getBuffer();
            descriptorInfo.offset = meshData.matricesStartIndex * sizeof(ShaderOutputObject);
            descriptorInfo.range = sizeof(ShaderOutputObject) * meshData.instanceCount;

            BuffersDescriptorWrites write = {};
            write.binding = 0;
            write.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.infos.push_back(descriptorInfo);

            DescriptorWrites descriptorWritesInfo = {};
            descriptorWritesInfo.bufferWrites = { write };
            DescriptorAllocator::writeUniforms(rendererPtr, objDescriptorSet, descriptorWritesInfo);

            //bind set
            DescriptorBind bindingInfo = {};
            bindingInfo.descriptorScope = DescriptorScopes::RASTER_OBJECT;
            bindingInfo.set = objDescriptorSet;
            bindingInfo.layout = pipeline.getLayout();
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            DescriptorAllocator::bindSet(cmdBuffer, bindingInfo);

            //bind vbo and ibo and send draw calls (draw calls should be computed in the performCulling() function)
            meshData.parentModelPtr->bindBuffers(cmdBuffer);

            //draw
            vkCmdDrawIndexedIndirect(
                cmdBuffer,
                drawCommandsBuffer->getBuffer(),
                meshData.drawCommandIndex * sizeof(uint32_t),
                1,
                sizeof(VkDrawIndexedIndirectCommand)
            );
        }
    }

    void CommonMeshGroup::clearDrawCommand(const VkCommandBuffer &cmdBuffer)
    {
        //clear instance count
        uint32_t drawCountDefaultValue = 0;
        for(const auto& [mesh, meshData] : meshesData)
        {
            uint32_t drawCommandLocation = (sizeof(VkDrawIndexedIndirectCommand) * meshData.drawCommandIndex) + offsetof(VkDrawIndexedIndirectCommand, instanceCount);
            vkCmdFillBuffer(
                cmdBuffer,
                drawCommandsBuffer->getBuffer(),
                drawCommandLocation,
                sizeof(VkDrawIndexedIndirectCommand::instanceCount),
                drawCountDefaultValue
            );
        }
    }
}