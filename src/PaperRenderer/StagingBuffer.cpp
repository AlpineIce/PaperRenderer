#include "StagingBuffer.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    RendererStagingBuffer::RendererStagingBuffer(RenderEngine& renderer, Queue& queue)
        :stagingBuffer(renderer, {}),
        renderer(&renderer),
        gpuQueue(&queue)
    {
        //log constructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "A RendererStagingBuffer was created"
        });
    }

    RendererStagingBuffer::~RendererStagingBuffer()
    {
        //log destructor
        renderer->getLogger().recordLog({
            .type = INFO,
            .text = "A RendererStagingBuffer was destroyed"
        });
    }

    RendererStagingBuffer::RendererStagingBuffer(RendererStagingBuffer&& other) noexcept
        :stagingBuffer(std::move(other.stagingBuffer)),
        transferQueue(other.transferQueue),
        queueSize(other.queueSize),
        stackLocation(other.stackLocation),
        renderer(other.renderer),
        gpuQueue(other.gpuQueue)
    {
        std::lock_guard guard(stagingBufferMutex);
        other.queueSize = 0;
        other.stackLocation = 0;

        renderer->getLogger().recordLog({
            .type = INFO,
            .text = "A RendererStagingBuffer was moved"
        });
    }

    void RendererStagingBuffer::resetBuffer()
    {
        stackLocation = 0;
    }

    std::set<Buffer*> RendererStagingBuffer::submitQueuedTransfers(VkCommandBuffer cmdBuffer)
    {
        //timer
        Timer timer(*renderer, "Record Queued Transfers (StagingBuffer)", REGULAR);

        //lock mutex
        std::lock_guard guard(stagingBufferMutex);

        //rebuild buffer if needed
        VkDeviceSize availableSize = stagingBuffer.getSize();
        if(stackLocation + queueSize > availableSize)
        {
            const BufferInfo bufferInfo = {
                .size = (VkDeviceSize)((stackLocation + queueSize) * bufferOverhead),
                .usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR,
                .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
            };
            stagingBuffer = Buffer(*renderer, bufferInfo);

            availableSize = bufferInfo.size;
        }

        //keep track of dst buffers
        std::set<Buffer*> dstBuffers;

        //copy to dst
        for(QueuedTransfer& transfer : transferQueue)
        {
            //make sure dstBuffer is referenced
            dstBuffers.insert(transfer.dstBuffer);

            //buffer write
            const BufferWrite bufferWrite = {
                .offset = stackLocation,
                .size = transfer.data.size(),
                .readData = transfer.data.data()
            };        
            stagingBuffer.writeToBuffer({ bufferWrite });

            //push VkBufferCopy
            const VkBufferCopy copy = {
                .srcOffset = bufferWrite.offset,
                .dstOffset = transfer.dstOffset,
                .size = bufferWrite.size
            };

            //record copy command
            vkCmdCopyBuffer(cmdBuffer, stagingBuffer.getBuffer(), transfer.dstBuffer->getBuffer(), 1, &copy);

            //increment stack
            stackLocation += bufferWrite.size;
        }

        //clear queue
        transferQueue.clear();
        queueSize = 0;

        //return collection of dst buffers
        return dstBuffers;
    }

    Queue& RendererStagingBuffer::submitQueuedTransfers(const SynchronizationInfo& syncInfo)
    {
        //start command buffer
        CommandBuffer cmdBuffer(renderer->getDevice().getCommands(), TRANSFER);

        const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = NULL
        };
        vkBeginCommandBuffer(cmdBuffer, &beginInfo);

        std::set<Buffer*> dstBuffers = submitQueuedTransfers(cmdBuffer);

        //end command buffer
        vkEndCommandBuffer(cmdBuffer);

        //submit
        renderer->getDevice().getCommands().submitToQueue(*gpuQueue, syncInfo, { cmdBuffer });

        //add owners
        for(Buffer* buffer : dstBuffers)
        {
            buffer->addOwner(*gpuQueue);
        }
        stagingBuffer.addOwner(*gpuQueue);
        
        return *gpuQueue;
    }
}