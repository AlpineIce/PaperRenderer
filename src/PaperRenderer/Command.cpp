#include "Command.h"
#include "PaperRenderer.h"

#include <stdexcept>
#include <algorithm>
#include <thread>

namespace PaperRenderer
{
    //----------CMD BUFFER ALLOCATOR DEFINITIONS----------//

    bool Commands::isInit = false;
    std::unordered_map<QueueType, QueuesInFamily>* Commands::queuesPtr;
    std::unordered_map<QueueType, VkCommandPool> Commands::commandPools;

    Commands::Commands(RenderEngine* renderer, std::unordered_map<QueueType, QueuesInFamily>* queuesPtr)
        :rendererPtr(renderer)
    {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rendererPtr->getDevice()->getGPU(), rendererPtr->getDevice()->getSurface(), &capabilities);

        this->queuesPtr = queuesPtr;
        isInit = true;
        createCommandPools();
    }

    Commands::~Commands()
    {
        for(auto [type, pool] : commandPools)
        {
            vkDestroyCommandPool(rendererPtr->getDevice()->getDevice(), pool, nullptr);
        }
    }

    void Commands::createCommandPools()
    {
        for(const auto& [queueType, queues] : *queuesPtr)
        {
            VkCommandPoolCreateInfo graphicsPoolInfo = {};
            graphicsPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            graphicsPoolInfo.pNext = NULL;
            graphicsPoolInfo.flags = 0;
            graphicsPoolInfo.queueFamilyIndex = queues.queueFamilyIndex;
            vkCreateCommandPool(rendererPtr->getDevice()->getDevice(), &graphicsPoolInfo, nullptr, &commandPools[queueType]);
        }
    }

    void Commands::freeCommandBuffers(RenderEngine* renderer, std::vector<CommandBuffer>& commandBuffers)
    {
        if(isInit)
        {
            std::unordered_map<QueueType, std::vector<VkCommandBuffer>> sortedCommandBuffers;
            for(CommandBuffer& cmdBuffer : commandBuffers)
            {
                sortedCommandBuffers[cmdBuffer.type].push_back(cmdBuffer.buffer);
            }
            for(const auto& [type, buffers] : sortedCommandBuffers)
            {
                vkFreeCommandBuffers(renderer->getDevice()->getDevice(), commandPools.at(type), buffers.size(), buffers.data());
            }
        }
        else
        {
            throw std::runtime_error("Commands pools not yet initialized");
        }
    }

    void Commands::submitToQueue(const SynchronizationInfo &synchronizationInfo, const std::vector<VkCommandBuffer> &commandBuffers)
    {
        //command buffers
        std::vector<VkCommandBufferSubmitInfo> cmdBufferSubmitInfos = {};
        for(const VkCommandBuffer& cmdBuffer : commandBuffers)
        {
            VkCommandBufferSubmitInfo cmdBufferSubmitInfo = {};
            cmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            cmdBufferSubmitInfo.pNext = NULL;
            cmdBufferSubmitInfo.commandBuffer = cmdBuffer;
            cmdBufferSubmitInfo.deviceMask = 0;

            cmdBufferSubmitInfos.push_back(cmdBufferSubmitInfo);
        }

        std::vector<VkSemaphoreSubmitInfo> semaphoreWaitInfos;
        std::vector<VkSemaphoreSubmitInfo> semaphoreSignalInfos;

        //binary wait semaphores
        for(const BinarySemaphorePair& pair : synchronizationInfo.binaryWaitPairs)
        {
            VkSemaphoreSubmitInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.semaphore = pair.semaphore;
            semaphoreInfo.stageMask = pair.stage;
            semaphoreInfo.deviceIndex = 0;

            semaphoreWaitInfos.push_back(semaphoreInfo);
        }

        //binary signal semaphores
        for(const BinarySemaphorePair& pair : synchronizationInfo.binarySignalPairs)
        {
            VkSemaphoreSubmitInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.semaphore = pair.semaphore;
            semaphoreInfo.stageMask = pair.stage;
            semaphoreInfo.deviceIndex = 0;

            semaphoreSignalInfos.push_back(semaphoreInfo);
        }

        //timeline wait semaphores
        for(const TimelineSemaphorePair& pair : synchronizationInfo.timelineWaitPairs)
        {
            VkSemaphoreSubmitInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.semaphore = pair.semaphore;
            semaphoreInfo.value = pair.value;
            semaphoreInfo.stageMask = pair.stage;
            semaphoreInfo.deviceIndex = 0;

            semaphoreWaitInfos.push_back(semaphoreInfo);
        }

        //timeline signal semaphores
        for(const TimelineSemaphorePair& pair : synchronizationInfo.timelineSignalPairs)
        {
            VkSemaphoreSubmitInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.semaphore = pair.semaphore;
            semaphoreInfo.value = pair.value;
            semaphoreInfo.stageMask = pair.stage;
            semaphoreInfo.deviceIndex = 0;

            semaphoreSignalInfos.push_back(semaphoreInfo);
        }
        
        //fill in the submit info
        VkSubmitInfo2 submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.pNext = NULL;
        submitInfo.flags = 0;
        submitInfo.waitSemaphoreInfoCount = semaphoreWaitInfos.size();
        submitInfo.pWaitSemaphoreInfos = semaphoreWaitInfos.data();
        submitInfo.commandBufferInfoCount = cmdBufferSubmitInfos.size();
        submitInfo.pCommandBufferInfos = cmdBufferSubmitInfos.data();
        submitInfo.signalSemaphoreInfoCount = semaphoreSignalInfos.size();
        submitInfo.pSignalSemaphoreInfos = semaphoreSignalInfos.data();

        //find an "unlocked" queue with the specified type (also nested hell I know)
        Queue* lockedQueue = NULL;
        if(queuesPtr->count(synchronizationInfo.queueType))
        {
            bool threadLocked = false;
            while(!threadLocked)
            {
                for(Queue* queue : queuesPtr->at(synchronizationInfo.queueType).queues)
                {
                    if(queue->threadLock.try_lock())
                    {
                        threadLocked = true;
                        lockedQueue = queue;
                        
                        break;
                    }
                }
            }
        }
        else
        {
            throw std::runtime_error("No queues available for specified submission type");
        }
        
        //submit
        if(lockedQueue)
        {
            vkQueueSubmit2(lockedQueue->queue, 1, &submitInfo, synchronizationInfo.fence);
        }
        else
        {
            throw std::runtime_error("Tried to submit to null queue");
        }
        
        lockedQueue->threadLock.unlock();
    }

    VkSemaphore Commands::getSemaphore(RenderEngine* renderer)
    {
        VkSemaphore semaphore;

        VkSemaphoreCreateInfo semaphoreInfo;
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphoreInfo.pNext = NULL;
        semaphoreInfo.flags = 0;

        vkCreateSemaphore(renderer->getDevice()->getDevice(), &semaphoreInfo, nullptr, &semaphore);

        return semaphore;
    }

    VkSemaphore Commands::getTimelineSemaphore(RenderEngine* renderer, uint64_t initialValue)
    {
        VkSemaphore semaphore;

        VkSemaphoreTypeCreateInfo semaphoreTypeInfo = {};
        semaphoreTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        semaphoreTypeInfo.pNext = NULL;
        semaphoreTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        semaphoreTypeInfo.initialValue = initialValue;

        VkSemaphoreCreateInfo semaphoreInfo;
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphoreInfo.pNext = &semaphoreTypeInfo;
        semaphoreInfo.flags = 0;

        vkCreateSemaphore(renderer->getDevice()->getDevice(), &semaphoreInfo, nullptr, &semaphore);

        return semaphore;
    }

    VkFence Commands::getSignaledFence(RenderEngine* renderer)
    {
        VkFence fence;
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.pNext = NULL;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        vkCreateFence(renderer->getDevice()->getDevice(), &fenceInfo, nullptr, &fence);

        return fence;
    }

    VkFence Commands::getUnsignaledFence(RenderEngine* renderer)
    {
        VkFence fence;
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.pNext = NULL;
        fenceInfo.flags = 0;

        vkCreateFence(renderer->getDevice()->getDevice(), &fenceInfo, nullptr, &fence);

        return fence;
    }

    VkCommandBuffer Commands::getCommandBuffer(RenderEngine* renderer, QueueType type)
    {
        if(isInit)
        {
            VkCommandBufferAllocateInfo bufferInfo = {};
            bufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            bufferInfo.pNext = NULL;
            bufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            bufferInfo.commandBufferCount = 1;
            bufferInfo.commandPool = commandPools.at(type);

            VkCommandBuffer returnBuffer;
            VkResult result = vkAllocateCommandBuffers(renderer->getDevice()->getDevice(), &bufferInfo, &returnBuffer);
            
            return returnBuffer;
        }
        else
        {
            throw std::runtime_error("Command pools not yet initialized");

            return VK_NULL_HANDLE;
        }
    }
}