#pragma once
#include "VulkanResources.h"

namespace PaperRenderer
{
    class RendererStagingBuffer
    {
    private:
        std::recursive_mutex stagingBufferMutex;
        std::unique_ptr<Buffer> stagingBuffer;
        const float bufferOverhead = 1.5f;

        struct QueuedTransfer
        {
            VkDeviceSize dstOffset;
            std::vector<char> data;
            const Buffer& dstBuffer;
        };

        std::deque<QueuedTransfer> transferQueue;
        VkDeviceSize queueSize = 0;
        VkDeviceSize stackLocation = 0;

        class RenderEngine& renderer;

    public:
        RendererStagingBuffer(RenderEngine& renderer);
        ~RendererStagingBuffer();
        
        void idleBuffer();
        void queueDataTransfers(const Buffer& dstBuffer, VkDeviceSize dstOffset, const std::vector<char>& data); //do not submit more than 1 transfer with the same destination! undefined behavior!
        void submitQueuedTransfers(VkCommandBuffer cmdBuffer); //records all queued transfers and clears the queue
        const Queue& submitQueuedTransfers(SynchronizationInfo syncInfo); //Submits all queued transfers and clears the queue
        void addOwner(const Queue& queue);
    };
}