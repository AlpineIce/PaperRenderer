#include "Command.h"

namespace Renderer
{
    //----------QUEUE RETURN DEFINITIONS----------//

    QueueReturn::QueueReturn(QueuePrivateData pData)
        :pData(pData)
    {
    }

    QueueReturn::QueueReturn(QueueReturn &&queueReturn)
    {
        shallowCopy(queueReturn.pData);
        queueReturn.moveInvalidate();
    }

    QueueReturn::~QueueReturn()
    {
        waitForFence();
        for(VkSemaphore semaphore : pData.semaphores)
        {
            if(semaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(pData.devicePtr->getDevice(), semaphore, nullptr);
            }
        }
        if(pData.commandBuffers.size())
        {
            vkFreeCommandBuffers(pData.devicePtr->getDevice(), pData.pool, pData.commandBuffers.size(), pData.commandBuffers.data());
        }
    }

    void QueueReturn::shallowCopy(QueuePrivateData& data)
    {
        this->pData = data;
    }

    void QueueReturn::moveInvalidate()
    {
        pData.fence = VK_NULL_HANDLE;
        pData.semaphores.resize(0);
        pData.commandBuffers.resize(0);
    }

    void QueueReturn::waitForFence()
    {
        if(pData.fence != VK_NULL_HANDLE)
        {
            vkWaitForFences(pData.devicePtr->getDevice(), 1, &pData.fence, VK_TRUE, UINT32_MAX);
            vkDestroyFence(pData.devicePtr->getDevice(), pData.fence, nullptr);
            pData.fence = VK_NULL_HANDLE;
        }
    }

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

    VkSemaphore CmdBufferAllocator::getSemaphore()
    {
        VkSemaphore semaphore;

        VkSemaphoreCreateInfo semaphoreInfo;
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphoreInfo.pNext = NULL;
        semaphoreInfo.flags = 0;

        vkCreateSemaphore(devicePtr->getDevice(), &semaphoreInfo, nullptr, &semaphore);

        return semaphore;
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

    QueueReturn CmdBufferAllocator::submitQueue(const VkSubmitInfo &submitInfo, CmdPoolType poolType, bool useFence)
    {
        VkFence fence;
        if(useFence)
        {
            fence = getUnsignaledFence();
        }
        else
        {
            fence = VK_NULL_HANDLE;
        }
        
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

        VkResult result = vkQueueSubmit(queue, 1, &submitInfo, fence);

        std::vector<VkCommandBuffer> cmdBuffers(submitInfo.commandBufferCount);
        std::vector<VkSemaphore> semaphores(submitInfo.signalSemaphoreCount);
        memcpy(cmdBuffers.data(), submitInfo.pCommandBuffers, sizeof(VkCommandBuffer) * cmdBuffers.size());
        memcpy(semaphores.data(), submitInfo.pSignalSemaphores, sizeof(VkSemaphore) * semaphores.size());

        QueuePrivateData pData;
        pData.devicePtr = devicePtr;
        pData.commandBuffers = cmdBuffers;
        pData.fence = fence;
        pData.semaphores = semaphores;
        pData.pool = pool;
        pData.result = result;

        return QueueReturn(pData);
    }

    QueueReturn CmdBufferAllocator::submitPresentQueue(const VkPresentInfoKHR &submitInfo)
    {
        VkResult result = vkQueuePresentKHR(devicePtr->getQueues().present[0], &submitInfo);

        QueuePrivateData pData;
        pData.devicePtr = devicePtr;
        pData.commandBuffers = std::vector<VkCommandBuffer>();
        pData.fence = VK_NULL_HANDLE;
        pData.semaphores = std::vector<VkSemaphore>();
        pData.pool = commandPools.present;
        pData.result = result;

        return QueueReturn(pData);
    }
}