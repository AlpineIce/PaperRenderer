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
        for(std::unordered_map<PaperRenderer::QueueType, std::vector<PaperRenderer::Commands::CommandPoolData>>& frameCommandPool : commandPools)
        {
            for(auto& [type, pools] : frameCommandPool)
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
        }

        //log destructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "Commands destructor initialized"
        });
    }

    void Commands::createCommandPools()
    {
        for(std::unordered_map<PaperRenderer::QueueType, std::vector<PaperRenderer::Commands::CommandPoolData>>& queuePoolDatas : commandPools)
        {
            //create 4 arrays of std::thread::hardware_concurrency length arrays of command pools (honestly presentation doesnt need that many pools but its ok)
            for(auto& [queueType, queues] : *queuesPtr)
            {
                queuePoolDatas[queueType] = std::vector<CommandPoolData>(coreCount);

                for(uint32_t i = 0; i < coreCount; i++)
                {
                    VkCommandPoolCreateInfo commandPoolInfo = {
                        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                        .pNext = NULL,
                        .flags = 0,
                        .queueFamilyIndex = queues.queueFamilyIndex
                    };
                    vkCreateCommandPool(renderer.getDevice().getDevice(), &commandPoolInfo, nullptr, &queuePoolDatas[queueType][i].cmdPool);
                }
            }
        }
    }

    void Commands::resetCommandPools()
    {
        //Timer
        Timer timer(renderer, "Reset Command Pools", REGULAR);

        //give warning if if there are locked command buffers present (from counter)
        if(lockedCmdBufferCount)
        {
            renderer.getLogger().recordLog({
                .type = WARNING,
                .text = std::to_string(lockedCmdBufferCount) + " Locked command buffers present at time of resetting command pools. Imminent deadlock WILL occur"
            });
        }

        //reset all pools
        for(auto& [queueType, pools] : commandPools[renderer.getBufferIndex()])
        {
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

    const Queue& Commands::submitToQueue(const SynchronizationInfo &synchronizationInfo, const std::vector<VkCommandBuffer> &commandBuffers)
    {
        //command buffers
        std::vector<VkCommandBufferSubmitInfo> cmdBufferSubmitInfos = {};
        for(const VkCommandBuffer& cmdBuffer : commandBuffers)
        {
            //add to submit info
            cmdBufferSubmitInfos.push_back({
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .pNext = NULL,
                .commandBuffer = cmdBuffer,
                .deviceMask = 0
            });

            //verify command buffer is unlocked, throw error if it isnt
            if(cmdBuffersLockedPool.count(cmdBuffer)) throw std::runtime_error("Command buffer submitted never had its mutex unlocked. Please call unlockCommandBuffer(cmdBuffer) on same thread recorded on");
        }

        std::vector<VkSemaphoreSubmitInfo> semaphoreWaitInfos;
        std::vector<VkSemaphoreSubmitInfo> semaphoreSignalInfos;

        //binary wait semaphores
        for(const BinarySemaphorePair& pair : synchronizationInfo.binaryWaitPairs)
        {
            semaphoreWaitInfos.push_back({
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .pNext = NULL,
                .semaphore = pair.semaphore,
                .stageMask = pair.stage,
                .deviceIndex = 0
            });
        }

        //binary signal semaphores
        for(const BinarySemaphorePair& pair : synchronizationInfo.binarySignalPairs)
        {
            semaphoreSignalInfos.push_back({
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .pNext = NULL,
                .semaphore = pair.semaphore,
                .stageMask = pair.stage,
                .deviceIndex = 0
            });
        }

        //timeline wait semaphores
        for(const TimelineSemaphorePair& pair : synchronizationInfo.timelineWaitPairs)
        {
            semaphoreWaitInfos.push_back({
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .pNext = NULL,
                .semaphore = pair.semaphore,
                .value = pair.value,
                .stageMask = pair.stage,
                .deviceIndex = 0
            });
        }

        //timeline signal semaphores
        for(const TimelineSemaphorePair& pair : synchronizationInfo.timelineSignalPairs)
        {
            semaphoreSignalInfos.push_back({
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .pNext = NULL,
                .semaphore = pair.semaphore,
                .value = pair.value,
                .stageMask = pair.stage,
                .deviceIndex = 0
            });
        }
        
        //fill in the submit info
        const VkSubmitInfo2 submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = NULL,
            .flags = 0,
            .waitSemaphoreInfoCount = (uint32_t)semaphoreWaitInfos.size(),
            .pWaitSemaphoreInfos = semaphoreWaitInfos.data(),
            .commandBufferInfoCount = (uint32_t)cmdBufferSubmitInfos.size(),
            .pCommandBufferInfos = cmdBufferSubmitInfos.data(),
            .signalSemaphoreInfoCount = (uint32_t)semaphoreSignalInfos.size(),
            .pSignalSemaphoreInfos = semaphoreSignalInfos.data()
        };

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

        return(*lockedQueue);
    }

    VkSemaphore Commands::getSemaphore()
    {
        VkSemaphore semaphore;

        VkSemaphoreCreateInfo semaphoreInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0
        };

        vkCreateSemaphore(renderer.getDevice().getDevice(), &semaphoreInfo, nullptr, &semaphore);

        return semaphore;
    }

    VkSemaphore Commands::getTimelineSemaphore(uint64_t initialValue)
    {
        VkSemaphore semaphore;

        const VkSemaphoreTypeCreateInfo semaphoreTypeInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .pNext = NULL,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = initialValue
        };

        const VkSemaphoreCreateInfo semaphoreInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &semaphoreTypeInfo,
            .flags = 0
        };

        vkCreateSemaphore(renderer.getDevice().getDevice(), &semaphoreInfo, nullptr, &semaphore);

        return semaphore;
    }

    VkFence Commands::getSignaledFence()
    {
        VkFence fence;
        const VkFenceCreateInfo fenceInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = NULL,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT
        };

        vkCreateFence(renderer.getDevice().getDevice(), &fenceInfo, nullptr, &fence);

        return fence;
    }

    VkFence Commands::getUnsignaledFence()
    {
        VkFence fence;
        const VkFenceCreateInfo fenceInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0
        };

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
            for(CommandPoolData& pool : commandPools[renderer.getBufferIndex()][type])
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

            const VkCommandBufferAllocateInfo bufferInfo = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .pNext = NULL,
                .commandPool = lockedPool->cmdPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = bufferCount
            };

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