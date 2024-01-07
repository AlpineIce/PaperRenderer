#pragma once
#include "vulkan/vulkan.hpp"
#include "Device.h"

#include <vector>

namespace Renderer
{
    struct CommandPools
    {
        VkCommandPool graphics;
        VkCommandPool compute;
        VkCommandPool transfer;
        VkCommandPool present;
    };

    struct CommandBuffers
    {
        std::vector<VkCommandBuffer> graphics;
        std::vector<VkCommandBuffer> compute;
        std::vector<VkCommandBuffer> transfer;
        std::vector<VkCommandBuffer> present;
    };

    class Commands
    {
    private:
        CommandPools commandPools;
        CommandBuffers commandBuffers;
        static const uint32_t frameCount = 2;

        Device* devicePtr;
    public:
        Commands(Device* device);
        ~Commands();

        CommandBuffers* getCommandBuffersPtr() { return &commandBuffers; }
        CommandPools getCommandPools() const { return commandPools; }
        static uint32_t getFrameCount() { return frameCount; }
        
    };
}