#include "Command.h"

namespace Renderer
{
    Commands::Commands(Device* device)
        :devicePtr(device)
    {
        //----------COMMAND POOL CREATION----------//

        //graphics command pool
        VkCommandPoolCreateInfo graphicsPoolInfo = {};
        graphicsPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        graphicsPoolInfo.pNext = NULL;
        graphicsPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        graphicsPoolInfo.queueFamilyIndex = devicePtr->getQueueFamilies().graphicsFamilyIndex;
        vkCreateCommandPool(devicePtr->getDevice(), &graphicsPoolInfo, nullptr, &(commandPools.graphics));

        //compute command pool
        VkCommandPoolCreateInfo computePoolInfo = {};
        computePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        computePoolInfo.pNext = NULL;
        computePoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        computePoolInfo.queueFamilyIndex = devicePtr->getQueueFamilies().computeFamilyIndex;
        vkCreateCommandPool(devicePtr->getDevice(), &computePoolInfo, nullptr, &(commandPools.compute));

        //transfer command pool
        VkCommandPoolCreateInfo transferPoolInfo = {};
        transferPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        transferPoolInfo.pNext = NULL;
        transferPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT ;
        transferPoolInfo.queueFamilyIndex = devicePtr->getQueueFamilies().transferFamilyIndex;
        vkCreateCommandPool(devicePtr->getDevice(), &transferPoolInfo, nullptr, &(commandPools.transfer));

        //present command pool
        VkCommandPoolCreateInfo presentPoolInfo = {};
        presentPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        presentPoolInfo.pNext = NULL;
        presentPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        presentPoolInfo.queueFamilyIndex = devicePtr->getQueueFamilies().presentationFamilyIndex;
        vkCreateCommandPool(devicePtr->getDevice(), &presentPoolInfo, nullptr, &(commandPools.present));

        //----------COMMAND BUFFER CREATION----------//

        //graphics (2 for 2 frames)
        commandBuffers.graphics.resize(frameCount);
        VkCommandBufferAllocateInfo graphicsBufferInfo = {};
        graphicsBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        graphicsBufferInfo.pNext = NULL;
        graphicsBufferInfo.commandPool = commandPools.graphics;
        graphicsBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        graphicsBufferInfo.commandBufferCount = frameCount;

        vkAllocateCommandBuffers(devicePtr->getDevice(), &graphicsBufferInfo, commandBuffers.graphics.data());

        //compute
        commandBuffers.compute.resize(1);
        VkCommandBufferAllocateInfo computeBufferInfo = {};
        computeBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        computeBufferInfo.pNext = NULL;
        computeBufferInfo.commandPool = commandPools.compute;
        computeBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        computeBufferInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(devicePtr->getDevice(), &computeBufferInfo, commandBuffers.compute.data());

        //transfer
        commandBuffers.transfer.resize(1);
        VkCommandBufferAllocateInfo transferBufferInfo = {};
        transferBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        transferBufferInfo.pNext = NULL;
        transferBufferInfo.commandPool = commandPools.transfer;
        transferBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        transferBufferInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(devicePtr->getDevice(), &transferBufferInfo, commandBuffers.transfer.data());

        //present (2 fer 2 frames)
        commandBuffers.present.resize(frameCount);
        VkCommandBufferAllocateInfo presentBufferInfo = {};
        presentBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        presentBufferInfo.pNext = NULL;
        presentBufferInfo.commandPool = commandPools.present;
        presentBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        presentBufferInfo.commandBufferCount = frameCount;
        
        vkAllocateCommandBuffers(devicePtr->getDevice(), &presentBufferInfo, commandBuffers.present.data());
    }
    Commands::~Commands()
    {
        vkDestroyCommandPool(devicePtr->getDevice(), commandPools.graphics, nullptr);
        vkDestroyCommandPool(devicePtr->getDevice(), commandPools.compute, nullptr);
        vkDestroyCommandPool(devicePtr->getDevice(), commandPools.transfer, nullptr);
        vkDestroyCommandPool(devicePtr->getDevice(), commandPools.present, nullptr);
    }
}