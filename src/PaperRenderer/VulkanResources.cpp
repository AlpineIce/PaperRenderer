#include "VulkanResources.h"
#include "PaperRenderer.h"

#include <stdexcept>
#include <algorithm>

namespace PaperRenderer
{
    //----------RESOURCE BASE CLASS DEFINITIONS----------//

    VulkanResource::VulkanResource(RenderEngine& renderer)
        :renderer(renderer)
    {
    }

    VulkanResource::~VulkanResource()
    {
    }
    
    //----------BUFFER DEFINITIONS----------//

    Buffer::Buffer(RenderEngine& renderer, const BufferInfo& bufferInfo)
        :VulkanResource(renderer)
    {
        VkBufferCreateInfo bufferCreateInfo = {};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.pNext = NULL;
        bufferCreateInfo.flags = 0;
        bufferCreateInfo.size = bufferInfo.size;
        bufferCreateInfo.usage = bufferInfo.usageFlags;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;

        const QueueFamiliesIndices& deviceQueueFamilies = renderer.getDevice().getQueueFamiliesIndices();
        std::vector<uint32_t> queueFamilyIndices;
        if(deviceQueueFamilies.graphicsFamilyIndex != -1) queueFamilyIndices.push_back(deviceQueueFamilies.graphicsFamilyIndex);
        if(deviceQueueFamilies.computeFamilyIndex != -1) queueFamilyIndices.push_back(deviceQueueFamilies.computeFamilyIndex);
        if(deviceQueueFamilies.transferFamilyIndex != -1) queueFamilyIndices.push_back(deviceQueueFamilies.transferFamilyIndex);
        if(deviceQueueFamilies.presentationFamilyIndex != -1) queueFamilyIndices.push_back(deviceQueueFamilies.presentationFamilyIndex);
        std::sort(queueFamilyIndices.begin(), queueFamilyIndices.end());
        auto uniqueIndices = std::unique(queueFamilyIndices.begin(), queueFamilyIndices.end());
        queueFamilyIndices.erase(uniqueIndices, queueFamilyIndices.end());

        if(!queueFamilyIndices.size()) throw std::runtime_error("Tried to create buffer with no queue family indices referenced");
        
        bufferCreateInfo.queueFamilyIndexCount = queueFamilyIndices.size();
        bufferCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data();

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = bufferInfo.allocationFlags;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VmaAllocationInfo allocInfo = {};
        VkResult result = vmaCreateBuffer(renderer.getDevice().getAllocator(), &bufferCreateInfo, &allocCreateInfo, &buffer, &allocation, &allocInfo);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Buffer creation failed");
        }

        VkMemoryPropertyFlags memPropertyFlags;
        vmaGetAllocationMemoryProperties(renderer.getDevice().getAllocator(), allocation, &memPropertyFlags);
        if(memPropertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) writable = true;
        
        size = bufferInfo.size;
    }

    Buffer::~Buffer()
    {
        vmaDestroyBuffer(renderer.getDevice().getAllocator(), buffer, allocation);
    }

    int Buffer::writeToBuffer(const std::vector<BufferWrite>& writes) const
    {
        //write data
        for(const BufferWrite& write : writes)
        {
            if(write.data && write.size)
            {
                if(vmaCopyMemoryToAllocation(renderer.getDevice().getAllocator(), write.data, allocation, write.offset, write.size) != VK_SUCCESS) return 1;
            }
        }

        return 0;
    }

    int Buffer::readFromBuffer(const std::vector<BufferWrite> &reads) const
    {
        //read data
        for(const BufferWrite& read : reads)
        {
            if(read.data && read.size)
            {
               if(vmaCopyAllocationToMemory(renderer.getDevice().getAllocator(), allocation, read.offset, read.data, read.size) != VK_SUCCESS) return 1;
            }
        }

        return 0;
    }

    void Buffer::copyFromBufferRanges(const Buffer &src, const std::vector<VkBufferCopy>& regions, const SynchronizationInfo& synchronizationInfo) const
    {
        VkCommandBuffer transferBuffer = renderer.getDevice().getCommands().getCommandBuffer(QueueType::TRANSFER); //note theres only 1 transfer cmd buffer

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(transferBuffer, &beginInfo);
        vkCmdCopyBuffer(transferBuffer, src.getBuffer(), this->buffer, regions.size(), regions.data());
        vkEndCommandBuffer(transferBuffer);

        renderer.getDevice().getCommands().unlockCommandBuffer(transferBuffer);

        renderer.getDevice().getCommands().submitToQueue(synchronizationInfo, { transferBuffer });
    }

    VkDeviceAddress Buffer::getBufferDeviceAddress() const
    {
        if(buffer != VK_NULL_HANDLE)
        {
            VkBufferDeviceAddressInfo deviceAddressInfo = {};
            deviceAddressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            deviceAddressInfo.pNext = NULL;
            deviceAddressInfo.buffer = buffer;

            return vkGetBufferDeviceAddress(renderer.getDevice().getDevice(), &deviceAddressInfo);
        }
        else
        {
            return 0;
        }
    }

    //----------FRAGMENTABLE BUFFER DEFINITIONS----------//

    FragmentableBuffer::FragmentableBuffer(RenderEngine& renderer, const BufferInfo &bufferInfo)
        :renderer(renderer)
    {
        buffer = std::make_unique<Buffer>(renderer, bufferInfo);
    }

    FragmentableBuffer::~FragmentableBuffer()
    {
    }

    void FragmentableBuffer::verifyFragmentation()
    {
    }

    FragmentableBuffer::WriteResult FragmentableBuffer::newWrite(void* data, VkDeviceSize size, VkDeviceSize minAlignment, VkDeviceSize* returnLocation)
    {
        WriteResult result = SUCCESS;
        desiredLocation += renderer.getDevice().getAlignment(size, minAlignment);

        if(stackLocation + renderer.getDevice().getAlignment(size, minAlignment) > buffer->getSize())
        {
            //if compaction gives back no results then there's no more available memory
            if(!compact().size() || stackLocation + size > buffer->getSize())
            {
                if(returnLocation) *returnLocation = UINT64_MAX;

                result = OUT_OF_MEMORY;
                return result;
            }

            //otherwise the compaction succeeded and enough available memory was created;
            desiredLocation = stackLocation;
            result = COMPACTED;
        }

        //write if host visible
        if(buffer->isWritable() && data)
        {
            BufferWrite write = {};
            write.data = data;
            write.offset = renderer.getDevice().getAlignment(stackLocation, minAlignment);
            write.size = size;
            
            buffer->writeToBuffer({ write });
        }

        if(returnLocation) *returnLocation = renderer.getDevice().getAlignment(stackLocation, minAlignment);

        if(result == SUCCESS)
        {
            stackLocation = desiredLocation;
        }
        
        return result;
    }

    void FragmentableBuffer::removeFromRange(VkDeviceSize offset, VkDeviceSize size)
    {
        Chunk memoryFragment = {
            .location = offset,
            .size = size
        };
        memoryFragments.push_back(memoryFragment);
    }

    std::vector<CompactionResult> FragmentableBuffer::compact()
    {
        std::vector<CompactionResult> compactionLocations;

        //if statement because the compaction callback shouldnt be invoked if no memory fragments exists, which leads to no effective compaction
        if(memoryFragments.size())
        {
            //sort memory fragments first
            std::sort(memoryFragments.begin(), memoryFragments.end(), FragmentableBuffer::Chunk::compareByLocation);

            //start a command buffer if the buffer isnt writable from CPU (transfer will be done by GPU instead)
            VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
            if(!buffer->isWritable())
            {
                //start command buffer
                cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(TRANSFER);

                VkCommandBufferBeginInfo beginInfo = {};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.pNext = NULL;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                vkBeginCommandBuffer(cmdBuffer, &beginInfo);
            }

            //move data from memory fragment (location + size) into (location), subtract (size) from (stackLocation), and remove memory fragment from stack
            for(uint32_t i = 0; i < memoryFragments.size(); i++)
            {
                const Chunk& chunk = memoryFragments.at(i);

                //get copy size
                VkDeviceSize copySize = 0;
                if(i < memoryFragments.size() - 1)
                {
                    //use "distance" to next chunk
                    const Chunk& nextChunk = memoryFragments.at(i + 1);
                    copySize = nextChunk.location - (chunk.location + chunk.size);
                }
                else
                {
                    //use rest of the range of buffer
                    copySize = stackLocation - (chunk.location + chunk.size);
                }
                
                //CPU data move
                if(!cmdBuffer)
                {
                    std::vector<char> readData(stackLocation - (chunk.location + chunk.size));

                    //read data
                    BufferWrite read = {};
                    read.offset = chunk.location + chunk.size;
                    read.size = copySize;
                    read.data = readData.data();
                    buffer->readFromBuffer({ read });

                    //copy data into buffer location
                    BufferWrite write = {};
                    write.offset = chunk.location;
                    write.size = copySize;
                    write.data = readData.data();
                    buffer->writeToBuffer({ write });
                }
                //GPU data move     //TODO TEST IF THIS STUFF ACTUALLY WORKS!!!
                else
                {
                    const VkDeviceSize maxTransferSize = chunk.size;

                    uint32_t iterations = std::ceil((double)copySize / (double)maxTransferSize);
                    for(uint32_t j = 0; j < iterations; j++)
                    {
                        //buffer copy src
                        VkBufferCopy copyRegion = {};
                        copyRegion.srcOffset = chunk.location + chunk.size;
                        copyRegion.size = std::min(copySize, maxTransferSize);
                        copyRegion.dstOffset = chunk.location;
                        
                        vkCmdCopyBuffer(cmdBuffer, buffer->getBuffer(), buffer->getBuffer(), 1, &copyRegion);

                        //insert memory barrier at src offset. dst barrier isnt needed since future writes should not read/write from/into previous writes
                        VkBufferMemoryBarrier2 memBarrier = {
                            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                            .pNext = NULL,
                            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .buffer = buffer->getBuffer(),
                            .offset = copyRegion.srcOffset,
                            .size = copyRegion.size
                        };

                        VkDependencyInfo dependencyInfo = {};
                        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                        dependencyInfo.pNext = NULL;
                        dependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
                        dependencyInfo.bufferMemoryBarrierCount = 1;
                        dependencyInfo.pBufferMemoryBarriers = &memBarrier;

                        vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

                        copySize -= copyRegion.size;
                    }
                }

                //move stack "pointer"
                stackLocation -= chunk.size;

                //create compaction result
                compactionLocations.push_back({
                    .location = chunk.location,
                    .shiftSize = chunk.size
                });
            }

            //end command buffer if used and submit
            if(cmdBuffer)
            {
                vkEndCommandBuffer(cmdBuffer);

                renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

                //submit
                SynchronizationInfo syncInfo = {};
                syncInfo.queueType = TRANSFER;
                syncInfo.fence = renderer.getDevice().getCommands().getUnsignaledFence();
                renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer });

                vkWaitForFences(renderer.getDevice().getDevice(), 1, &syncInfo.fence, VK_TRUE, UINT64_MAX);
                vkDestroyFence(renderer.getDevice().getDevice(), syncInfo.fence, nullptr);
            }

            //clear memory fragments
            memoryFragments.clear();

            //call callback function
            if(compactionCallback) compactionCallback(compactionLocations);
        }

        return compactionLocations;
    }

    //----------IMAGE DEFINITIONS----------//

    Image::Image(RenderEngine& renderer, const ImageInfo& imageInfo)
        :VulkanResource(renderer),
        imageInfo(imageInfo)
    {
        //calculate mip levels (select the least of minimum mip levels either explicitely, or from whats mathematically doable)
        mipmapLevels = std::min((uint32_t)(std::floor(std::log2(std::max(imageInfo.extent.width, imageInfo.extent.height))) + 1), std::max(imageInfo.maxMipLevels, (uint32_t)1));

        //create image
        VkImageCreateInfo imageCreateInfo = {};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.pNext = NULL;
        imageCreateInfo.flags = 0;
        imageCreateInfo.imageType = imageInfo.imageType;
        imageCreateInfo.format = imageInfo.format;
        imageCreateInfo.extent = imageInfo.extent;
        imageCreateInfo.mipLevels = mipmapLevels;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = imageInfo.samples;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage = imageInfo.usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        const QueueFamiliesIndices& deviceQueueFamilies = renderer.getDevice().getQueueFamiliesIndices();
        std::vector<uint32_t> queueFamilyIndices;
        if(deviceQueueFamilies.graphicsFamilyIndex != -1) queueFamilyIndices.push_back(deviceQueueFamilies.graphicsFamilyIndex);
        if(deviceQueueFamilies.computeFamilyIndex != -1) queueFamilyIndices.push_back(deviceQueueFamilies.computeFamilyIndex);
        if(deviceQueueFamilies.transferFamilyIndex != -1) queueFamilyIndices.push_back(deviceQueueFamilies.transferFamilyIndex);
        if(deviceQueueFamilies.presentationFamilyIndex != -1) queueFamilyIndices.push_back(deviceQueueFamilies.presentationFamilyIndex);
        std::sort(queueFamilyIndices.begin(), queueFamilyIndices.end());
        auto uniqueIndices = std::unique(queueFamilyIndices.begin(), queueFamilyIndices.end());
        queueFamilyIndices.erase(uniqueIndices, queueFamilyIndices.end());

        if(!queueFamilyIndices.size()) throw std::runtime_error("Tried to create buffer with no queue family indices referenced");
        
        imageCreateInfo.queueFamilyIndexCount = queueFamilyIndices.size();
        imageCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data();

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = 0;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VmaAllocationInfo allocInfo = {};
        if(vmaCreateImage(renderer.getDevice().getAllocator(), &imageCreateInfo, &allocCreateInfo, &image, &allocation, &allocInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("Buffer creation failed");
        }

        size = 0; //TODO?
    }

    Image::~Image()
    {
        vmaDestroyImage(renderer.getDevice().getAllocator(), image, allocation);
    }

    VkImageView Image::getNewImageView(VkImageAspectFlags aspectMask, VkImageViewType viewType, VkFormat format)
    {
        VkImageSubresourceRange subresource = {};
        subresource.aspectMask = aspectMask;
        subresource.baseMipLevel = 0;
        subresource.levelCount = mipmapLevels;
        subresource.baseArrayLayer = 0;
        subresource.layerCount = 1;

        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.pNext = NULL;
        viewInfo.flags = 0;
        viewInfo.image = image;
        viewInfo.viewType = viewType;
        viewInfo.format = format;
        viewInfo.subresourceRange = subresource;

        VkImageView view;
        VkResult result = vkCreateImageView(renderer.getDevice().getDevice(), &viewInfo, nullptr, &view);

        return view;
    }

    void Image::setImageData(const Buffer &imageStagingBuffer)
    {
        //get synchronization stuff
        VkSemaphore copySemaphore = renderer.getDevice().getCommands().getSemaphore();
        VkFence blitFence = renderer.getDevice().getCommands().getUnsignaledFence();

        //copy staging buffer into image
        SynchronizationInfo copySynchronizationInfo = {
            .queueType = QueueType::TRANSFER,
            .binaryWaitPairs = {},
            .binarySignalPairs = { { copySemaphore, VK_PIPELINE_STAGE_2_TRANSFER_BIT } },
            .timelineWaitPairs = {},
            .timelineSignalPairs = {},
            .fence = VK_NULL_HANDLE
        };
        copyBufferToImage(imageStagingBuffer.getBuffer(), image, copySynchronizationInfo);

        //generate mipmaps
        SynchronizationInfo blitSynchronization = {
            .queueType = QueueType::GRAPHICS,
            .binaryWaitPairs = { { copySemaphore, VK_PIPELINE_STAGE_2_BLIT_BIT } },
            .binarySignalPairs = {},
            .timelineWaitPairs = {},
            .timelineSignalPairs = {},
            .fence = blitFence
        };
        generateMipmaps(blitSynchronization);
        
        //destroy synchronization stuff
        vkWaitForFences(renderer.getDevice().getDevice(), 1, &blitFence, VK_TRUE, UINT64_MAX);
        vkDestroySemaphore(renderer.getDevice().getDevice(), copySemaphore, nullptr);
        vkDestroyFence(renderer.getDevice().getDevice(), blitSynchronization.fence, nullptr);
    }

    VkSampler Image::getNewSampler()
    {
        VkPhysicalDeviceFeatures2 features = {};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = NULL;
        vkGetPhysicalDeviceFeatures2(renderer.getDevice().getGPU(), &features);

        VkPhysicalDeviceProperties2 properties = {};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = NULL;
        vkGetPhysicalDeviceProperties2(renderer.getDevice().getGPU(), &properties);

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
        samplerInfo.anisotropyEnable = features.features.samplerAnisotropy;
        samplerInfo.maxAnisotropy = properties.properties.limits.maxSamplerAnisotropy;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = mipmapLevels;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        VkSampler sampler;
        VkResult result = vkCreateSampler(renderer.getDevice().getDevice(), &samplerInfo, nullptr, &sampler);

        return sampler;
    }

    void Image::copyBufferToImage(VkBuffer src, VkImage dst, const SynchronizationInfo& synchronizationInfo)
    {
        VkCommandBuffer transferBuffer = renderer.getDevice().getCommands().getCommandBuffer(QueueType::TRANSFER);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkImageSubresourceLayers subresource = {};
        subresource.aspectMask = imageInfo.imageAspect;
        subresource.mipLevel = 0;
        subresource.baseArrayLayer = 0;
        subresource.layerCount = 1;

        VkBufferImageCopy copyRegion = {};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource = subresource;
        copyRegion.imageOffset = {0};
        copyRegion.imageExtent = imageInfo.extent;

        vkBeginCommandBuffer(transferBuffer, &beginInfo);

        //layout transition memory barrier
        VkImageMemoryBarrier2 imageBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {
                .aspectMask = imageInfo.imageAspect,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };

        VkDependencyInfo dependencyInfo = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = NULL,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = NULL,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &imageBarrier
        };

        vkCmdPipelineBarrier2(transferBuffer, &dependencyInfo);

        //copy image
        vkCmdCopyBufferToImage(transferBuffer, src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        vkEndCommandBuffer(transferBuffer);

        renderer.getDevice().getCommands().unlockCommandBuffer(transferBuffer);

        renderer.getDevice().getCommands().submitToQueue(synchronizationInfo, { transferBuffer });
    }
    
    void Image::generateMipmaps(const SynchronizationInfo& synchronizationInfo)
    {
        //command buffer
        VkCommandBuffer blitBuffer = renderer.getDevice().getCommands().getCommandBuffer(QueueType::GRAPHICS);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(blitBuffer, &beginInfo); 

        //mipmap blit process
        for(uint32_t i = 1; i < mipmapLevels; i++)
        {
            //----------INITIAL IMAGE BARRIERS----------//

            VkImageMemoryBarrier2 initialImageBarriers[2];

            //source mip level
            initialImageBarriers[0] = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange = {
                    .aspectMask = imageInfo.imageAspect,
                    .baseMipLevel = i - 1,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                }
            };

            //destination mip level
            initialImageBarriers[1] = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                .srcAccessMask = VK_ACCESS_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange = {
                    .aspectMask = imageInfo.imageAspect,
                    .baseMipLevel = i,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                }
            };

            VkDependencyInfo initialDependencyInfo = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = NULL,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                .memoryBarrierCount = 0,
                .pMemoryBarriers = NULL,
                .bufferMemoryBarrierCount = 0,
                .pBufferMemoryBarriers = NULL,
                .imageMemoryBarrierCount = 2,
                .pImageMemoryBarriers = initialImageBarriers
            };

            vkCmdPipelineBarrier2(blitBuffer, &initialDependencyInfo);

            //----------IMAGE BLIT----------//

            VkImageBlit2 imageBlit = {};
            imageBlit.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
            imageBlit.pNext = NULL;
            imageBlit.srcSubresource = {
                .aspectMask = imageInfo.imageAspect,
                .mipLevel = i - 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            };
            imageBlit.srcOffsets[1] = { int32_t(imageInfo.extent.width >> (i - 1)), int32_t(imageInfo.extent.height >> (i - 1)), 1 };
            imageBlit.dstSubresource = {
                .aspectMask = imageInfo.imageAspect,
                .mipLevel = i,
                .baseArrayLayer = 0,
                .layerCount = 1
            };
            imageBlit.dstOffsets[1] = { int32_t(imageInfo.extent.width >> i), int32_t(imageInfo.extent.height >> i), 1 };

            VkBlitImageInfo2 blitInfo = {};
            blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
            blitInfo.pNext = NULL;
            blitInfo.filter = VK_FILTER_LINEAR;
            blitInfo.srcImage = image;
            blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            blitInfo.dstImage = image;
            blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            blitInfo.regionCount = 1;
            blitInfo.pRegions = &imageBlit;

            vkCmdBlitImage2(blitBuffer, &blitInfo);
        }

        //final image layout transitions
        for(uint32_t i = 0; i < mipmapLevels; i++)
        {
            VkImageMemoryBarrier2 finalImageBarrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = imageInfo.desiredLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange = {
                    .aspectMask = imageInfo.imageAspect,
                    .baseMipLevel = i,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                }
            };

            VkDependencyInfo finalDependencyInfo = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = NULL,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                .memoryBarrierCount = 0,
                .pMemoryBarriers = NULL,
                .bufferMemoryBarrierCount = 0,
                .pBufferMemoryBarriers = NULL,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &finalImageBarrier
            };

            vkCmdPipelineBarrier2(blitBuffer, &finalDependencyInfo);
        }
        
        vkEndCommandBuffer(blitBuffer);

        renderer.getDevice().getCommands().unlockCommandBuffer(blitBuffer);

        renderer.getDevice().getCommands().submitToQueue(synchronizationInfo, { blitBuffer });
    }
}