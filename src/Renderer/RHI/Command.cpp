#include "Command.h"

namespace Renderer
{
    CmdBufferAllocator::CmdBufferAllocator(Device* device)
        :devicePtr(device)
    {
        createCommandPools();
    }

    CmdBufferAllocator::~CmdBufferAllocator()
    {
        vkDestroyCommandPool(devicePtr->getDevice(), commandPools.graphics, nullptr);
        vkDestroyCommandPool(devicePtr->getDevice(), commandPools.compute, nullptr);
        vkDestroyCommandPool(devicePtr->getDevice(), commandPools.transfer, nullptr);
        vkDestroyCommandPool(devicePtr->getDevice(), commandPools.present, nullptr);
    }

    void CmdBufferAllocator::createCommandPools()
    {
        //graphics command pool
        VkCommandPoolCreateInfo graphicsPoolInfo = {};
        graphicsPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        graphicsPoolInfo.pNext = NULL;
        graphicsPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        graphicsPoolInfo.queueFamilyIndex = devicePtr->getQueueFamilies().graphicsFamilyIndex;
        vkCreateCommandPool(devicePtr->getDevice(), &graphicsPoolInfo, nullptr, &(commandPools.graphics));

        //compute command pool
        VkCommandPoolCreateInfo computePoolInfo = {};
        computePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        computePoolInfo.pNext = NULL;
        computePoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        computePoolInfo.queueFamilyIndex = devicePtr->getQueueFamilies().computeFamilyIndex;
        vkCreateCommandPool(devicePtr->getDevice(), &computePoolInfo, nullptr, &(commandPools.compute));

        //transfer command pool
        VkCommandPoolCreateInfo transferPoolInfo = {};
        transferPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        transferPoolInfo.pNext = NULL;
        transferPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        transferPoolInfo.queueFamilyIndex = devicePtr->getQueueFamilies().transferFamilyIndex;
        vkCreateCommandPool(devicePtr->getDevice(), &transferPoolInfo, nullptr, &(commandPools.transfer));

        //present command pool
        VkCommandPoolCreateInfo presentPoolInfo = {};
        presentPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        presentPoolInfo.pNext = NULL;
        presentPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        presentPoolInfo.queueFamilyIndex = devicePtr->getQueueFamilies().presentationFamilyIndex;
        vkCreateCommandPool(devicePtr->getDevice(), &presentPoolInfo, nullptr, &(commandPools.present));
    }

    VkFence CmdBufferAllocator::getSignaledFence()
    {
        VkFence fence;
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.pNext = NULL;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        vkCreateFence(devicePtr->getDevice(), &fenceInfo, nullptr, &fence);

        return fence;
    }

    VkFence CmdBufferAllocator::getUnsignaledFence()
    {
        VkFence fence;
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.pNext = NULL;
        fenceInfo.flags = 0;

        vkCreateFence(devicePtr->getDevice(), &fenceInfo, nullptr, &fence);

        return fence;
    }

    VkCommandBuffer CmdBufferAllocator::getCommandBuffer(CmdPoolType type)
    {
        VkCommandBufferAllocateInfo graphicsBufferInfo = {};
        graphicsBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        graphicsBufferInfo.pNext = NULL;
        graphicsBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        graphicsBufferInfo.commandBufferCount = 1;

        switch(type)
        {
            case GRAPHICS:
                graphicsBufferInfo.commandPool = commandPools.graphics;
                break;
            case COMPUTE:
                graphicsBufferInfo.commandPool = commandPools.compute;
                break;
            case TRANSFER:
                graphicsBufferInfo.commandPool = commandPools.transfer;
                break;
            case PRESENT:
                graphicsBufferInfo.commandPool = commandPools.present;
                break;
        }

        VkCommandBuffer returnBuffer;
        VkResult result = vkAllocateCommandBuffers(devicePtr->getDevice(), &graphicsBufferInfo, &returnBuffer);
        
        return returnBuffer;
    }

    QueueReturn CmdBufferAllocator::submitQueue(const VkSubmitInfo &submitInfo, CmdPoolType poolType)
    {
        std::shared_ptr<VkFence> fence = std::make_shared<VkFence>(getUnsignaledFence());

        VkCommandPool pool;
        VkQueue queue;

        switch(poolType)
        {
            case GRAPHICS:
                pool = commandPools.graphics;
                queue = devicePtr->getQueues().graphics.at(0);
                break;
            case COMPUTE:
                pool = commandPools.compute;
                queue = devicePtr->getQueues().compute.at(0);
                break;
            case TRANSFER:
                pool = commandPools.transfer;
                queue = devicePtr->getQueues().transfer.at(0);
                break;
            case PRESENT:
                pool = commandPools.present;
                queue = devicePtr->getQueues().present.at(0);
                break;
        }

        VkResult result = vkQueueSubmit(queue, 1, &submitInfo, *fence);

        std::vector<VkCommandBuffer> cmdBuffers(submitInfo.commandBufferCount);
        memcpy(cmdBuffers.data(), submitInfo.pCommandBuffers, sizeof(VkCommandBuffer) * cmdBuffers.size());

        return {fence, result, pool, cmdBuffers};
    }

    QueueReturn CmdBufferAllocator::submitPresentQueue(const VkPresentInfoKHR &submitInfo)
    {
        VkResult result = vkQueuePresentKHR(devicePtr->getQueues().present[0], &submitInfo);

        return {NULL, result, commandPools.present, std::vector<VkCommandBuffer>()};
    }

    void Renderer::CmdBufferAllocator::waitForQueue(QueueReturn& info)
    {
        if(info.fence.get())
        {
            vkWaitForFences(devicePtr->getDevice(), 1, info.fence.get(), VK_TRUE, UINT32_MAX);
            vkDestroyFence(devicePtr->getDevice(), *(info.fence.get()), nullptr);
        }
        if(info.commandBuffers.size())
        {
            vkFreeCommandBuffers(devicePtr->getDevice(), info.pool, info.commandBuffers.size(), info.commandBuffers.data());
        }
    }
}