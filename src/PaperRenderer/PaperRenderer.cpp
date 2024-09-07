#include "PaperRenderer.h"
#define VK_NO_PROTOTYPES
#include "volk.h"
#define VMA_IMPLEMENTATION
#define VMA_VULKAN_VERSION 1003000
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "vk_mem_alloc.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE 
#include "glm/glm.hpp"

#include <iostream>
#include <math.h>

namespace PaperRenderer
{
    //----------STAGING BUFFER DEFINITIONS----------//
    
    EngineStagingBuffer::EngineStagingBuffer(RenderEngine *renderer)
        :rendererPtr(renderer)
    {
        transferSemaphore = Commands::getTimelineSemaphore(rendererPtr, finalSemaphoreValue);
    }

    EngineStagingBuffer::~EngineStagingBuffer()
    {
        vkDestroySemaphore(rendererPtr->getDevice()->getDevice(), transferSemaphore, nullptr);
    }

    void EngineStagingBuffer::queueDataTransfers(const Buffer &dstBuffer, VkDeviceSize dstOffset, const std::vector<char> &data)
    {
        //push transfer to queue
        transferQueue.emplace_back(
            dstBuffer,
            dstOffset,
            data
        );
        queueSize += data.size();
    }

    void EngineStagingBuffer::submitQueuedTransfers(SynchronizationInfo syncInfo)
    {
        //check previous transfers
        uint64_t currentSemaphoreValue;
        vkGetSemaphoreCounterValue(rendererPtr->getDevice()->getDevice(), transferSemaphore, &currentSemaphoreValue);

        auto transferInfo = previousTransfers.begin();
        while(transferInfo != previousTransfers.end())
        {
            if(currentSemaphoreValue >= transferInfo->semaphoreSignalValue)
            {
                transferInfo = previousTransfers.erase(transferInfo);
            }
            else
            {
                transferInfo++;
            }
        }

        //create a list of memory fragments that can be filled
        struct MemoryFragment
        {
            VkDeviceSize offset;
            VkDeviceSize size;
        };

        std::list<MemoryFragment> fragments;
        fragments.push_back({ 0, stagingBuffer->getSize() }); //push the initial full size fragment
        VkDeviceSize availableSize = fragments.front().size;
        
        for(const TransferInfo& transfer : previousTransfers)
        {
            auto fragment = fragments.begin();
            while(fragment != fragments.end())
            {
                //split fragment if a transfer overlaps
                if(transfer.offset < fragment->size + fragment->offset)
                {
                    availableSize -= fragment->size;

                    //generate left fragment
                    MemoryFragment left = {
                        .offset = fragment->offset,
                        .size = transfer.offset - fragment->offset
                    };
                    if(left.size)
                    {
                        fragments.push_front(left);
                        availableSize += left.size;
                    }

                    //generate right fragment
                    MemoryFragment right = {
                        .offset = transfer.offset + transfer.size,
                        .size = (fragment->offset + fragment->size) - (transfer.offset + transfer.size)
                    };
                    if(right.size)
                    {
                        fragments.push_front(right);
                        availableSize += right.size;
                    }

                    //remove old
                    fragments.erase(fragment);

                    break; //a transfer should never overlap more than one fragment as that would imply a transfer is within a transfer
                }
                else
                {
                    fragment++;
                }
            }
        }

        //rebuild buffer if needed
        if(queueSize > availableSize)
        {
            //wait for timeline semaphore
            VkSemaphoreWaitInfo waitInfo = {};
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            waitInfo.pNext = NULL;
            waitInfo.flags = 0;
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &transferSemaphore;
            waitInfo.pValues = &finalSemaphoreValue;

            vkWaitSemaphores(rendererPtr->getDevice()->getDevice(), &waitInfo, UINT64_MAX);

            //rebuild buffer
            const VkDeviceSize currentBufferSize = stagingBuffer ? stagingBuffer->getSize() : 0;

            BufferInfo bufferInfo = {};
            bufferInfo.allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            bufferInfo.size = (queueSize - availableSize + currentBufferSize) * bufferOverhead;
            bufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
            stagingBuffer = std::make_unique<Buffer>(rendererPtr, bufferInfo);

            availableSize = bufferInfo.size;

            //clear any previous transfers
            previousTransfers.clear();
        }

        //modify semaphore values
        finalSemaphoreValue++;
        syncInfo.timelineSignalPairs.push_back({ transferSemaphore, VK_PIPELINE_STAGE_2_TRANSFER_BIT, finalSemaphoreValue });

        //fill in the staging buffer with queued transfers
        struct DstCopy
        {
            const QueuedTransfer& transfer;
            VkBufferCopy copyInfo;
        };
        std::vector<DstCopy> dstCopies;
        for(const QueuedTransfer& transfer : transferQueue)
        {
            //dynamic buffer write
            BufferWrite dynamicBufferWrite = {
                .offset = 0,
                .size = transfer.data.size(),
                .data = (char*)transfer.data.data()
            };

            //fill staging buffer
            auto fragment = fragments.begin();
            while(fragment != fragments.end())
            {
                dynamicBufferWrite.offset = fragment->offset;

                //split the transfer data if size is larger than fragment; remove fragment from list
                if(fragment->size < dynamicBufferWrite.size)
                {
                    BufferWrite fragmentedWrite = dynamicBufferWrite;
                    fragmentedWrite.size = fragment->size;

                    dynamicBufferWrite.offset += fragmentedWrite.size;
                    dynamicBufferWrite.size -= fragmentedWrite.size;

                    fragments.erase(fragment);

                    stagingBuffer->writeToBuffer({ fragmentedWrite });

                    previousTransfers.push_back({
                        .semaphoreSignalValue = finalSemaphoreValue,
                        .size = fragmentedWrite.size,
                        .offset = fragmentedWrite.offset
                    });
                }
                //else push the transfer and shrink the fragment
                else
                {
                    MemoryFragment newFragment = {
                        .offset = dynamicBufferWrite.size,
                        .size = fragment->size - dynamicBufferWrite.size
                    };
                    fragments.erase(fragment);
                    fragments.push_front(newFragment);
                    
                    stagingBuffer->writeToBuffer({ dynamicBufferWrite });

                    previousTransfers.push_back({
                        .semaphoreSignalValue = finalSemaphoreValue,
                        .size = dynamicBufferWrite.size,
                        .offset = dynamicBufferWrite.offset
                    });

                    break;
                }
            }
        }

        //start command buffer
        VkCommandBuffer cmdBuffer = Commands::getCommandBuffer(rendererPtr, syncInfo.queueType);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmdBuffer, &beginInfo);

        //copy to dst
        for(const auto& [transfer, copyInfo] : dstCopies)
        {
            vkCmdCopyBuffer(cmdBuffer, stagingBuffer->getBuffer(), transfer.dstBuffer.getBuffer(), 1, &copyInfo);
        }

        vkEndCommandBuffer(cmdBuffer);

        //submit
        Commands::submitToQueue(syncInfo, { cmdBuffer });

        rendererPtr->recycleCommandBuffer({ cmdBuffer, syncInfo.queueType });

        queueSize = 0;
    }

    //----------RENDER ENGINE DEFINITIONS----------//

    RenderEngine::RenderEngine(RendererCreationStruct creationInfo)
        :shadersDir(creationInfo.shadersDir),
        device(this, creationInfo.windowState.windowName),
        swapchain(this, creationInfo.windowState),
        descriptors(this),
        pipelineBuilder(this),
        rasterPreprocessPipeline(this, creationInfo.shadersDir),
        tlasInstanceBuildPipeline(this, creationInfo.shadersDir),
        stagingBuffer(this)
    {
        copyFence = Commands::getSignaledFence(this);

        rebuildModelDataBuffer();
        rebuildInstancesbuffer();

        //finish up
        vkDeviceWaitIdle(device.getDevice());
        std::cout << "Renderer initialization complete" << std::endl;
    }

    RenderEngine::~RenderEngine()
    {
        vkDeviceWaitIdle(device.getDevice());
    
        //free cmd buffers
        Commands::freeCommandBuffers(this, usedCmdBuffers);
        usedCmdBuffers.clear();

        vkDestroyFence(device.getDevice(), copyFence, nullptr);

        hostInstancesDataBuffer.reset();
        hostModelDataBuffer.reset();
        deviceInstancesDataBuffer.reset();
        deviceModelDataBuffer.reset();
    }

    void RenderEngine::rebuildModelDataBuffer()
    {
        //copy old data into temporary variables and "delete" buffers
        std::vector<char> oldData;
        VkDeviceSize newModelDataSize = 4096;
        if(hostModelDataBuffer)
        {
            hostModelDataBuffer->compact();
            newModelDataSize = hostModelDataBuffer->getDesiredLocation();
            oldData.resize(hostModelDataBuffer->getStackLocation());
            
            BufferWrite read = {};
            read.offset = 0;
            read.size = oldData.size();
            read.data = oldData.data();
            hostModelDataBuffer->getBuffer()->readFromBuffer({ read });
            hostModelDataBuffer.reset();
        }

        BufferInfo hostModelsBufferInfo = {};
        hostModelsBufferInfo.allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        hostModelsBufferInfo.size = newModelDataSize * modelsDataOverhead;
        hostModelsBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostModelDataBuffer = std::make_unique<FragmentableBuffer>(this, hostModelsBufferInfo);
        hostModelDataBuffer->setCompactionCallback([this](std::vector<CompactionResult> results){ handleModelDataCompaction(results); });

        BufferInfo deviceModelsBufferInfo = {};
        deviceModelsBufferInfo.allocationFlags = 0;
        deviceModelsBufferInfo.size = hostModelsBufferInfo.size;
        deviceModelsBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
        deviceModelDataBuffer = std::make_unique<Buffer>(this, deviceModelsBufferInfo);

        //rewrite data
        BufferWrite write = {};
        write.offset = 0;
        write.size = oldData.size();
        write.data = oldData.data();
        hostModelDataBuffer->getBuffer()->writeToBuffer({ write });
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
            uint32_t dataOffset = instance->getParentModelPtr()->getShaderDataLocation();
            
            //write data
            BufferWrite write = {};
            write.offset = offsetof(ModelInstance::ShaderModelInstance, modelDataOffset) + (sizeof(ModelInstance::ShaderModelInstance) * instance->rendererSelfIndex);
            write.size = sizeof(uint32_t);
            write.data = &dataOffset;
            hostInstancesDataBuffer->writeToBuffer({ write });
        }
    }

    void RenderEngine::rebuildInstancesbuffer()
    {
        //copy old data into temporary variables and "delete" buffers
        std::vector<char> oldData(renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance));
        if(hostInstancesDataBuffer)
        {
            BufferWrite read = {};
            read.offset = 0;
            read.size = oldData.size();
            read.data = oldData.data();
            hostInstancesDataBuffer->readFromBuffer({ read });
            hostInstancesDataBuffer.reset();
        }

        //host visible
        BufferInfo hostBufferInfo = {};
        hostBufferInfo.allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        hostBufferInfo.size = std::max((VkDeviceSize)(renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance) * instancesDataOverhead), (VkDeviceSize)sizeof(ModelInstance::ShaderModelInstance) * 128);
        hostBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostInstancesDataBuffer = std::make_unique<Buffer>(this, hostBufferInfo);

        //device local
        BufferInfo deviceBufferInfo = {};
        deviceBufferInfo.allocationFlags = 0;
        deviceBufferInfo.size = hostBufferInfo.size; //same size as host visible buffer
        deviceBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        deviceInstancesDataBuffer = std::make_unique<Buffer>(this, deviceBufferInfo);

        //rewrite data
        BufferWrite write = {};
        write.offset = 0;
        write.size = oldData.size();
        write.data = oldData.data();
        hostInstancesDataBuffer->writeToBuffer({ write });
        hostInstancesDataBuffer->readFromBuffer({ write });
        int a = 0;
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
                rebuildInstancesbuffer(); //TODO SYNCHRONIZATION
            }

            //copy initial data into host visible instances data
            ModelInstance::ShaderModelInstance shaderModelInstance = object->getShaderInstance();
            
            //write data
            BufferWrite write = {};
            write.offset = sizeof(ModelInstance::ShaderModelInstance) * object->rendererSelfIndex;
            write.size = sizeof(ModelInstance::ShaderModelInstance);
            write.data = &shaderModelInstance;
            hostInstancesDataBuffer->writeToBuffer({ write });
        }
    }

    void RenderEngine::removeObject(ModelInstance* object)
    {
        if(renderingModelInstances.size() > 1)
        {
            //new reference for last element and remove
            renderingModelInstances.at(object->rendererSelfIndex) = renderingModelInstances.back();
            renderingModelInstances.at(object->rendererSelfIndex)->rendererSelfIndex = object->rendererSelfIndex;

            //read data to move
            ModelInstance::ShaderModelInstance moveData;

            BufferWrite read = {};
            read.offset = sizeof(ModelInstance::ShaderModelInstance) * (renderingModelInstances.size() - 1);
            read.size = sizeof(ModelInstance::ShaderModelInstance);
            read.data = &moveData;
            hostInstancesDataBuffer->readFromBuffer({ read });

            //write data to move
            BufferWrite write = {};
            write.offset = sizeof(ModelInstance::ShaderModelInstance) * object->rendererSelfIndex;
            write.size = sizeof(ModelInstance::ShaderModelInstance);
            write.data = &moveData;
            hostInstancesDataBuffer->writeToBuffer({ write });

            //remove last element from instances vector (the one that was moved in the mirrored buffer)
            renderingModelInstances.pop_back();

            //check buffer size and rebuild if unnecessarily large by a factor of 2
            if(hostInstancesDataBuffer->getSize() / sizeof(ModelInstance::ShaderModelInstance) > renderingModelInstances.size() * 2 && renderingModelInstances.size() > 128)
            {
                rebuildInstancesbuffer(); //TODO THIS NEEDS TO WAIT ON BOTH FRAME FENCES
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
        const std::vector<BinarySemaphorePair> &binaryBufferCopySignalSemaphores,
        const std::vector<TimelineSemaphorePair> &timelineBufferCopySignalSemaphores
    )
    {
        //wait for fences
        std::vector<VkFence> waitFences;
        waitFences.push_back(copyFence);

        vkWaitForFences(device.getDevice(), waitFences.size(), waitFences.data(), VK_TRUE, UINT64_MAX);

        //acquire next image
        const VkSemaphore& imageAcquireSemaphore = swapchain.acquireNextImage();
        frameNumber++;

        //reset fences
        vkResetFences(device.getDevice(), waitFences.size(), waitFences.data());

        //free command buffers and reset descriptor pool
        Commands::freeCommandBuffers(this, usedCmdBuffers);
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

        VkCommandBuffer transferBuffer = Commands::getCommandBuffer(this, QueueType::TRANSFER);

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

        Commands::submitToQueue(bufferCopySync, { transferBuffer });
        recycleCommandBuffer({ transferBuffer, QueueType::TRANSFER });

        return imageAcquireSemaphore;
    }

    void RenderEngine::endFrame(const std::vector<VkSemaphore>& waitSemaphores)
    {
        //presentation
        swapchain.presentImage(waitSemaphores);

        if(bufferIndex == 0) bufferIndex = 1;
        else bufferIndex = 0;

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
