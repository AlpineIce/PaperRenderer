#include "Buffer.h"

namespace Renderer
{
    //-----------BASE BUFFER DEFINITIONS----------//

    Buffer::Buffer(Device* device, CmdBufferAllocator* commands, VkDeviceSize size)
        :devicePtr(device),
        commandsPtr(commands),
        size(size),
        buffer(VK_NULL_HANDLE)
    {
        /*      DEVELOPMENT CODE
        DeviceAllocationInfo allocationInfo = {};
        allocationInfo.allocationSize = 65536;
        allocationInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        DeviceAllocation allocation(devicePtr, allocationInfo);*/
    }

    Buffer::~Buffer()
    {
        if(buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(devicePtr->getAllocator(), buffer, allocation);
        }
    }

    void Buffer::createBuffer(VkBufferUsageFlags usage, VmaMemoryUsage memUsage, VmaAllocationCreateFlags memFlag)
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = NULL;
        bufferInfo.flags = 0;
        bufferInfo.size = this->size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.queueFamilyIndexCount = 0;
        bufferInfo.pQueueFamilyIndices = NULL;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = memFlag;
        allocCreateInfo.usage = memUsage;

        VkResult result = vmaCreateBuffer(devicePtr->getAllocator(), &bufferInfo, &allocCreateInfo, &buffer, &allocation, &allocInfo);
    }

    CommandBuffer Buffer::copyFromBuffer(Buffer &src, const std::vector<SemaphorePair>& waitPairs, const std::vector<SemaphorePair>& signalPairs, const VkFence& fence)
    {
        if(this->size <= src.getAllocatedSize())
        {
            return copyBuffer(src.getBuffer(), this->buffer, this->size, waitPairs, signalPairs, fence);
        }
        else
        {
            return copyBuffer(src.getBuffer(), this->buffer, src.getAllocatedSize(), waitPairs, signalPairs, fence);
        }
    }

    CommandBuffer Buffer::copyFromBufferRanges(Buffer &src, const std::vector<SemaphorePair> &waitPairs, const std::vector<SemaphorePair> &signalPairs, const VkFence &fence, const std::vector<VkBufferCopy>& regions)
    {
        VkCommandBuffer transferBuffer = commandsPtr->getCommandBuffer(CmdPoolType::TRANSFER); //note theres only 1 transfer cmd buffer

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(transferBuffer, &beginInfo); 

        vkCmdCopyBuffer(transferBuffer, src.getBuffer(), this->buffer, regions.size(), regions.data());

        vkEndCommandBuffer(transferBuffer);

        VkCommandBufferSubmitInfo cmdBufferSubmitInfo = {};
        cmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdBufferSubmitInfo.pNext = NULL;
        cmdBufferSubmitInfo.commandBuffer = transferBuffer;
        cmdBufferSubmitInfo.deviceMask = 0;

        std::vector<VkSemaphoreSubmitInfo> semaphoreWaitInfos;
        for(const SemaphorePair& pair : waitPairs)
        {
            VkSemaphoreSubmitInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.semaphore = pair.semaphore;
            semaphoreInfo.stageMask = pair.stage;
            semaphoreInfo.deviceIndex = 0;

            semaphoreWaitInfos.push_back(semaphoreInfo);
        }

        std::vector<VkSemaphoreSubmitInfo> semaphoreSignalInfos;
        for(const SemaphorePair& pair : signalPairs)
        {
            VkSemaphoreSubmitInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.semaphore = pair.semaphore;
            semaphoreInfo.stageMask = pair.stage;
            semaphoreInfo.deviceIndex = 0;

            semaphoreSignalInfos.push_back(semaphoreInfo);
        }
        
        VkSubmitInfo2 submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.pNext = NULL;
        submitInfo.flags = 0;
        submitInfo.waitSemaphoreInfoCount = semaphoreWaitInfos.size();
        submitInfo.pWaitSemaphoreInfos = semaphoreWaitInfos.data();
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdBufferSubmitInfo;
        submitInfo.signalSemaphoreInfoCount = semaphoreSignalInfos.size();
        submitInfo.pSignalSemaphoreInfos = semaphoreSignalInfos.data();

        vkQueueSubmit2(devicePtr->getQueues().transfer.at(0), 1, &submitInfo, fence);
        
        return { transferBuffer, TRANSFER };
    }

    CommandBuffer Buffer::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size, const std::vector<SemaphorePair>& waitPairs, const std::vector<SemaphorePair>& signalPairs, const VkFence& fence)
    {
        VkCommandBuffer transferBuffer = commandsPtr->getCommandBuffer(CmdPoolType::TRANSFER); //note theres only 1 transfer cmd buffer

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(transferBuffer, &beginInfo); 

        VkBufferCopy copyRegion = {};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;

        vkCmdCopyBuffer(transferBuffer, src, dst, 1, &copyRegion);

        vkEndCommandBuffer(transferBuffer);

        VkCommandBufferSubmitInfo cmdBufferSubmitInfo = {};
        cmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdBufferSubmitInfo.pNext = NULL;
        cmdBufferSubmitInfo.commandBuffer = transferBuffer;
        cmdBufferSubmitInfo.deviceMask = 0;

        std::vector<VkSemaphoreSubmitInfo> semaphoreWaitInfos;
        for(const SemaphorePair& pair : waitPairs)
        {
            VkSemaphoreSubmitInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.semaphore = pair.semaphore;
            semaphoreInfo.stageMask = pair.stage;
            semaphoreInfo.deviceIndex = 0;

            semaphoreWaitInfos.push_back(semaphoreInfo);
        }

        std::vector<VkSemaphoreSubmitInfo> semaphoreSignalInfos;
        for(const SemaphorePair& pair : signalPairs)
        {
            VkSemaphoreSubmitInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.semaphore = pair.semaphore;
            semaphoreInfo.stageMask = pair.stage;
            semaphoreInfo.deviceIndex = 0;

            semaphoreSignalInfos.push_back(semaphoreInfo);
        }
        
        VkSubmitInfo2 submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.pNext = NULL;
        submitInfo.flags = 0;
        submitInfo.waitSemaphoreInfoCount = semaphoreWaitInfos.size();
        submitInfo.pWaitSemaphoreInfos = semaphoreWaitInfos.data();
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdBufferSubmitInfo;
        submitInfo.signalSemaphoreInfoCount = semaphoreSignalInfos.size();
        submitInfo.pSignalSemaphoreInfos = semaphoreSignalInfos.data();

        vkQueueSubmit2(devicePtr->getQueues().transfer.at(0), 1, &submitInfo, fence);
        
        return { transferBuffer, TRANSFER };
    }

    VkDeviceAddress Buffer::getBufferDeviceAddress() const
    {
        VkBufferDeviceAddressInfo deviceAddressInfo = {};
        deviceAddressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        deviceAddressInfo.pNext = NULL;
        deviceAddressInfo.buffer = buffer;

        return vkGetBufferDeviceAddress(devicePtr->getDevice(), &deviceAddressInfo);
    }

    //-----------STAGING BUFFER DEFINITIONS----------//

    StagingBuffer::StagingBuffer(Device* device, CmdBufferAllocator* commands, VkDeviceSize size)
        :Buffer(device, commands, size)
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = NULL;
        bufferInfo.flags = 0;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.queueFamilyIndexCount = 0;
        bufferInfo.pQueueFamilyIndices = NULL;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VkResult result = vmaCreateBuffer(devicePtr->getAllocator(), &bufferInfo, &allocCreateInfo, &buffer, &allocation, &allocInfo);
        vmaMapMemory(devicePtr->getAllocator(), allocation, &(allocInfo.pMappedData));
    }

    StagingBuffer::~StagingBuffer()
    {
        vmaUnmapMemory(devicePtr->getAllocator(), allocation);
    }

    void StagingBuffer::mapData(void *data, VkDeviceSize bytesOffset, VkDeviceSize size)
    {
        memcpy((char*)allocInfo.pMappedData + bytesOffset, data, size);
        vmaFlushAllocation(devicePtr->getAllocator(), allocation, bytesOffset, size);
    }

    //-----------VERTEX BUFFER DEFINITIONS----------//

    VertexBuffer::VertexBuffer(Device* device, CmdBufferAllocator* commands, std::vector<Vertex>* vertices)
        :Buffer(device, commands, vertices->size() * sizeof(Vertex)),
        verticesLength(vertices->size())
    {
        StagingBuffer stagingBuffer(devicePtr, commandsPtr, this->size);
        stagingBuffer.mapData(vertices->data(), 0, this->size);

        createVertexBuffer();

        VkFence fence = commandsPtr->getUnsignaledFence();
        CommandBuffer cmdBuffer = copyBuffer(stagingBuffer.getBuffer(), this->buffer, this->size, std::vector<SemaphorePair>(), std::vector<SemaphorePair>(), fence);
        vkWaitForFences(devicePtr->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(devicePtr->getDevice(), fence, nullptr);
        commandsPtr->freeCommandBuffer(cmdBuffer);
    }

    VertexBuffer::~VertexBuffer()
    {
    }

    void VertexBuffer::createVertexBuffer()
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = NULL;
        bufferInfo.flags = 0;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.queueFamilyIndexCount = 0;
        bufferInfo.pQueueFamilyIndices = NULL;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VkResult result = vmaCreateBuffer(devicePtr->getAllocator(), &bufferInfo, &allocCreateInfo, &buffer, &allocation, &allocInfo);
    }

    //----------INDEX BUFFER DEFINITIONS----------//

    IndexBuffer::IndexBuffer(Device* device, CmdBufferAllocator* commands, std::vector<uint32_t>* indices)
        :Buffer(device, commands, indices->size() * sizeof(uint32_t)),
        indicesLength(indices->size())
    {
        StagingBuffer stagingBuffer(devicePtr, commandsPtr, this->size);
        stagingBuffer.mapData(indices->data(), 0, this->size);

        createIndexBuffer();

        VkFence fence = commandsPtr->getUnsignaledFence();
        CommandBuffer cmdBuffer = copyBuffer(stagingBuffer.getBuffer(), this->buffer, this->size, std::vector<SemaphorePair>(), std::vector<SemaphorePair>(), fence);
        vkWaitForFences(devicePtr->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(devicePtr->getDevice(), fence, nullptr);
        commandsPtr->freeCommandBuffer(cmdBuffer);
    }

    IndexBuffer::~IndexBuffer()
    {
    }

    void IndexBuffer::createIndexBuffer()
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = NULL;
        bufferInfo.flags = 0;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.queueFamilyIndexCount = 0;
        bufferInfo.pQueueFamilyIndices = NULL;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VkResult result = vmaCreateBuffer(devicePtr->getAllocator(), &bufferInfo, &allocCreateInfo, &buffer, &allocation, &allocInfo);
    }

    //----------TEXTURE "BUFFER" DEFINITIONS----------//

    Texture::Texture(Device* device, CmdBufferAllocator* commands, Image* imageData)
        :devicePtr(device),
        commandsPtr(commands),
        size(imageData->size)
    {
        StagingBuffer stagingBuffer(devicePtr, commandsPtr, this->size);
        stagingBuffer.mapData(imageData->data, 0, this->size);

        createTexture(imageData);

        VkSemaphore layoutChangeSemaphore = changeImageLayout(texture, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkSemaphore copySemaphore = copyBufferToImage(stagingBuffer.getBuffer(), texture, imageData, layoutChangeSemaphore);
        
        generateMipmaps(imageData, copySemaphore);
        vkDestroySemaphore(devicePtr->getDevice(), layoutChangeSemaphore, nullptr);
        vkDestroySemaphore(devicePtr->getDevice(), copySemaphore, nullptr);
        createTextureView();
        createSampler();

        for(CommandBuffer& buffer : creationBuffers)
        {
            commandsPtr->freeCommandBuffer(buffer);
        }
        creationBuffers.clear();
    }

    Texture::~Texture()
    {
        vkDestroySampler(devicePtr->getDevice(), sampler, nullptr);
        vkDestroyImageView(devicePtr->getDevice(), textureView, nullptr);
        vmaDestroyImage(devicePtr->getAllocator(), texture, allocation);
    }

    VkSemaphore Texture::changeImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        VkCommandBuffer transferBuffer = commandsPtr->getCommandBuffer(CmdPoolType::TRANSFER); //note theres only 1 transfer cmd buffer

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(transferBuffer, &beginInfo); 

        VkImageSubresourceRange subresource = {};
        subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.baseMipLevel = 0;
        subresource.levelCount = 1;
        subresource.baseArrayLayer = 0;
        subresource.layerCount = 1;
        
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = subresource;

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } 
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } 
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } 
        else
        {
            throw std::invalid_argument("unsupported layout transition!");
        }

        vkCmdPipelineBarrier(
            transferBuffer,
            sourceStage, destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        vkEndCommandBuffer(transferBuffer);

        VkCommandBufferSubmitInfo cmdBufferSubmitInfo = {};
        cmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdBufferSubmitInfo.pNext = NULL;
        cmdBufferSubmitInfo.commandBuffer = transferBuffer;
        cmdBufferSubmitInfo.deviceMask = 0;

        VkSemaphore signalSemaphore = commandsPtr->getSemaphore();
        VkSemaphoreSubmitInfo semaphoreSignalInfo = {};
        semaphoreSignalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        semaphoreSignalInfo.pNext = NULL;
        semaphoreSignalInfo.semaphore = signalSemaphore;
        semaphoreSignalInfo.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        semaphoreSignalInfo.deviceIndex = 0;
        
        VkSubmitInfo2 submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.pNext = NULL;
        submitInfo.flags = 0;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdBufferSubmitInfo;
        submitInfo.signalSemaphoreInfoCount = 1;
        submitInfo.pSignalSemaphoreInfos = &semaphoreSignalInfo;

        vkQueueSubmit2(devicePtr->getQueues().transfer.at(0), 1, &submitInfo, VK_NULL_HANDLE);
        creationBuffers.push_back({transferBuffer, TRANSFER});

        return signalSemaphore;
    }

    VkSemaphore Texture::copyBufferToImage(VkBuffer src, VkImage dst, Image* imageData, const VkSemaphore& waitSemaphore)
    {
        VkCommandBuffer transferBuffer = commandsPtr->getCommandBuffer(CmdPoolType::TRANSFER);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(transferBuffer, &beginInfo); //note theres only 1 transfer cmd buffer

        VkExtent3D imageExtent = {};
        imageExtent.width = imageData->width;
        imageExtent.height = imageData->height;
        imageExtent.depth = 1;

        VkImageSubresourceLayers subresource = {};
        subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.mipLevel = 0;
        subresource.baseArrayLayer = 0;
        subresource.layerCount = 1;

        VkBufferImageCopy copyRegion = {};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource = subresource;
        copyRegion.imageOffset = {0};
        copyRegion.imageExtent = imageExtent;

        vkCmdCopyBufferToImage(transferBuffer, src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        vkEndCommandBuffer(transferBuffer);

        VkSemaphoreSubmitInfo semaphoreWaitInfo = {};
        semaphoreWaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        semaphoreWaitInfo.pNext = NULL;
        semaphoreWaitInfo.semaphore = waitSemaphore;
        semaphoreWaitInfo.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        semaphoreWaitInfo.deviceIndex = 0;
        
        VkCommandBufferSubmitInfo cmdBufferSubmitInfo = {};
        cmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdBufferSubmitInfo.pNext = NULL;
        cmdBufferSubmitInfo.commandBuffer = transferBuffer;
        cmdBufferSubmitInfo.deviceMask = 0;

        VkSemaphore signalSemaphore = commandsPtr->getSemaphore();
        VkSemaphoreSubmitInfo semaphoreSignalInfo = {};
        semaphoreSignalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        semaphoreSignalInfo.pNext = NULL;
        semaphoreSignalInfo.semaphore = signalSemaphore;
        semaphoreSignalInfo.stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        semaphoreSignalInfo.deviceIndex = 0;
        
        VkSubmitInfo2 submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.pNext = NULL;
        submitInfo.flags = 0;
        submitInfo.waitSemaphoreInfoCount = 1;
        submitInfo.pWaitSemaphoreInfos = &semaphoreWaitInfo;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdBufferSubmitInfo;
        submitInfo.signalSemaphoreInfoCount = 1;
        submitInfo.pSignalSemaphoreInfos = &semaphoreSignalInfo;

        vkQueueSubmit2(devicePtr->getQueues().transfer.at(0), 1, &submitInfo, VK_NULL_HANDLE);
        creationBuffers.push_back({transferBuffer, TRANSFER});

        return signalSemaphore;
    }
    
    void Texture::createTexture(Image* imageData)
    {
        VkExtent3D imageExtent;
        imageExtent.width = imageData->width;
        imageExtent.height = imageData->height;
        imageExtent.depth = 1;

        mipmapLevels = std::floor(std::log2(std::max(imageData->width, imageData->height))) + 1;

        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.pNext = NULL;
        imageInfo.flags = 0;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.extent = imageExtent;
        imageInfo.mipLevels = mipmapLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        uint32_t queueFamilies[] = {(uint32_t)devicePtr->getQueueFamilies().graphicsFamilyIndex,
                                    (uint32_t)devicePtr->getQueueFamilies().transferFamilyIndex};
        if(queueFamilies[0] == queueFamilies[1])
        {
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.queueFamilyIndexCount = 0;
            imageInfo.pQueueFamilyIndices = nullptr;
        }
        else
        {
            imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
            imageInfo.queueFamilyIndexCount = 2;
            imageInfo.pQueueFamilyIndices = queueFamilies;
        }

        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VkResult result = vmaCreateImage(devicePtr->getAllocator(), &imageInfo, &allocCreateInfo, &texture, &allocation, &allocInfo);
    }

    void Texture::generateMipmaps(Image* imageData, const VkSemaphore& waitSemaphore)
    {
        VkCommandBuffer blitBuffer = commandsPtr->getCommandBuffer(CmdPoolType::GRAPHICS);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(blitBuffer, &beginInfo); 

        injectMemBarrier(
            blitBuffer,
            texture,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 1
        );

        for(uint32_t i = 1; i < mipmapLevels; i++)
        {
            VkImageSubresourceRange subresource = {};
            subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresource.baseMipLevel = i - 1;
            subresource.levelCount = 1;
            subresource.baseArrayLayer = 0;
            subresource.layerCount = 1;
            
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture;
            barrier.subresourceRange = subresource;

            injectMemBarrier(
                blitBuffer,
                texture,
                0,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                i, 1
            );


            VkImageBlit imageBlit = {};
            imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBlit.srcSubresource.layerCount = 1;
            imageBlit.srcSubresource.mipLevel   = i - 1;
            imageBlit.srcOffsets[1].x           = int32_t(imageData->width >> (i - 1));
            imageBlit.srcOffsets[1].y           = int32_t(imageData->height >> (i - 1));
            imageBlit.srcOffsets[1].z           = 1;
            imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBlit.dstSubresource.layerCount = 1;
            imageBlit.dstSubresource.mipLevel   = i;
            imageBlit.dstOffsets[1].x           = int32_t(imageData->width >> i);
            imageBlit.dstOffsets[1].y           = int32_t(imageData->height >> i);
            imageBlit.dstOffsets[1].z           = 1;

            vkCmdBlitImage(blitBuffer,
                texture,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                texture,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &imageBlit,
                VK_FILTER_LINEAR);

            injectMemBarrier(
                blitBuffer,
                texture,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                i, 1
            );
        }

        injectMemBarrier(
            blitBuffer,
            texture,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, mipmapLevels
        );
        

        vkEndCommandBuffer(blitBuffer);

        VkSemaphoreSubmitInfo semaphoreWaitInfo = {};
        semaphoreWaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        semaphoreWaitInfo.pNext = NULL;
        semaphoreWaitInfo.semaphore = waitSemaphore;
        semaphoreWaitInfo.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        semaphoreWaitInfo.deviceIndex = 0;
        
        VkCommandBufferSubmitInfo cmdBufferSubmitInfo = {};
        cmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdBufferSubmitInfo.pNext = NULL;
        cmdBufferSubmitInfo.commandBuffer = blitBuffer;
        cmdBufferSubmitInfo.deviceMask = 0;
        
        VkSubmitInfo2 submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.pNext = NULL;
        submitInfo.flags = 0;
        submitInfo.waitSemaphoreInfoCount = 1;
        submitInfo.pWaitSemaphoreInfos = &semaphoreWaitInfo;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdBufferSubmitInfo;

        VkFence fence = commandsPtr->getUnsignaledFence();
        vkQueueSubmit2(devicePtr->getQueues().graphics.at(0), 1, &submitInfo, fence);
        vkWaitForFences(devicePtr->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(devicePtr->getDevice(), fence, nullptr);
    }

    void Texture::createTextureView()
    {
        VkImageSubresourceRange subresource = {};
        subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.baseMipLevel = 0;
        subresource.levelCount = mipmapLevels;
        subresource.baseArrayLayer = 0;
        subresource.layerCount = 1;

        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.pNext = NULL;
        viewInfo.flags = 0;
        viewInfo.image = texture;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange = subresource;

        VkResult result = vkCreateImageView(devicePtr->getDevice(), &viewInfo, nullptr, &textureView);
    }

    void Texture::createSampler()
    {
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.pNext = 0;
        samplerInfo.flags = 0;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = devicePtr->getGPUFeatures().samplerAnisotropy;
        samplerInfo.maxAnisotropy = devicePtr->getGPUProperties().properties.limits.maxSamplerAnisotropy;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = mipmapLevels;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        VkResult result = vkCreateSampler(devicePtr->getDevice(), &samplerInfo, nullptr, &sampler);
    }

    void Texture::injectMemBarrier(
        const VkCommandBuffer &command,
        const VkImage &image,
        VkAccessFlags srcAccess,
        VkAccessFlags dstAccess,
        VkImageLayout srcLayout,
        VkImageLayout dstLayout,
        VkPipelineStageFlags srcMask,
        VkPipelineStageFlags dstMask, 
        uint32_t baseMipLevel,
        uint32_t levels)
    {
        VkImageSubresourceRange subresource = {};
        subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.baseMipLevel = baseMipLevel;
        subresource.levelCount = levels;
        subresource.baseArrayLayer = 0;
        subresource.layerCount = 1;
        
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = srcLayout;
        barrier.newLayout = dstLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = subresource;

        vkCmdPipelineBarrier(
            command,
            srcMask, 
            dstMask,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
    }

    //----------UNIFORM BUFFER DEFINITIONS----------//

    UniformBuffer::UniformBuffer(Device *device, CmdBufferAllocator *commands, VkDeviceSize size)
        :Buffer(device, commands, size),
        dataPtr(NULL)
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = NULL;
        bufferInfo.flags = 0;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.queueFamilyIndexCount = 0;
        bufferInfo.pQueueFamilyIndices = NULL;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VmaAllocationInfo allocInfo;
        VkResult result = vmaCreateBuffer(devicePtr->getAllocator(), &bufferInfo, &allocCreateInfo, &buffer, &allocation, &allocInfo);
        vmaMapMemory(devicePtr->getAllocator(), allocation, &dataPtr);
    }

    UniformBuffer::~UniformBuffer()
    {
        vmaUnmapMemory(devicePtr->getAllocator(), allocation);
    }

    void UniformBuffer::updateUniformBuffer(void const* updateData, VkDeviceSize size)
    {
        memcpy(dataPtr, updateData, size);
        vmaFlushAllocation(devicePtr->getAllocator(), allocation, 0, size);
    }

    //----------STORAGE BUFFER DEFINITIONS----------//

    StorageBuffer::StorageBuffer(Device *device, CmdBufferAllocator *commands, VkDeviceSize size)
        :Buffer(device, commands, size)
    {
        createStorageBuffer();
    }

    StorageBuffer::~StorageBuffer()
    {
    }

    CommandBuffer StorageBuffer::setDataFromStaging(const StagingBuffer &stagingBuffer, VkDeviceSize size, const std::vector<SemaphorePair>& waitPairs, const std::vector<SemaphorePair>& signalPairs, const VkFence& fence)
    {
        return copyBuffer(stagingBuffer.getBuffer(), buffer, size, waitPairs, signalPairs, fence);
    }

    void StorageBuffer::createStorageBuffer()
    {
        std::vector<uint32_t> queueFamilies = {
            (uint32_t)std::max(devicePtr->getQueueFamilies().graphicsFamilyIndex, 0),
            (uint32_t)std::max(devicePtr->getQueueFamilies().computeFamilyIndex, 0)
        };

        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = NULL;
        bufferInfo.flags = 0;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                           VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        if(devicePtr->getQueueFamilies().graphicsFamilyIndex == devicePtr->getQueueFamilies().computeFamilyIndex)
        {
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            bufferInfo.queueFamilyIndexCount = 0;
        }
        else
        {
            bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
            bufferInfo.queueFamilyIndexCount = queueFamilies.size();
            bufferInfo.pQueueFamilyIndices = queueFamilies.data();
        }

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VmaAllocationInfo allocInfo;
        VkResult result = vmaCreateBuffer(devicePtr->getAllocator(), &bufferInfo, &allocCreateInfo, &buffer, &allocation, &allocInfo);
    }
}