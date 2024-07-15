#include "PaperRenderer.h"

#include <iostream>
#include <math.h>

namespace PaperRenderer
{
    RenderEngine::RenderEngine(RendererCreationStruct creationInfo)
        :shadersDir(creationInfo.shadersDir),
        device(creationInfo.windowState.windowName),
        swapchain(&device, creationInfo.windowState),
        descriptors(&device),
        pipelineBuilder(this),
        rasterPreprocessPipeline(this, creationInfo.shadersDir),
        tlasInstanceBuildPipeline(this, creationInfo.shadersDir)
    {
        //synchronization and cmd buffers
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
        }
        usedCmdBuffers.resize(PaperMemory::Commands::getFrameCount());

        copyFence = PaperMemory::Commands::getSignaledFence(device.getDevice());

        rebuildBuffersAndAllocations();

        //finish up
        vkDeviceWaitIdle(device.getDevice());
        std::cout << "Renderer initialization complete" << std::endl;
    }

    RenderEngine::~RenderEngine()
    {
        vkDeviceWaitIdle(device.getDevice());
        
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            //free cmd buffers
            PaperMemory::Commands::freeCommandBuffers(device.getDevice(), usedCmdBuffers.at(i));
            usedCmdBuffers.at(i).clear();
        }

        vkDestroyFence(device.getDevice(), copyFence, nullptr);
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
        hostAllocationSize = PaperMemory::DeviceAllocation::padToMultiple(hostAllocationSize, hostInstancesDataBuffer->getMemoryRequirements().alignment); //pad buffer to allocation requirement
        hostAllocationSize += hostModelDataBuffer->getBuffer()->getMemoryRequirements().size;

        VkDeviceSize deviceAllocationSize = 0;
        deviceAllocationSize += std::max(deviceInstancesDataBuffer->getMemoryRequirements().size, (VkDeviceSize)sizeof(ModelInstance::ShaderModelInstance) * 128);
        deviceAllocationSize = PaperMemory::DeviceAllocation::padToMultiple(deviceAllocationSize, deviceInstancesDataBuffer->getMemoryRequirements().alignment); //pad buffer to allocation requirement
        deviceAllocationSize += deviceModelDataBuffer->getMemoryRequirements().size;

        //create new allocations
        PaperMemory::DeviceAllocationInfo hostAllocationInfo = {};
        hostAllocationInfo.allocationSize = hostAllocationSize;
        hostAllocationInfo.allocFlags = 0;
        hostAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        hostDataAllocation = std::make_unique<PaperMemory::DeviceAllocation>(device.getDevice(), device.getGPU(), hostAllocationInfo);

        PaperMemory::DeviceAllocationInfo deviceAllocationInfo = {};
        deviceAllocationInfo.allocationSize = deviceAllocationSize;
        deviceAllocationInfo.allocFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        deviceAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        deviceDataAllocation = std::make_unique<PaperMemory::DeviceAllocation>(device.getDevice(), device.getGPU(), deviceAllocationInfo);

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
        PaperMemory::BufferInfo hostModelsBufferInfo = {};
        hostModelsBufferInfo.size = rebuildSize * modelsDataOverhead;
        hostModelsBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostModelsBufferInfo.queueFamiliesIndices = device.getQueueFamiliesIndices();
        hostModelDataBuffer = std::make_unique<PaperMemory::FragmentableBuffer>(device.getDevice(), hostModelsBufferInfo);
        hostModelDataBuffer->setCompactionCallback([this](std::vector<PaperMemory::CompactionResult> results){ handleModelDataCompaction(results); });

        PaperMemory::BufferInfo deviceModelsBufferInfo = {};
        deviceModelsBufferInfo.size = hostModelsBufferInfo.size;
        deviceModelsBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
        deviceModelsBufferInfo.queueFamiliesIndices = device.getQueueFamiliesIndices();
        deviceModelDataBuffer = std::make_unique<PaperMemory::Buffer>(device.getDevice(), deviceModelsBufferInfo);
    }

    void RenderEngine::handleModelDataCompaction(std::vector<PaperMemory::CompactionResult> results)
    {
        std::cout << "Model data compaction event" << std::endl;
    }

    void RenderEngine::rebuildInstancesbuffers()
    {
        //host visible
        PaperMemory::BufferInfo hostBufferInfo = {};
        hostBufferInfo.size = std::max((VkDeviceSize)(renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance) * instancesDataOverhead), (VkDeviceSize)sizeof(ModelInstance::ShaderModelInstance) * 128);
        hostBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostBufferInfo.queueFamiliesIndices = device.getQueueFamiliesIndices();
        hostInstancesDataBuffer = std::make_unique<PaperMemory::Buffer>(device.getDevice(), hostBufferInfo);

        //device local
        PaperMemory::BufferInfo deviceBufferInfo = {};
        deviceBufferInfo.size = hostBufferInfo.size; //same size as host visible buffer
        deviceBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        deviceBufferInfo.queueFamiliesIndices = device.getQueueFamiliesIndices();
        deviceInstancesDataBuffer = std::make_unique<PaperMemory::Buffer>(device.getDevice(), deviceBufferInfo);
    }

    void RenderEngine::addModelData(Model* model)
    {
        model->selfIndex = renderingModels.size();
        renderingModels.push_back(model);
        

        //copy initial data into host visible instances data
        std::vector<char> shaderData = model->getShaderData();

        hostModelDataBuffer->newWrite(shaderData.data(), shaderData.size(), 0, &model->shaderDataLocation);
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

    int RenderEngine::beginFrame(const std::vector<VkFence>& waitFences, VkSemaphore imageAquireSignalSemaphore, const std::vector<PaperMemory::SemaphorePair>& bufferCopySignalSemaphores)
    {
        //wait for fences
        vkWaitForFences(device.getDevice(), waitFences.size(), waitFences.data(), VK_TRUE, UINT64_MAX);

        //get available image
        uint32_t imageReturnIndex;
        VkResult imageAquireResult = vkAcquireNextImageKHR(device.getDevice(),
            swapchain.getSwapchain(),
            UINT32_MAX,
            imageAquireSignalSemaphore,
            VK_NULL_HANDLE, &imageReturnIndex);

        int returnResult = 0;
        if(imageAquireResult == VK_ERROR_OUT_OF_DATE_KHR || imageAquireResult == VK_SUBOPTIMAL_KHR) //i have no idea why it deadlocks on anything but frame 0
        {
            if(currentImage == 0)
            {
                swapchain.recreate();
                returnResult = 1;

                beginFrame(waitFences, imageAquireSignalSemaphore, bufferCopySignalSemaphores);
            }
            else
            {
                currentImage = 0;
                return 2;
            }
        }
        else
        {
            //reset fences
            std::vector<VkFence> allWaitFences = waitFences;
            allWaitFences.push_back(copyFence);
            allWaitFences.insert(allWaitFences.begin(), preprocessFences.begin(), preprocessFences.end());
            allWaitFences.insert(allWaitFences.begin(), accelerationStructureFences.begin(), accelerationStructureFences.end());

            preprocessFences.clear();
            accelerationStructureFences.clear();

            vkWaitForFences(device.getDevice(), allWaitFences.size(), allWaitFences.data(), VK_TRUE, UINT64_MAX);
            vkResetFences(device.getDevice(), allWaitFences.size(), allWaitFences.data());

            //free command buffers and reset descriptor pool
            PaperMemory::Commands::freeCommandBuffers(device.getDevice(), usedCmdBuffers.at(currentImage));
            usedCmdBuffers.at(currentImage).clear();
            descriptors.refreshPools(currentImage);

            //copy instances and model data (done in one submission)
            VkBufferCopy instancesRegion;
            instancesRegion.srcOffset = 0;
            instancesRegion.dstOffset = 0;
            instancesRegion.size = renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance);

            VkBufferCopy modelsRegion;
            modelsRegion.srcOffset = 0;
            modelsRegion.dstOffset = 0;
            modelsRegion.size = hostModelDataBuffer->getStackLocation();

            VkCommandBuffer transferBuffer = PaperMemory::Commands::getCommandBuffer(device.getDevice(), PaperMemory::QueueType::TRANSFER);

            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.pNext = NULL;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(transferBuffer, &beginInfo);
            vkCmdCopyBuffer(transferBuffer, hostInstancesDataBuffer->getBuffer(), deviceInstancesDataBuffer->getBuffer(), 1, &instancesRegion);
            vkCmdCopyBuffer(transferBuffer, hostModelDataBuffer->getBuffer()->getBuffer(), deviceModelDataBuffer->getBuffer(), 1, &modelsRegion);
            vkEndCommandBuffer(transferBuffer);

            PaperMemory::SynchronizationInfo bufferCopySync = {};
            bufferCopySync.queueType = PaperMemory::QueueType::TRANSFER;
            bufferCopySync.waitPairs = {};
            bufferCopySync.signalPairs = bufferCopySignalSemaphores;
            bufferCopySync.fence = copyFence;

            PaperMemory::Commands::submitToQueue(device.getDevice(), bufferCopySync, { transferBuffer });
            recycleCommandBuffer({ transferBuffer, PaperMemory::QueueType::TRANSFER });
        }

        return returnResult;
    }

    int RenderEngine::endFrame(const std::vector<VkSemaphore>& waitSemaphores)
    {
        //presentation
        //VkResult returnResult;
        VkPresentInfoKHR presentSubmitInfo = {};
        presentSubmitInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentSubmitInfo.pNext = NULL;
        presentSubmitInfo.waitSemaphoreCount = waitSemaphores.size();
        presentSubmitInfo.pWaitSemaphores = waitSemaphores.data();
        presentSubmitInfo.swapchainCount = 1;
        presentSubmitInfo.pSwapchains = &swapchain.getSwapchain();
        presentSubmitInfo.pImageIndices = &currentImage;
        presentSubmitInfo.pResults = NULL;//&returnResult;

        //too lazy to properly fix this, it probably barely affects performance anyways
        device.getQueues().at(PaperMemory::QueueType::PRESENT).queues.at(0)->threadLock.lock();
        VkResult presentResult = vkQueuePresentKHR(device.getQueues().at(PaperMemory::QueueType::PRESENT).queues.at(0)->queue, &presentSubmitInfo);
        device.getQueues().at(PaperMemory::QueueType::PRESENT).queues.at(0)->threadLock.unlock();

        int returnNumber = 0;
        /*if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) 
        {
            swapchain.recreate();
            returnNumber = 1;
        }*/

        //increment frame counter
        const uint32_t maxFrameCount = PaperMemory::Commands::getFrameCount();
        if(currentImage == maxFrameCount - 1)
        {
            currentImage = 0;
        }
        else
        {
            currentImage++;
        }

        glfwPollEvents();

        return returnNumber;
    }

    void RenderEngine::recycleCommandBuffer(PaperMemory::CommandBuffer& commandBuffer)
    {
        if(commandBuffer.buffer)
        {
            usedCmdBuffers.at(currentImage).push_back(commandBuffer);
            commandBuffer.buffer = VK_NULL_HANDLE;
        }
    }

    void RenderEngine::recycleCommandBuffer(PaperMemory::CommandBuffer&& commandBuffer)
    {
        if(commandBuffer.buffer)
        {
            usedCmdBuffers.at(currentImage).push_back(commandBuffer);
        }
    }
}
