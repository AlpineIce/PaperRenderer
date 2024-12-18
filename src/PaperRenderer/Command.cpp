#include "Command.h"
#include "PaperRenderer.h"

#include <stdexcept>
#include <algorithm>
#include <future>
#include <functional>

namespace PaperRenderer
{
    //----------CMD BUFFER ALLOCATOR DEFINITIONS----------//

    Commands::Commands(RenderEngine& renderer, std::unordered_map<QueueType, QueuesInFamily>* queuesPtr)
        :renderer(renderer),
        queuesPtr(queuesPtr)
    {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderer.getDevice().getGPU(), renderer.getDevice().getSurface(), &capabilities);

        createCommandPools();

        //log constructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "Commands constructor finished"
        });
    }

    Commands::~Commands()
    {
        for(auto& [type, pools] : commandPools)
        {
            //wait for any remaining queue submissions
            for(Queue* queue : queuesPtr->at(type).queues)
            {
                std::lock_guard<std::mutex> guard(queue->threadLock);
            }

            //wait for and destroy command pools
            for(CommandPoolData& pool : pools)
            {
                std::lock_guard<std::recursive_mutex> guard(pool.threadLock);
                vkDestroyCommandPool(renderer.getDevice().getDevice(), pool.cmdPool, nullptr);
            }
        }

        //log destructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "Commands destructor initialized"
        });
    }

    void Commands::createCommandPools()
    {
        //create 4 arrays of std::thread::hardware_concurrency length arrays of command pools (honestly presentation doesnt need that many pools but its ok)
        for(auto& [queueType, queues] : *queuesPtr)
        {
            commandPools[queueType] = std::vector<CommandPoolData>(coreCount);
            for(uint32_t i = 0; i < coreCount; i++)
            {
                VkCommandPoolCreateInfo commandPoolInfo = {
                    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                    .pNext = NULL,
                    .flags = 0,
                    .queueFamilyIndex = queues.queueFamilyIndex
                };
                vkCreateCommandPool(renderer.getDevice().getDevice(), &commandPoolInfo, nullptr, &commandPools[queueType][i].cmdPool);
            }
        }
    }

    void Commands::resetCommandPools()
    {
        //Timer
        Timer timer(renderer, "Reset Command Pools");

        //give warning if if there are locked command buffers present (from counter)
        if(lockedCmdBufferCount)
        {
            renderer.getLogger().recordLog({
                .type = WARNING,
                .text = std::to_string(lockedCmdBufferCount) + " Locked command buffers present at time of resetting command pools. Imminent deadlock WILL occur"
            });
        }

        //reset all pools
        for(auto& [queueType, pools] : commandPools)
        {
            //async reset pools
            std::vector<std::future<void>> futures;
            futures.reserve(coreCount);

            for(CommandPoolData& pool : pools)
            {
                //wait for any non-submitted command buffers (potentially a deadlock problem)
                std::lock_guard<std::recursive_mutex> guard(pool.threadLock);

                //reset pool
                vkResetCommandPool(renderer.getDevice().getDevice(), pool.cmdPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
                pool.cmdBufferStackLocation = 0;
            }
        }
    }

    void Commands::submitToQueue(const SynchronizationInfo &synchronizationInfo, const std::vector<VkCommandBuffer> &commandBuffers)
    {
        //command buffers
        std::vector<VkCommandBufferSubmitInfo> cmdBufferSubmitInfos = {};
        for(const VkCommandBuffer& cmdBuffer : commandBuffers)
        {
            //add to submit info
            VkCommandBufferSubmitInfo cmdBufferSubmitInfo = {};
            cmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            cmdBufferSubmitInfo.pNext = NULL;
            cmdBufferSubmitInfo.commandBuffer = cmdBuffer;
            cmdBufferSubmitInfo.deviceMask = 0;

            cmdBufferSubmitInfos.push_back(cmdBufferSubmitInfo);

            //verify command buffer is unlocked, throw error if it isnt
            if(cmdBuffersLockedPool.count(cmdBuffer)) throw std::runtime_error("Command buffer submitted never had its mutex unlocked. Please call unlockCommandBuffer(cmdBuffer) on same thread recorded on");
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
        //find a command pool to lock (similar to queue submit, just keep looping until one is available)
        bool threadLocked = false;
        CommandPoolData* lockedPool = NULL;
        while(!threadLocked)
        {
            for(CommandPoolData& pool : commandPools[type])
            {
                if(pool.threadLock.try_lock())
                {
                    threadLocked = true;
                    lockedPool = &pool;

                    break;
                }
            }
        }

        uint32_t& stackLocation = lockedPool->cmdBufferStackLocation;

        //allocate more command buffers if needed
        if(!(stackLocation < lockedPool->cmdBuffers.size()))
        {
            const uint32_t bufferCount = 64;
            lockedPool->cmdBuffers.resize(lockedPool->cmdBuffers.size() + 64);

            VkCommandBufferAllocateInfo bufferInfo = {};
            bufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            bufferInfo.pNext = NULL;
            bufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            bufferInfo.commandBufferCount = bufferCount;
            bufferInfo.commandPool = lockedPool->cmdPool;

            VkResult result = vkAllocateCommandBuffers(renderer.getDevice().getDevice(), &bufferInfo, &lockedPool->cmdBuffers[stackLocation]);
        }

        //get command buffer
        VkCommandBuffer returnBuffer = lockedPool->cmdBuffers[stackLocation];

        //keep track of command buffer's locked command pool
        std::lock_guard guard(cmdBuffersLockedPoolMutex);
        cmdBuffersLockedPool[returnBuffer] = lockedPool;

        //increment locked counter (protected by mutex)
        lockedCmdBufferCount++;

        //increment stack
        stackLocation++;

        //----------COMMAND POOL REMAINS LOCKED UNTIL QUEUE SUBMISSION, BUT RECURSIVE MUTEX ALLOWS MORE ALLOCATIONS ON THE SAME THREAD----------//
        
        return returnBuffer;
    }

    void Commands::unlockCommandBuffer(VkCommandBuffer cmdBuffer)
    {
        //unlock the command pool used by the command buffer (if in map)
        std::lock_guard guard(cmdBuffersLockedPoolMutex);
        if(cmdBuffersLockedPool.count(cmdBuffer))
        {
            cmdBuffersLockedPool[cmdBuffer]->threadLock.unlock();
            cmdBuffersLockedPool.erase(cmdBuffer);
        }

        //decrement locked counter (protected by mutex)
        lockedCmdBufferCount--;
    }
}