#pragma once
#include "VulkanResources.h"

namespace PaperRenderer
{
    struct StagingBufferTransfer
    {
        VkDeviceSize dstOffset = 0;
        std::vector<uint8_t> data = {};
        Buffer* dstBuffer = NULL;
        std::function<void(const Buffer& srcBuffer, const VkDeviceSize srcOffset)> postWriteOp = NULL;
    };

    class RendererStagingBuffer
    {
    private:
        std::recursive_mutex stagingBufferMutex;
        Buffer stagingBuffer;
        static constexpr float bufferOverhead = 1.5f;

        VkDeviceSize stackLocation = 0;

        void verifyBufferSize(std::vector<StagingBufferTransfer>& transfers);

        class RenderEngine* renderer;
        class Queue* gpuQueue;

    public:
        RendererStagingBuffer(RenderEngine& renderer, Queue& queue);
        ~RendererStagingBuffer();
        RendererStagingBuffer(const RendererStagingBuffer&) = delete;
        RendererStagingBuffer(RendererStagingBuffer&& other) noexcept;
        
        void idle();
        void resetBuffer();
        Queue& submitTransfers(std::vector<StagingBufferTransfer>& transfers, const SynchronizationInfo& syncInfo);
    };
}