#include "Command.h"

#include <stdexcept>

namespace PaperRenderer
{
    namespace PaperMemory
    {
        //----------CMD BUFFER ALLOCATOR DEFINITIONS----------//

        bool Commands::isInit = false;
        std::unordered_map<QueueType, QueuesInFamily>* Commands::queuesPtr;
        std::unordered_map<QueueType, VkCommandPool> Commands::commandPools;

        Commands::Commands(VkDevice device, std::unordered_map<QueueType, QueuesInFamily>* queuesPtr)
            :device(device)
        {
            this->queuesPtr = queuesPtr;
            isInit = true;
            createCommandPools();
        }

        Commands::~Commands()
        {
            for(auto [type, pool] : commandPools)
            {
                vkDestroyCommandPool(device, pool, nullptr);
            }
        }

        void Commands::createCommandPools()
        {
            for(const auto& [queueType, queues] : *queuesPtr)
            {
                VkCommandPoolCreateInfo graphicsPoolInfo = {};
                graphicsPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                graphicsPoolInfo.pNext = NULL;
                graphicsPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
                graphicsPoolInfo.queueFamilyIndex = queues.queueFamilyIndex;
                vkCreateCommandPool(device, &graphicsPoolInfo, nullptr, &commandPools[queueType]);
            }
        }

        void Commands::freeCommandBuffer(VkDevice device, CommandBuffer& commandBuffer)
        {
            if(isInit)
            {
                vkFreeCommandBuffers(device, commandPools.at(commandBuffer.type), 1, &commandBuffer.buffer);
            }
            else
            {
                throw std::runtime_error("Commands pools not yet initialized");
            }
        }

        void Commands::submitToQueue(VkDevice device, const SynchronizationInfo &synchronizationInfo, const std::vector<VkCommandBuffer> &commandBuffers)
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

            //wait semaphores
            std::vector<VkSemaphoreSubmitInfo> semaphoreWaitInfos;
            for(const SemaphorePair& pair : synchronizationInfo.waitPairs)
            {
                VkSemaphoreSubmitInfo semaphoreInfo = {};
                semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                semaphoreInfo.pNext = NULL;
                semaphoreInfo.semaphore = pair.semaphore;
                semaphoreInfo.stageMask = pair.stage;
                semaphoreInfo.deviceIndex = 0;

                semaphoreWaitInfos.push_back(semaphoreInfo);
            }

            //signal semaphores
            std::vector<VkSemaphoreSubmitInfo> semaphoreSignalInfos;
            for(const SemaphorePair& pair : synchronizationInfo.signalPairs)
            {
                VkSemaphoreSubmitInfo semaphoreInfo = {};
                semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                semaphoreInfo.pNext = NULL;
                semaphoreInfo.semaphore = pair.semaphore;
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

        VkSemaphore Commands::getSemaphore(VkDevice device)
        {
            VkSemaphore semaphore;

            VkSemaphoreCreateInfo semaphoreInfo;
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.flags = 0;

            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore);

            return semaphore;
        }

        VkFence Commands::getSignaledFence(VkDevice device)
        {
            VkFence fence;
            VkFenceCreateInfo fenceInfo = {};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.pNext = NULL;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            vkCreateFence(device, &fenceInfo, nullptr, &fence);

            return fence;
        }

        VkFence Commands::getUnsignaledFence(VkDevice device)
        {
            VkFence fence;
            VkFenceCreateInfo fenceInfo = {};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.pNext = NULL;
            fenceInfo.flags = 0;

            vkCreateFence(device, &fenceInfo, nullptr, &fence);

            return fence;
        }

        VkCommandBuffer Commands::getCommandBuffer(VkDevice device, QueueType type)
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
                VkResult result = vkAllocateCommandBuffers(device, &bufferInfo, &returnBuffer);
                
                return returnBuffer;
            }
            else
            {
                throw std::runtime_error("Command pools not yet initialized");

                return VK_NULL_HANDLE;
            }
        }
    }
}