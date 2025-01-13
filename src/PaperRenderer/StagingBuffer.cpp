#include "StagingBuffer.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    RendererStagingBuffer::RendererStagingBuffer(RenderEngine& renderer)
        :renderer(renderer)
    {
        //log constructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "A RendererStagingBuffer was created"
        });
    }

    RendererStagingBuffer::~RendererStagingBuffer()
    {
        stagingBuffer.reset();

        //log destructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "A RendererStagingBuffer was destroyed"
        });
    }

    void RendererStagingBuffer::idleBuffer()
    {
        if(stagingBuffer) stagingBuffer->idleOwners();
        stackLocation = 0;
    }

    void RendererStagingBuffer::queueDataTransfers(const Buffer &dstBuffer, VkDeviceSize dstOffset, const std::vector<char> &data)
    {
        //lock mutex
        std::lock_guard guard(stagingBufferMutex);

        //push transfer to queue
        transferQueues[&dstBuffer].emplace_front(
            dstOffset,
            data
        );
        queueSize += data.size();
    }

    std::vector<RendererStagingBuffer::DstCopy> RendererStagingBuffer::getDataTransfers()
    {
        //lock mutex
        std::lock_guard guard(stagingBufferMutex);

        //rebuild buffer if needed
        VkDeviceSize availableSize = stagingBuffer ? stagingBuffer->getSize() : 0;
        if(stackLocation + queueSize > availableSize)
        {
            const BufferInfo bufferInfo = {
                .size = (VkDeviceSize)((stackLocation + queueSize) * bufferOverhead),
                .usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR,
                .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
            };
            stagingBuffer = std::make_unique<Buffer>(renderer, bufferInfo);

            availableSize = bufferInfo.size;
        }

        //fill in the staging buffer with queued transfers
        std::vector<DstCopy> dstCopies;
        for(auto& [buffer, transfers] : transferQueues)
        {
            for(const QueuedTransfer& transfer : transfers)
            {
                //buffer write
                BufferWrite bufferWrite = {
                    .offset = stackLocation,
                    .size = transfer.data.size(),
                    .readData = (char const*)transfer.data.data()
                };

                //fill staging buffer           
                stagingBuffer->writeToBuffer({ bufferWrite });

                //push VkBufferCopy
                VkBufferCopy bufferCopyInfo = {
                    .srcOffset = bufferWrite.offset,
                    .dstOffset = transfer.dstOffset,
                    .size = bufferWrite.size
                };

                dstCopies.emplace_back(
                    *buffer,
                    bufferCopyInfo
                );

                stackLocation += bufferWrite.size;
            }
            transfers.clear();
        }
        queueSize = 0;

        return dstCopies;
    }

    void RendererStagingBuffer::submitQueuedTransfers(VkCommandBuffer cmdBuffer)
    {
        //timer
        Timer timer(renderer, "Record Queued Transfers (StagingBuffer)", REGULAR);

        //copy to dst
        for(const auto& copy : getDataTransfers())
        {
            vkCmdCopyBuffer(cmdBuffer, stagingBuffer->getBuffer(), copy.dstBuffer.getBuffer(), 1, &copy.copyInfo);
        }
    }

    const Queue& RendererStagingBuffer::submitQueuedTransfers(SynchronizationInfo syncInfo)
    {
        //timer
        Timer timer(renderer, "Submit Queued Transfers (StagingBuffer)", REGULAR);

        //start command buffer
        VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(syncInfo.queueType);

        const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = NULL
        };
        vkBeginCommandBuffer(cmdBuffer, &beginInfo);

        submitQueuedTransfers(cmdBuffer);

        //end command buffer
        vkEndCommandBuffer(cmdBuffer);

        renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

        //submit
        const Queue& queue = renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer });

        //add owner
        if(stagingBuffer) stagingBuffer->addOwner(queue);

        return queue;
    }

    void RendererStagingBuffer::addOwner(const Queue &queue)
    {
        std::lock_guard guard(stagingBufferMutex);
        if(stagingBuffer) stagingBuffer->addOwner(queue);
    }
}