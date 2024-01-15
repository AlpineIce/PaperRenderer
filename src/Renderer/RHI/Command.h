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

    struct QueueReturn
    {
        std::shared_ptr<VkFence> fence;
        VkResult result;
        VkCommandPool pool;
        std::vector<VkCommandBuffer> commandBuffers;
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
        VkFence getSignaledFence();
        VkFence getUnsignaledFence();
    public:
        CmdBufferAllocator(Device* device);
        ~CmdBufferAllocator();

        QueueReturn submitQueue(const VkSubmitInfo& submitInfo, CmdPoolType poolType);
        QueueReturn submitPresentQueue(const VkPresentInfoKHR& submitInfo);
        void waitForQueue(QueueReturn& info);

        VkCommandBuffer getCommandBuffer(CmdPoolType type);
        static uint32_t getFrameCount() { return frameCount; }
    };
}