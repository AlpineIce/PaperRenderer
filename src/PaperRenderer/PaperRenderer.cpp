#include "PaperRenderer.h"

#include <iostream>
#include <math.h>

namespace PaperRenderer
{
    RenderEngine::RenderEngine(RendererCreationStruct creationInfo)
        :shadersDir(creationInfo.shadersDir),
        device(creationInfo.windowState.windowName),
        swapchain(this, creationInfo.windowState),
        descriptors(&device),
        pipelineBuilder(this),
        rasterPreprocessPipeline(this, creationInfo.shadersDir),
        tlasInstanceBuildPipeline(this, creationInfo.shadersDir)
    {
        copyFence = Commands::getSignaledFence(device.getDevice());

        rebuildBuffersAndAllocations();

        //finish up
        vkDeviceWaitIdle(device.getDevice());
        std::cout << "Renderer initialization complete" << std::endl;
    }

    RenderEngine::~RenderEngine()
    {
        vkDeviceWaitIdle(device.getDevice());
    
        //free cmd buffers
        Commands::freeCommandBuffers(device.getDevice(), usedCmdBuffers);
        usedCmdBuffers.clear();

        vkDestroyFence(device.getDevice(), copyFence, nullptr);

        hostDataAllocation.reset();
        deviceDataAllocation.reset();
        hostInstancesDataBuffer.reset();
        hostModelDataBuffer.reset();
        deviceInstancesDataBuffer.reset();
        deviceModelDataBuffer.reset();
    }

    void RenderEngine::rebuildBuffersAndAllocations()
    {
        //copy old data into temporary variables and "delete" buffers
        std::vector<char> oldModelInstancesData(renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance));
        if(hostInstancesDataBuffer)
        {
            memcpy(oldModelInstancesData.data(), hostInstancesDataBuffer->getHostDataPtr(), oldModelInstancesData.size());
            hostInstancesDataBuffer.reset();
        }
        
        VkDeviceSize oldModelsDataSize = 4096;
        std::vector<char> oldModelsData;
        if(hostModelDataBuffer)
        {
            hostModelDataBuffer->compact();
            oldModelsDataSize = hostModelDataBuffer->getStackLocation();

            oldModelsData.resize(oldModelsDataSize);
            memcpy(oldModelsData.data(), hostModelDataBuffer->getBuffer()->getHostDataPtr(), oldModelsData.size());
            hostModelDataBuffer.reset();
        }
        
        //delete old allocations (technically not needed)
        hostDataAllocation.reset();
        deviceDataAllocation.reset();

        //rebuild buffers
        rebuildInstancesbuffers();
        rebuildModelDataBuffers(oldModelsDataSize);

        //get allocation size requirements
        VkDeviceSize hostAllocationSize = 0;
        hostAllocationSize += std::max(hostInstancesDataBuffer->getMemoryRequirements().size, (VkDeviceSize)sizeof(ModelInstance::ShaderModelInstance) * 128);
        hostAllocationSize = DeviceAllocation::padToMultiple(hostAllocationSize, hostInstancesDataBuffer->getMemoryRequirements().alignment); //pad buffer to allocation requirement
        hostAllocationSize += hostModelDataBuffer->getBuffer()->getMemoryRequirements().size;

        VkDeviceSize deviceAllocationSize = 0;
        deviceAllocationSize += std::max(deviceInstancesDataBuffer->getMemoryRequirements().size, (VkDeviceSize)sizeof(ModelInstance::ShaderModelInstance) * 128);
        deviceAllocationSize = DeviceAllocation::padToMultiple(deviceAllocationSize, deviceInstancesDataBuffer->getMemoryRequirements().alignment); //pad buffer to allocation requirement
        deviceAllocationSize += deviceModelDataBuffer->getMemoryRequirements().size;

        //create new allocations
        DeviceAllocationInfo hostAllocationInfo = {};
        hostAllocationInfo.allocationSize = hostAllocationSize;
        hostAllocationInfo.allocFlags = 0;
        hostAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        hostDataAllocation = std::make_unique<DeviceAllocation>(device.getDevice(), device.getGPU(), hostAllocationInfo);

        DeviceAllocationInfo deviceAllocationInfo = {};
        deviceAllocationInfo.allocationSize = deviceAllocationSize;
        deviceAllocationInfo.allocFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        deviceAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        deviceDataAllocation = std::make_unique<DeviceAllocation>(device.getDevice(), device.getGPU(), deviceAllocationInfo);

        //assign allocations and copy old data back in
        hostInstancesDataBuffer->assignAllocation(hostDataAllocation.get());
        memcpy(hostInstancesDataBuffer->getHostDataPtr(), oldModelInstancesData.data(), oldModelInstancesData.size());
        hostModelDataBuffer->assignAllocation(hostDataAllocation.get());
        hostModelDataBuffer->newWrite(oldModelsData.data(), oldModelsData.size(), 0, NULL); //location isn't needed

        deviceInstancesDataBuffer->assignAllocation(deviceDataAllocation.get());
        deviceModelDataBuffer->assignAllocation(deviceDataAllocation.get());
    }

    void RenderEngine::rebuildModelDataBuffers(VkDeviceSize rebuildSize)
    {
        BufferInfo hostModelsBufferInfo = {};
        hostModelsBufferInfo.size = rebuildSize * modelsDataOverhead;
        hostModelsBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostModelsBufferInfo.queueFamiliesIndices = device.getQueueFamiliesIndices();
        hostModelDataBuffer = std::make_unique<FragmentableBuffer>(device.getDevice(), hostModelsBufferInfo);
        hostModelDataBuffer->setCompactionCallback([this](std::vector<CompactionResult> results){ handleModelDataCompaction(results); });

        BufferInfo deviceModelsBufferInfo = {};
        deviceModelsBufferInfo.size = hostModelsBufferInfo.size;
        deviceModelsBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
        deviceModelsBufferInfo.queueFamiliesIndices = device.getQueueFamiliesIndices();
        deviceModelDataBuffer = std::make_unique<Buffer>(device.getDevice(), deviceModelsBufferInfo);
    }

    void RenderEngine::handleModelDataCompaction(std::vector<CompactionResult> results) //UNTESTED FUNCTION
    {
        //fix model data first
        for(const CompactionResult compactionResult : results)
        {
            
            for(Model* model : renderingModels)
            {
                if(model->shaderDataLocation > compactionResult.location)
                {
                    model->shaderDataLocation -= compactionResult.shiftSize;
                }
            }
        }

        //then fix instances data
        for(ModelInstance* instance : renderingModelInstances)
        {
            ModelInstance::ShaderModelInstance& shaderModelInstance = *((ModelInstance::ShaderModelInstance*)hostInstancesDataBuffer->getHostDataPtr() + instance->rendererSelfIndex);
            shaderModelInstance.modelDataOffset = instance->getParentModelPtr()->getShaderDataLocation();
        }
    }

    void RenderEngine::rebuildInstancesbuffers()
    {
        //host visible
        BufferInfo hostBufferInfo = {};
        hostBufferInfo.size = std::max((VkDeviceSize)(renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance) * instancesDataOverhead), (VkDeviceSize)sizeof(ModelInstance::ShaderModelInstance) * 128);
        hostBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostBufferInfo.queueFamiliesIndices = device.getQueueFamiliesIndices();
        hostInstancesDataBuffer = std::make_unique<Buffer>(device.getDevice(), hostBufferInfo);

        //device local
        BufferInfo deviceBufferInfo = {};
        deviceBufferInfo.size = hostBufferInfo.size; //same size as host visible buffer
        deviceBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        deviceBufferInfo.queueFamiliesIndices = device.getQueueFamiliesIndices();
        deviceInstancesDataBuffer = std::make_unique<Buffer>(device.getDevice(), deviceBufferInfo);
    }

    void RenderEngine::addModelData(Model* model)
    {
        model->selfIndex = renderingModels.size();
        renderingModels.push_back(model);
        

        //copy initial data into host visible instances data
        std::vector<char> shaderData = model->getShaderData();

        hostModelDataBuffer->newWrite(shaderData.data(), shaderData.size(), 8, &model->shaderDataLocation);
    }

    void RenderEngine::removeModelData(Model* model)
    {
        if(renderingModels.size() > 1)
        {
            //new reference for last element and remove
            renderingModels.at(model->selfIndex) = renderingModels.back();
            renderingModels.at(model->selfIndex)->selfIndex = model->selfIndex;
            renderingModels.pop_back();

            //(no need to copy data because fragmentable buffer)
        }
        else
        {
            renderingModels.clear();
        }

        //remove from buffer
        hostModelDataBuffer->removeFromRange(model->shaderDataLocation, model->getShaderData().size());
        
        model->selfIndex = UINT64_MAX;

        //TODO UPDATE DEPENDENCIES
    }

    void RenderEngine::addObject(ModelInstance* object)
    {
        if(object->getParentModelPtr() != NULL)
        {
            //self reference
            object->rendererSelfIndex = renderingModelInstances.size();
            renderingModelInstances.push_back(object);

            //check buffer size and rebuild if too small
            if(hostInstancesDataBuffer->getSize() / sizeof(ModelInstance::ShaderModelInstance) < renderingModelInstances.size() && renderingModelInstances.size() > 128)
            {
                rebuildBuffersAndAllocations(); //TODO SYNCHRONIZATION
            }

            //copy initial data into host visible instances data
            ModelInstance::ShaderModelInstance shaderModelInstance = object->getShaderInstance();
            memcpy((ModelInstance::ShaderModelInstance*)hostInstancesDataBuffer->getHostDataPtr() + object->rendererSelfIndex, &shaderModelInstance, sizeof(ModelInstance::ShaderModelInstance));
        }
    }

    void RenderEngine::removeObject(ModelInstance* object)
    {
        if(renderingModelInstances.size() > 1)
        {
            //new reference for last element and remove
            renderingModelInstances.at(object->rendererSelfIndex) = renderingModelInstances.back();
            renderingModelInstances.at(object->rendererSelfIndex)->rendererSelfIndex = object->rendererSelfIndex;

            //re-copy data
            memcpy(
                (ModelInstance::ShaderModelInstance*)hostInstancesDataBuffer->getHostDataPtr() + object->rendererSelfIndex, 
                (ModelInstance::ShaderModelInstance*)hostInstancesDataBuffer->getHostDataPtr() + renderingModelInstances.size() - 1,
                sizeof(ModelInstance::ShaderModelInstance));

            renderingModelInstances.pop_back();

            //check buffer size and rebuild if unnecessarily large by a factor of 2
            if(hostInstancesDataBuffer->getSize() / sizeof(ModelInstance::ShaderModelInstance) > renderingModelInstances.size() * 2 && renderingModelInstances.size() > 128)
            {
                rebuildBuffersAndAllocations(); //TODO THIS NEEDS TO WAIT ON BOTH FRAME FENCES
            }
        }
        else
        {
            renderingModelInstances.clear();
        }

        //TODO UPDATE DEPENDENCIES
        
        object->rendererSelfIndex = UINT32_MAX;
    }

    const VkSemaphore& RenderEngine::beginFrame(
        const std::vector<VkFence> &waitFences,
        const std::vector<BinarySemaphorePair> &binaryBufferCopySignalSemaphores,
        const std::vector<TimelineSemaphorePair> &timelineBufferCopySignalSemaphores
    )
    {
        //wait for fences
        std::vector<VkFence> allWaitFences = waitFences;
        allWaitFences.push_back(copyFence);

        vkWaitForFences(device.getDevice(), allWaitFences.size(), allWaitFences.data(), VK_TRUE, UINT64_MAX);

        //acquire next image
        const VkSemaphore& imageAcquireSemaphore = swapchain.acquireNextImage(currentImage);
        frameNumber++;

        //reset fences
        vkResetFences(device.getDevice(), allWaitFences.size(), allWaitFences.data());

        //free command buffers and reset descriptor pool
        Commands::freeCommandBuffers(device.getDevice(), usedCmdBuffers);
        usedCmdBuffers.clear();
        descriptors.refreshPools();

        //copy instances and model data (done in one submission)
        VkBufferCopy instancesRegion;
        instancesRegion.srcOffset = 0;
        instancesRegion.dstOffset = 0;
        instancesRegion.size = renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance);

        VkBufferCopy modelsRegion;
        modelsRegion.srcOffset = 0;
        modelsRegion.dstOffset = 0;
        modelsRegion.size = hostModelDataBuffer->getStackLocation();

        VkCommandBuffer transferBuffer = Commands::getCommandBuffer(device.getDevice(), QueueType::TRANSFER);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(transferBuffer, &beginInfo);
        vkCmdCopyBuffer(transferBuffer, hostInstancesDataBuffer->getBuffer(), deviceInstancesDataBuffer->getBuffer(), 1, &instancesRegion);
        vkCmdCopyBuffer(transferBuffer, hostModelDataBuffer->getBuffer()->getBuffer(), deviceModelDataBuffer->getBuffer(), 1, &modelsRegion);
        vkEndCommandBuffer(transferBuffer);

        SynchronizationInfo bufferCopySync = {};
        bufferCopySync.queueType = QueueType::TRANSFER;
        bufferCopySync.binaryWaitPairs = {};
        bufferCopySync.binarySignalPairs = binaryBufferCopySignalSemaphores;
        bufferCopySync.timelineSignalPairs = timelineBufferCopySignalSemaphores;
        bufferCopySync.fence = copyFence;

        Commands::submitToQueue(device.getDevice(), bufferCopySync, { transferBuffer });
        recycleCommandBuffer({ transferBuffer, QueueType::TRANSFER });

        return imageAcquireSemaphore;
    }

    void RenderEngine::endFrame(const std::vector<VkSemaphore>& waitSemaphores)
    {
        //presentation
        swapchain.presentImage(waitSemaphores);

        glfwPollEvents();
    }

    void RenderEngine::recycleCommandBuffer(CommandBuffer& commandBuffer)
    {
        if(commandBuffer.buffer)
        {
            usedCmdBuffers.push_back(commandBuffer);
            commandBuffer.buffer = VK_NULL_HANDLE;
        }
    }

    void RenderEngine::recycleCommandBuffer(CommandBuffer&& commandBuffer)
    {
        if(commandBuffer.buffer)
        {
            usedCmdBuffers.push_back(commandBuffer);
        }
    }
}
