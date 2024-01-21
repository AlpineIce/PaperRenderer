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

    enum CmdPoolType
    {
        GRAPHICS = 0,
        COMPUTE = 1,
        TRANSFER = 2,
        PRESENT = 3
    };

    struct QueuePrivateData //i dont know how move semantics work
    {
        Device* devicePtr;
        VkResult result;
        VkFence fence;
        VkCommandPool pool;
        std::vector<VkCommandBuffer> commandBuffers;
        std::vector<VkSemaphore> semaphores;
    };

    class QueueReturn
    {
    private:
        QueuePrivateData pData;

        void shallowCopy(QueuePrivateData& data);

    public:
        QueueReturn(QueuePrivateData pData);

        QueueReturn(QueueReturn&& queueReturn);
        ~QueueReturn();

        void moveInvalidate();
        void waitForFence();
        std::vector<VkSemaphore> getSemaphores() const { return pData.semaphores; }
        VkResult getSubmitResult() const { return pData.result; }

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

        VkSemaphore getSemaphore();
        QueueReturn submitQueue(const VkSubmitInfo& submitInfo, CmdPoolType poolType, bool useFence);
        QueueReturn submitPresentQueue(const VkPresentInfoKHR& submitInfo);

        VkCommandBuffer getCommandBuffer(CmdPoolType type);
        static uint32_t getFrameCount() { return frameCount; }
    };
}