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
        };

        std::unordered_map<Buffer const*, std::deque<QueuedTransfer>> transferQueues;
        VkDeviceSize queueSize = 0;
        VkDeviceSize stackLocation = 0;

        struct DstCopy
        {
            const Buffer& dstBuffer;
            VkBufferCopy copyInfo;
        };
        std::vector<DstCopy> getDataTransfers();

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