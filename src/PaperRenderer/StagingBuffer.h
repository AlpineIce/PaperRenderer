#pragma once
#include "VulkanResources.h"

namespace PaperRenderer
{
    class RendererStagingBuffer
    {
    private:
        std::recursive_mutex stagingBufferMutex;
        Buffer stagingBuffer;
        const float bufferOverhead = 1.5f;

        struct QueuedTransfer
        {
            VkDeviceSize dstOffset = 0;
            std::vector<uint8_t> data = {};
            const Buffer& dstBuffer;
        };

        std::deque<QueuedTransfer> transferQueue = {};
        VkDeviceSize queueSize = 0;
        VkDeviceSize stackLocation = 0;

        std::set<Buffer*> submitQueuedTransfers(VkCommandBuffer cmdBuffer); //records all queued transfers and clears the queue

        class RenderEngine* renderer;

    public:
        RendererStagingBuffer(RenderEngine& renderer);
        ~RendererStagingBuffer();
        RendererStagingBuffer(const RendererStagingBuffer&) = delete;
        RendererStagingBuffer(RendererStagingBuffer&& other) noexcept;
        
        void resetBuffer();
        const Queue& submitQueuedTransfers(const SynchronizationInfo& syncInfo); //Submits all queued transfers and clears the queue

        //thread safe
        template<typename T>
        void queueDataTransfers(const Buffer& dstBuffer, VkDeviceSize dstOffset, const std::vector<T>& data) //do not submit more than 1 transfer with the same destination! undefined behavior!
        {
            //lock mutex
            std::lock_guard guard(stagingBufferMutex);

            //transform data
            std::vector<uint8_t> transferData(data.size() * sizeof(T));
            memcpy(transferData.data(), data.data(), transferData.size());

            //push transfer to queue
            transferQueue.emplace_front(
                dstOffset,
                std::move(transferData),
                dstBuffer
            );
            queueSize += data.size() * sizeof(T);
        }

        //thread safe
        template<typename T>
        void queueDataTransfers(const Buffer& dstBuffer, VkDeviceSize dstOffset, const T& data)
        {
            //lock mutex
            std::lock_guard guard(stagingBufferMutex);

            //create vector of one element
            std::vector<uint8_t> transferData(sizeof(T));
            memcpy(transferData.data(), &data, transferData.size());

            //push transfer to queue
            transferQueue.emplace_front(
                dstOffset,
                std::move(transferData),
                dstBuffer
            );
            queueSize += sizeof(T);
        }
    };
}