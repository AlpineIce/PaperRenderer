#include "Command.h"

namespace Renderer
{

    //----------CMD BUFFER ALLOCATOR DEFINITIONS----------//

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

    VkSemaphore CmdBufferAllocator::getSemaphore() const
    {
        VkSemaphore semaphore;

        VkSemaphoreCreateInfo semaphoreInfo;
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphoreInfo.pNext = NULL;
        semaphoreInfo.flags = 0;

        vkCreateSemaphore(devicePtr->getDevice(), &semaphoreInfo, nullptr, &semaphore);

        return semaphore;
    }

    VkFence CmdBufferAllocator::getSignaledFence() const
    {
        VkFence fence;
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.pNext = NULL;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        vkCreateFence(devicePtr->getDevice(), &fenceInfo, nullptr, &fence);

        return fence;
    }

    VkFence CmdBufferAllocator::getUnsignaledFence() const
    {
        VkFence fence;
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.pNext = NULL;
        fenceInfo.flags = 0;

        vkCreateFence(devicePtr->getDevice(), &fenceInfo, nullptr, &fence);

        return fence;
    }

    VkCommandBuffer CmdBufferAllocator::getCommandBuffer(CmdPoolType type) const
    {
        VkCommandBufferAllocateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        bufferInfo.pNext = NULL;
        bufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        bufferInfo.commandBufferCount = 1;

        switch(type)
        {
            case GRAPHICS:
                bufferInfo.commandPool = commandPools.graphics;
                break;
            case COMPUTE:
                bufferInfo.commandPool = commandPools.compute;
                break;
            case TRANSFER:
                bufferInfo.commandPool = commandPools.transfer;
                break;
            case PRESENT:
                bufferInfo.commandPool = commandPools.present;
                break;
        }

        VkCommandBuffer returnBuffer;
        VkResult result = vkAllocateCommandBuffers(devicePtr->getDevice(), &bufferInfo, &returnBuffer);
        
        return returnBuffer;
    }


}