#include "Command.h"
#include "PaperRenderer.h"

#include <stdexcept>
#include <algorithm>
#include <thread>

namespace PaperRenderer
{
    //----------CMD BUFFER ALLOCATOR DEFINITIONS----------//

    Commands::Commands(RenderEngine& renderer, std::unordered_map<QueueType, QueuesInFamily>* queuesPtr)
        :renderer(renderer),
        queuesPtr(queuesPtr),
        maxThreadCount(std::thread::hardware_concurrency())
    {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderer.getDevice().getGPU(), renderer.getDevice().getSurface(), &capabilities);

        createCommandPools();
    }

    Commands::~Commands()
    {
        for(auto& [type, pool] : commandPools)
        {
            vkDestroyCommandPool(renderer.getDevice().getDevice(), pool.cmdPool, nullptr);
        }
    }

    void Commands::createCommandPools()
    {
        for(auto& [queueType, queues] : *queuesPtr)
        {
            VkCommandPoolCreateInfo graphicsPoolInfo = {};
            graphicsPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            graphicsPoolInfo.pNext = NULL;
            graphicsPoolInfo.flags = 0;
            graphicsPoolInfo.queueFamilyIndex = queues.queueFamilyIndex;

            vkCreateCommandPool(renderer.getDevice().getDevice(), &graphicsPoolInfo, nullptr, &commandPools[queueType].cmdPool);
        }
    }

    void Commands::resetCommandPools()
    {
        for(auto& [queueType, poolData] : commandPools)
        {
            vkResetCommandPool(renderer.getDevice().getDevice(), poolData.cmdPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
            poolData.cmdBufferStackLocation = 0;
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

    VkSemaphore Commands::getSemaphore()
    {
        VkSemaphore semaphore;

        VkSemaphoreCreateInfo semaphoreInfo;
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphoreInfo.pNext = NULL;
        semaphoreInfo.flags = 0;

        vkCreateSemaphore(renderer.getDevice().getDevice(), &semaphoreInfo, nullptr, &semaphore);

        return semaphore;
    }

    VkSemaphore Commands::getTimelineSemaphore(uint64_t initialValue)
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

        vkCreateSemaphore(renderer.getDevice().getDevice(), &semaphoreInfo, nullptr, &semaphore);

        return semaphore;
    }

    VkFence Commands::getSignaledFence()
    {
        VkFence fence;
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.pNext = NULL;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        vkCreateFence(renderer.getDevice().getDevice(), &fenceInfo, nullptr, &fence);

        return fence;
    }

    VkFence Commands::getUnsignaledFence()
    {
        VkFence fence;
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.pNext = NULL;
        fenceInfo.flags = 0;

        vkCreateFence(renderer.getDevice().getDevice(), &fenceInfo, nullptr, &fence);

        return fence;
    }

    VkCommandBuffer Commands::getCommandBuffer(QueueType type)
    {
        uint32_t& stackLocation = commandPools.at(type).cmdBufferStackLocation;

        //allocate more command buffers if needed
        if(!(stackLocation < commandPools.at(type).cmdBuffers.size()))
        {
            const uint32_t bufferCount = 64;
            commandPools.at(type).cmdBuffers.resize(commandPools.at(type).cmdBuffers.size() + 64);

            VkCommandBufferAllocateInfo bufferInfo = {};
            bufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            bufferInfo.pNext = NULL;
            bufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            bufferInfo.commandBufferCount = bufferCount;
            bufferInfo.commandPool = commandPools.at(type).cmdPool;

            VkResult result = vkAllocateCommandBuffers(renderer.getDevice().getDevice(), &bufferInfo,
                &commandPools.at(type).cmdBuffers.at(stackLocation));
        }

        //get command buffer
        const VkCommandBuffer& returnBuffer = commandPools.at(type).cmdBuffers.at(stackLocation);

        //increment stack
        stackLocation++;
        
        return returnBuffer;
    }
}