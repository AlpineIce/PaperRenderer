#include "VulkanResources.h"
#include "PaperRenderer.h"

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

    void VulkanResource::idleOwners() const
    {
        for(Queue const* queue : owners)
        {
            //thread lock
            std::lock_guard<std::mutex> lock(const_cast<Queue*>(queue)->threadLock);

            //idle queue
            vkQueueWaitIdle(queue->queue);
        }
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
        idleOwners();
        vmaDestroyBuffer(renderer.getDevice().getAllocator(), buffer, allocation);
    }

    int Buffer::writeToBuffer(const std::vector<BufferWrite>& writes) const
    {
        //write data
        for(const BufferWrite& write : writes)
        {
            if(write.readData && write.size)
            {
                if(vmaCopyMemoryToAllocation(renderer.getDevice().getAllocator(), write.readData, allocation, write.offset, write.size) != VK_SUCCESS) return 1;
            }
        }

        return 0;
    }

    int Buffer::readFromBuffer(const std::vector<BufferRead> &reads) const
    {
        //read data
        for(const BufferRead& read : reads)
        {
            if(read.writeData && read.size)
            {
               if(vmaCopyAllocationToMemory(renderer.getDevice().getAllocator(), allocation, read.offset, read.writeData, read.size) != VK_SUCCESS) return 1;
            }
        }

        return 0;
    }

    const Queue& Buffer::copyFromBufferRanges(const Buffer &src, const std::vector<VkBufferCopy>& regions, const SynchronizationInfo& synchronizationInfo) const
    {
        VkCommandBuffer transferBuffer = renderer.getDevice().getCommands().getCommandBuffer(QueueType::TRANSFER); //note theres only 1 transfer cmd buffer

        const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = NULL
        };
        
        vkBeginCommandBuffer(transferBuffer, &beginInfo);
        vkCmdCopyBuffer(transferBuffer, src.getBuffer(), this->buffer, regions.size(), regions.data());
        vkEndCommandBuffer(transferBuffer);

        renderer.getDevice().getCommands().unlockCommandBuffer(transferBuffer);

        return renderer.getDevice().getCommands().submitToQueue(synchronizationInfo, { transferBuffer });
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

    FragmentableBuffer::FragmentableBuffer(RenderEngine& renderer, const BufferInfo &bufferInfo, VkDeviceSize minAlignment)
        :buffer(renderer, bufferInfo),
        minAlignment(minAlignment),
        renderer(renderer)
    {
    }

    FragmentableBuffer::~FragmentableBuffer()
    {
    }

    FragmentableBuffer::WriteResult FragmentableBuffer::newWrite(void* data, VkDeviceSize size, VkDeviceSize* returnLocation)
    {
        WriteResult result = SUCCESS;
        VkDeviceSize writeLocation = UINT64_MAX;

        //pad size
        size = renderer.getDevice().getAlignment(size, minAlignment);

        //attempt to find a chunk first
        auto lower = memoryFragments.lower_bound({ 0, size });
        if(lower != memoryFragments.end())
        {
            //set write location to the start of the fragment
            writeLocation = lower->location;

            //add new fragment if size is > 0
            if(lower->size - size > 0)
            {
                memoryFragments.insert({ lower->location + size, lower->size - size});
            }

            //remove old fragment
            memoryFragments.erase(lower);
        }
        //or just add to the stack if there is no good fragment
        else
        {
            desiredLocation += size;
            if(stackLocation + size > buffer.getSize())
            {
                //if compaction gives back no results then there's no more available memory
                if(!compact().size() || stackLocation + size > buffer.getSize())
                {
                    if(returnLocation) *returnLocation = UINT64_MAX;

                    result = OUT_OF_MEMORY;
                    return result;
                }

                //otherwise the compaction succeeded and enough available memory was created;
                result = COMPACTED;
            }

            //set write location to the stack pointer
            writeLocation = stackLocation;

            //change stack location to desired location
            stackLocation = desiredLocation;
        }

        //write if host visible
        if(buffer.isWritable() && data)
        {
            const BufferWrite write = {
                .offset = writeLocation,
                .size = size,
                .readData = data
            };
            buffer.writeToBuffer({ write });
        }

        //enumerate write location to ptr if provided
        if(returnLocation) *returnLocation = writeLocation;

        //increment total data size
        totalDataSize += size;
        
        return result;
    }

    void FragmentableBuffer::removeFromRange(VkDeviceSize offset, VkDeviceSize size)
    {
        //pad size
        size = renderer.getDevice().getAlignment(size, minAlignment);
        
        //add fragment
        Chunk memoryFragment = {
            .location = offset,
            .size = size
        };
        memoryFragments.insert(memoryFragment);

        //decrement total data size
        totalDataSize -= size;
    }

    std::vector<CompactionResult> FragmentableBuffer::compact()
    {
        std::vector<CompactionResult> compactionLocations;

        //if statement because the compaction callback shouldnt be invoked if no memory fragments exists, which leads to no effective compaction
        if(memoryFragments.size())
        {
            Timer timer(renderer, "Fragmentable Buffer Compaction", IRREGULAR);
            
            //sort memory fragments by their location
            std::vector<Chunk> vectorMemoryFragments;
            vectorMemoryFragments.insert(vectorMemoryFragments.end(), memoryFragments.begin(), memoryFragments.end());
            std::sort(vectorMemoryFragments.begin(), vectorMemoryFragments.end(), FragmentableBuffer::Chunk::compareByLocation);

            //start a command buffer
            VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(TRANSFER);

            const VkCommandBufferBeginInfo beginInfo = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .pNext = NULL,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                .pInheritanceInfo = NULL
            };
            vkBeginCommandBuffer(cmdBuffer, &beginInfo);

            //dynamic variables
            VkDeviceSize sizeReduction = 0;

            //shift data into memory fragment gaps
            for(uint32_t i = 0; i < vectorMemoryFragments.size(); i++)
            {
                //get current and next chunks
                const Chunk chunk = vectorMemoryFragments[i];
                const Chunk nextChunk = i < vectorMemoryFragments.size() - 1 ? vectorMemoryFragments[i + 1] : Chunk({ stackLocation, 0 });

                //get important sizes
                const VkDeviceSize totalCopySize = nextChunk.location - std::min(nextChunk.location, (chunk.location + chunk.size)); //copy the size of this chunks location + size all the way to the next chunks location
                const VkDeviceSize srcOffset = chunk.location + chunk.size; //copy past the fragmentation gap
                const VkDeviceSize dstOffset = chunk.location - sizeReduction; //copy into the gap

                //increment size reduction
                sizeReduction += chunk.size;

                //perform data moves if total copy size
                if(totalCopySize)
                {
                    //iteration count needed to avoid undefined behavior with overlapping copies
                    const VkDeviceSize iterations = std::ceil((double)totalCopySize / (double)sizeReduction);

                    //copy over iterations
                    for(VkDeviceSize j = 0; j < iterations; j++)
                    {
                        //get this iteration's copy size
                        const VkDeviceSize itCopySize = std::min(totalCopySize - (sizeReduction * j), sizeReduction);
                    
                        //buffer copy
                        const VkBufferCopy copyRegion = {
                            .srcOffset = srcOffset + (itCopySize * j),
                            .dstOffset = dstOffset + (itCopySize * j),
                            .size = itCopySize
                        };
                        vkCmdCopyBuffer(cmdBuffer, buffer.getBuffer(), buffer.getBuffer(), 1, &copyRegion);

                        //insert memory barrier at src offset
                        const VkBufferMemoryBarrier2 memBarrier = {
                            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                            .pNext = NULL,
                            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .buffer = buffer.getBuffer(),
                            .offset = copyRegion.srcOffset,
                            .size = copyRegion.size
                        };

                        const VkDependencyInfo dependencyInfo = {
                            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                            .pNext = NULL,
                            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                            .bufferMemoryBarrierCount = 1,
                            .pBufferMemoryBarriers = &memBarrier
                        };

                        vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);
                    }

                    //create compaction result
                    compactionLocations.push_back({
                        .location = chunk.location,
                        .shiftSize = chunk.size
                    });
                }
            }

            //modify stack pointers
            stackLocation -= sizeReduction;
            desiredLocation -= sizeReduction;

            //clear memory fragments
            memoryFragments.clear();

            //end command buffer
            vkEndCommandBuffer(cmdBuffer);

            renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

            //idle resource owners before submission
            buffer.idleOwners();

            //submit
            const SynchronizationInfo syncInfo = {
                .queueType = TRANSFER
            };
            vkQueueWaitIdle(renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer }).queue);

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
        idleOwners();
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
        //command buffer
        VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(QueueType::GRAPHICS);

        const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = NULL
        };

        vkBeginCommandBuffer(cmdBuffer, &beginInfo); 
        
        //copy
        copyBufferToImage(imageStagingBuffer.getBuffer(), image, cmdBuffer);

        //memory barrier
        const VkMemoryBarrier2 memBarrier = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT
        };

        const VkDependencyInfo dependencyInfo = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &memBarrier
        };

        vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

        //blit
        generateMipmaps(cmdBuffer);

        vkEndCommandBuffer(cmdBuffer);

        renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

        const SynchronizationInfo syncInfo = {
            .queueType = GRAPHICS
        };
        vkQueueWaitIdle(renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer }).queue);
    }

    VkSampler Image::getNewSampler()
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
        samplerInfo.anisotropyEnable = renderer.getDevice().getGPUFeatures().samplerAnisotropy;
        samplerInfo.maxAnisotropy = renderer.getDevice().getGPUProperties().properties.limits.maxSamplerAnisotropy;
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

    void Image::copyBufferToImage(VkBuffer src, VkImage dst, VkCommandBuffer cmdBuffer)
    {
        //layout transition memory barrier
        const VkImageMemoryBarrier2 imageBarrier = {
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

        const VkDependencyInfo dependencyInfo = {
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

        vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

        //copy image
        const VkBufferImageCopy copyRegion = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = imageInfo.imageAspect,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .imageOffset = {0},
            .imageExtent = imageInfo.extent
        };
        vkCmdCopyBufferToImage(cmdBuffer, src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    }
    
    void Image::generateMipmaps(VkCommandBuffer cmdBuffer)
    {
        //mipmap blit process
        for(uint32_t i = 1; i < mipmapLevels; i++)
        {
            //----------INITIAL IMAGE BARRIERS----------//

            const VkImageMemoryBarrier2 initialImageBarriers[2] = {
                { //source mip level
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
                },
                { //destination mip level
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
                }
            };

            const VkDependencyInfo initialDependencyInfo = {
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

            vkCmdPipelineBarrier2(cmdBuffer, &initialDependencyInfo);

            //----------IMAGE BLIT----------//

            VkImageBlit2 imageBlit = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
                .pNext = NULL,
                .srcSubresource = {
                    .aspectMask = imageInfo.imageAspect,
                    .mipLevel = i - 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                },
                .srcOffsets = { {}, int32_t(imageInfo.extent.width >> (i - 1)), int32_t(imageInfo.extent.height >> (i - 1)), 1 },
                .dstSubresource = {
                    .aspectMask = imageInfo.imageAspect,
                    .mipLevel = i,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                },
                .dstOffsets = { {}, int32_t(imageInfo.extent.width >> i), int32_t(imageInfo.extent.height >> i), 1 }
            };

            const VkBlitImageInfo2 blitInfo = {
                .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
                .pNext = NULL,
                .srcImage = image,
                .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .dstImage = image,
                .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .regionCount = 1,
                .pRegions = &imageBlit,
                .filter = VK_FILTER_LINEAR
            };

            vkCmdBlitImage2(cmdBuffer, &blitInfo);
        }

        //final image layout transitions
        for(uint32_t i = 0; i < mipmapLevels; i++)
        {
            const VkImageMemoryBarrier2 finalImageBarrier = {
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

            const VkDependencyInfo finalDependencyInfo = {
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

            vkCmdPipelineBarrier2(cmdBuffer, &finalDependencyInfo);
        }
    }
}