#pragma once
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

    enum CmdPoolType
    {
        GRAPHICS = 0,
        COMPUTE = 1,
        TRANSFER = 2,
        PRESENT = 3
    };

    class CmdBufferAllocator
    {
    private:
        CommandPools commandPools;
        static const uint32_t frameCount = 2;

        Device* devicePtr;

        void createCommandPools();
        
    public:
        CmdBufferAllocator(Device* device);
        ~CmdBufferAllocator();

        VkSemaphore getSemaphore() const;
        VkFence getSignaledFence() const;
        VkFence getUnsignaledFence() const;
        VkCommandBuffer getCommandBuffer(CmdPoolType type) const;
        static uint32_t getFrameCount() { return frameCount; }
    };
}