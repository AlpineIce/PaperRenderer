#include "PaperRenderer.h"
#define VK_NO_PROTOTYPES
#include "volk.h"
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "vk_mem_alloc.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE 
#include "glm.hpp"

#include <math.h>

namespace PaperRenderer
{
    //----------STAGING BUFFER DEFINITIONS----------//

    RendererStagingBuffer::RendererStagingBuffer(RenderEngine& renderer)
        :renderer(renderer)
    {
        transferSemaphore = renderer.getDevice().getCommands().getTimelineSemaphore(finalSemaphoreValue);
    }

    RendererStagingBuffer::~RendererStagingBuffer()
    {
        stagingBuffer.reset();
        vkDestroySemaphore(renderer.getDevice().getDevice(), transferSemaphore, nullptr);
    }

    void RendererStagingBuffer::queueDataTransfers(const Buffer& dstBuffer, VkDeviceSize dstOffset, const std::vector<char> &data)
    {
        //lock mutex
        std::lock_guard guard(stagingBufferMutex);

        //push transfer to queue
        transferQueues[&dstBuffer].emplace_front(
            dstOffset,
            data
        );
        queueSize += data.size();
    }

    std::vector<RendererStagingBuffer::DstCopy> RendererStagingBuffer::getDataTransfers()
    {
        //wait for transfer to complete
        VkSemaphoreWaitInfo waitInfo = {};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.pNext = NULL;
        waitInfo.flags = 0;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &transferSemaphore;
        waitInfo.pValues = &finalSemaphoreValue;

        vkWaitSemaphores(renderer.getDevice().getDevice(), &waitInfo, UINT64_MAX);

        //rebuild buffer if needed
        VkDeviceSize availableSize = stagingBuffer ? stagingBuffer->getSize() : 0;
        if(queueSize > availableSize)
        {
            BufferInfo bufferInfo = {};
            bufferInfo.allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            bufferInfo.size = queueSize * bufferOverhead;
            bufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
            stagingBuffer = std::make_unique<Buffer>(renderer, bufferInfo);

            availableSize = bufferInfo.size;
        }

        //modify semaphore values
        finalSemaphoreValue++;

        //fill in the staging buffer with queued transfers
        VkDeviceSize dynamicSrcOffset = 0;
        std::vector<DstCopy> dstCopies;
        for(auto& [buffer, transfers] : transferQueues)
        {
            for(const QueuedTransfer& transfer : transfers)
            {
                //buffer write
                BufferWrite bufferWrite = {
                    .offset = dynamicSrcOffset,
                    .size = transfer.data.size(),
                    .data = (char*)transfer.data.data()
                };

                //fill staging buffer           
                stagingBuffer->writeToBuffer({ bufferWrite });

                //push VkBufferCopy
                VkBufferCopy bufferCopyInfo = {
                    .srcOffset = bufferWrite.offset,
                    .dstOffset = transfer.dstOffset,
                    .size = bufferWrite.size
                };

                dstCopies.emplace_back(
                    *buffer,
                    bufferCopyInfo
                );

                dynamicSrcOffset += bufferWrite.size;
            }
            transfers.clear();
        }
        queueSize = 0;

        return dstCopies;
    }

    void RendererStagingBuffer::submitQueuedTransfers(SynchronizationInfo syncInfo)
    {
        //lock mutex
        std::lock_guard guard(stagingBufferMutex);

        //start command buffer
        VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(syncInfo.queueType);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmdBuffer, &beginInfo);

        //copy to dst
        for(const auto& copy : getDataTransfers())
        {
            vkCmdCopyBuffer(cmdBuffer, stagingBuffer->getBuffer(), copy.dstBuffer.getBuffer(), 1, &copy.copyInfo);
        }

        vkEndCommandBuffer(cmdBuffer);

        renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

        //submit
        syncInfo.timelineSignalPairs.push_back({ transferSemaphore, VK_PIPELINE_STAGE_2_TRANSFER_BIT, finalSemaphoreValue });
        renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer });
    }

    void RendererStagingBuffer::submitQueuedTransfers(VkCommandBuffer cmdBuffer)
    {
        //lock mutex
        std::lock_guard guard(stagingBufferMutex);
        
        //copy to dst
        for(const auto& copy : getDataTransfers())
        {
            vkCmdCopyBuffer(cmdBuffer, stagingBuffer->getBuffer(), copy.dstBuffer.getBuffer(), 1, &copy.copyInfo);
        }
    }

    //----------RENDER ENGINE DEFINITIONS----------//

    RenderEngine::RenderEngine(const RendererCreationStruct& creationInfo)
        :device(*this, creationInfo.windowState.windowName),
        swapchain(*this, creationInfo.windowState),
        descriptors(*this),
        pipelineBuilder(*this),
        rasterPreprocessPipeline(*this, creationInfo.rasterPreprocessSpirv),
        tlasInstanceBuildPipeline(*this, creationInfo.rtPreprocessSpirv),
        asBuilder(*this),
        stagingBuffer({ *this, *this })
    {
        rebuildModelDataBuffer();
        rebuildInstancesbuffer();

        //finish up
        vkDeviceWaitIdle(device.getDevice());
    }

    RenderEngine::~RenderEngine()
    {
        vkDeviceWaitIdle(device.getDevice());
    
        //free cmd buffers
        modelDataBuffer.reset();
        instancesDataBuffer.reset();
    }

    void RenderEngine::rebuildModelDataBuffer()
    {
        //new buffer to replace old
        VkDeviceSize newModelDataSize = 4096;
        VkDeviceSize newWriteSize = 0;
        if(modelDataBuffer)
        {
            modelDataBuffer->compact();
            newModelDataSize = modelDataBuffer->getDesiredLocation();
            newWriteSize = modelDataBuffer->getStackLocation();
        }

        BufferInfo modelsBufferInfo = {};
        modelsBufferInfo.allocationFlags = 0;
        modelsBufferInfo.size = newModelDataSize * modelsDataOverhead;
        modelsBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
        std::unique_ptr<FragmentableBuffer> newBuffer = std::make_unique<FragmentableBuffer>(*this, modelsBufferInfo);
        newBuffer->setCompactionCallback([this](std::vector<CompactionResult> results){ handleModelDataCompaction(results); });

        //copy old data into new if old buffer existed
        if(newWriteSize)
        {
            VkBufferCopy copyRegion = {};
            copyRegion.srcOffset = 0;
            copyRegion.dstOffset = 0;
            copyRegion.size = newWriteSize;

            SynchronizationInfo syncInfo = {};
            syncInfo.fence = device.getCommands().getUnsignaledFence();
            newBuffer->getBuffer().copyFromBufferRanges(*instancesDataBuffer, { copyRegion }, syncInfo);
            vkWaitForFences(device.getDevice(), 1, &syncInfo.fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(device.getDevice(), syncInfo.fence, nullptr);

            //pseudo write
            newBuffer->newWrite(NULL, newWriteSize, 1, NULL);
        }

        //replace old buffer
        modelDataBuffer = std::move(newBuffer);
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
                    //shift stored location
                    model->shaderDataLocation -= compactionResult.shiftSize;
                }
            }
        }

        //then fix instances data
        for(ModelInstance* instance : renderingModelInstances)
        {
            toUpdateModelInstances.push_front(instance);
        }
    }

    void RenderEngine::rebuildInstancesbuffer()
    {
        //new buffer to replace old
        BufferInfo bufferInfo = {};
        bufferInfo.allocationFlags = 0;
        bufferInfo.size = std::max((VkDeviceSize)(renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance) * instancesDataOverhead), (VkDeviceSize)sizeof(ModelInstance::ShaderModelInstance) * 128);
        bufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        std::unique_ptr<Buffer> newBuffer = std::make_unique<Buffer>(*this, bufferInfo);

        //copy old data into new if old existed
        if(instancesDataBuffer)
        {
            VkBufferCopy copyRegion = {};
            copyRegion.srcOffset = 0;
            copyRegion.dstOffset = 0;
            copyRegion.size = std::min(renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance), (size_t)instancesDataBuffer->getSize());

            SynchronizationInfo syncInfo = {};
            syncInfo.queueType = TRANSFER;
            syncInfo.fence = device.getCommands().getUnsignaledFence();
            newBuffer->copyFromBufferRanges(*instancesDataBuffer, { copyRegion }, syncInfo);
            vkWaitForFences(device.getDevice(), 1, &syncInfo.fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(device.getDevice(), syncInfo.fence, nullptr);
        }
        
        //replace old buffer
        instancesDataBuffer = std::move(newBuffer);
    }

    void RenderEngine::addModelData(Model* model)
    {
        //self reference
        model->selfIndex = renderingModels.size();
        renderingModels.push_back(model);
        
        //"write"
        if(modelDataBuffer->newWrite(NULL, model->getShaderData().size(), 8, &model->shaderDataLocation) == FragmentableBuffer::WriteResult::OUT_OF_MEMORY)
        {
            rebuildModelDataBuffer();
        }

        //queue data transfer
        toUpdateModels.push_front(model);
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
        modelDataBuffer->removeFromRange(model->shaderDataLocation, model->getShaderData().size());
        
        model->selfIndex = UINT64_MAX;

        //TODO UPDATE DEPENDENCIES
    }

    void RenderEngine::addObject(ModelInstance* object)
    {
        //self reference
        object->rendererSelfIndex = renderingModelInstances.size();
        renderingModelInstances.push_back(object);
        
        //queue data transfer
        toUpdateModelInstances.push_front(object);
    }

    void RenderEngine::removeObject(ModelInstance* object)
    {
        if(renderingModelInstances.size() > 1)
        {
            //new reference for last element and remove
            renderingModelInstances.at(object->rendererSelfIndex) = renderingModelInstances.back();
            renderingModelInstances.at(object->rendererSelfIndex)->rendererSelfIndex = object->rendererSelfIndex;

            //queue data transfer
            toUpdateModelInstances.push_front(renderingModelInstances.at(object->rendererSelfIndex));

            //remove last element from instances vector (the one that was moved in the mirrored buffer)
            renderingModelInstances.pop_back();
        }
        else
        {
            renderingModelInstances.clear();
        }

        //null out any instances that may be queued
        for(ModelInstance*& instance : toUpdateModelInstances)
        {
            if(instance == object)
            {
                instance = NULL;
            }
        }
        
        object->rendererSelfIndex = UINT32_MAX;
    }

    void RenderEngine::queueModelsAndInstancesTransfers()
    {
        //check buffer sizes
        if((instancesDataBuffer->getSize() / sizeof(ModelInstance::ShaderModelInstance) < renderingModelInstances.size() && renderingModelInstances.size() > 128) ||
            (instancesDataBuffer->getSize() / sizeof(ModelInstance::ShaderModelInstance) > renderingModelInstances.size() * 2 && renderingModelInstances.size() > 128))
        {
            rebuildInstancesbuffer(); //TODO SYNCHRONIZATION
        }

        //sort instances and models; remove duplicates
        std::sort(toUpdateModelInstances.begin(), toUpdateModelInstances.end());
        auto sortedInstances = std::unique(toUpdateModelInstances.begin(), toUpdateModelInstances.end());
        toUpdateModelInstances.erase(sortedInstances, toUpdateModelInstances.end());

        std::sort(toUpdateModels.begin(), toUpdateModels.end());
        auto sortedModels = std::unique(toUpdateModels.begin(), toUpdateModels.end());
        toUpdateModels.erase(sortedModels, toUpdateModels.end());

        //queue instance data
        for(ModelInstance* instance : toUpdateModelInstances)
        {
            //skip if instance is NULL
            if(!instance) continue;

            ModelInstance::ShaderModelInstance shaderInstance = instance->getShaderInstance();

            //write data
            std::vector<char> data(sizeof(ModelInstance::ShaderModelInstance));
            memcpy(data.data(), &shaderInstance, data.size());
            
            getStagingBuffer().queueDataTransfers(*instancesDataBuffer, sizeof(ModelInstance::ShaderModelInstance) * instance->rendererSelfIndex, data);
        }

        //queue model data
        for(Model* model : toUpdateModels)
        {
            getStagingBuffer().queueDataTransfers(modelDataBuffer->getBuffer(), model->shaderDataLocation, model->getShaderData());
        }

        //clear deques
        toUpdateModelInstances.clear();
        toUpdateModels.clear();
    }

    const VkSemaphore& RenderEngine::beginFrame()
    {
        //reset command and descriptor pools
        device.getCommands().resetCommandPools();
        descriptors.refreshPools();

        //destroy old acceleration structure data
        asBuilder.destroyOldData();

        //acquire next image
        const VkSemaphore& imageAcquireSemaphore = swapchain.acquireNextImage();

        //queue data transfers
        queueModelsAndInstancesTransfers();

        //stage transfers for render passes
        for(RenderPass* renderPass : renderPasses)
        {
            renderPass->queueInstanceTransfers();
        }

        //return image acquire semaphore
        return imageAcquireSemaphore;
    }

    void RenderEngine::endFrame(const std::vector<VkSemaphore>& waitSemaphores)
    {
        //presentation
        swapchain.presentImage(waitSemaphores);

        //increment frame counter so next frame can be prepared early
        frameNumber++;

        glfwPollEvents();
    }
}
