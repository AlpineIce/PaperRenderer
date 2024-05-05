#include "Command.h"

#include <stdexcept>

namespace PaperRenderer
{
    namespace PaperMemory
    {

        //----------CMD BUFFER ALLOCATOR DEFINITIONS----------//

        bool Commands::isInit = false;
        QueueFamiliesIndices Commands::queueFamilyIndices;
        std::unordered_map<CmdPoolType, VkCommandPool> Commands::commandPools;

        Commands::Commands(VkDevice device, const QueueFamiliesIndices& queueFamilyIndices)
            :device(device)
        {
            this->queueFamilyIndices = queueFamilyIndices;
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
            //graphics command pool
            VkCommandPoolCreateInfo graphicsPoolInfo = {};
            graphicsPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            graphicsPoolInfo.pNext = NULL;
            graphicsPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            graphicsPoolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamilyIndex;
            vkCreateCommandPool(device, &graphicsPoolInfo, nullptr, &commandPools[GRAPHICS]);

            //compute command pool
            VkCommandPoolCreateInfo computePoolInfo = {};
            computePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            computePoolInfo.pNext = NULL;
            computePoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            computePoolInfo.queueFamilyIndex = queueFamilyIndices.computeFamilyIndex;
            vkCreateCommandPool(device, &computePoolInfo, nullptr, &commandPools[COMPUTE]);

            //transfer command pool
            VkCommandPoolCreateInfo transferPoolInfo = {};
            transferPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            transferPoolInfo.pNext = NULL;
            transferPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            transferPoolInfo.queueFamilyIndex = queueFamilyIndices.transferFamilyIndex;
            vkCreateCommandPool(device, &transferPoolInfo, nullptr, &commandPools[TRANSFER]);

            //present command pool
            VkCommandPoolCreateInfo presentPoolInfo = {};
            presentPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            presentPoolInfo.pNext = NULL;
            presentPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            presentPoolInfo.queueFamilyIndex = queueFamilyIndices.presentationFamilyIndex;
            vkCreateCommandPool(device, &presentPoolInfo, nullptr, &commandPools[PRESENT]);
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

            vkQueueSubmit2(synchronizationInfo.queue, 1, &submitInfo, synchronizationInfo.fence);
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

        VkCommandBuffer Commands::getCommandBuffer(VkDevice device, CmdPoolType type)
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