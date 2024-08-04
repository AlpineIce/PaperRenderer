#include "VulkanResources.h"

#include <stdexcept>
#include <algorithm>

namespace PaperRenderer
{
    //----------RESOURCE BASE CLASS DEFINITIONS----------//

    VulkanResource::VulkanResource(VkDevice device)
        :device(device),
        allocationPtr(NULL)
    {
        memRequirements = {};
        memRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        memRequirements.pNext = NULL;
    }

    VulkanResource::~VulkanResource()
    {
    }

    int VulkanResource::assignAllocation(DeviceAllocation *allocation)
    {
        allocationPtr = allocation;

        return 0;
    }
    
    //----------BUFFER DEFINITIONS----------//

    Buffer::Buffer(VkDevice device, const BufferInfo& bufferInfo)
        :VulkanResource(device)
    {
        VkBufferCreateInfo bufferCreateInfo = {};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.pNext = NULL;
        bufferCreateInfo.flags = 0;
        bufferCreateInfo.size = bufferInfo.size;
        bufferCreateInfo.usage = bufferInfo.usageFlags;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;

        std::vector<uint32_t> queueFamilyIndices;
        if(bufferInfo.queueFamiliesIndices.graphicsFamilyIndex != -1) queueFamilyIndices.push_back(bufferInfo.queueFamiliesIndices.graphicsFamilyIndex);
        if(bufferInfo.queueFamiliesIndices.computeFamilyIndex != -1) queueFamilyIndices.push_back(bufferInfo.queueFamiliesIndices.computeFamilyIndex);
        if(bufferInfo.queueFamiliesIndices.transferFamilyIndex != -1) queueFamilyIndices.push_back(bufferInfo.queueFamiliesIndices.transferFamilyIndex);
        if(bufferInfo.queueFamiliesIndices.presentationFamilyIndex != -1) queueFamilyIndices.push_back(bufferInfo.queueFamiliesIndices.presentationFamilyIndex);
        std::sort(queueFamilyIndices.begin(), queueFamilyIndices.end());
        auto uniqueIndices = std::unique(queueFamilyIndices.begin(), queueFamilyIndices.end());
        queueFamilyIndices.erase(uniqueIndices, queueFamilyIndices.end());

        if(!queueFamilyIndices.size()) throw std::runtime_error("Tried to create buffer with no queue family indices referenced");
        
        bufferCreateInfo.queueFamilyIndexCount = queueFamilyIndices.size();
        bufferCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data();
        
        vkCreateBuffer(device, &bufferCreateInfo, nullptr, &buffer);

        //get memory requirements
        bufferMemRequirements.sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS;
        bufferMemRequirements.pNext = NULL;
        bufferMemRequirements.pCreateInfo = &bufferCreateInfo;

        vkGetDeviceBufferMemoryRequirements(device, &bufferMemRequirements, &memRequirements);
        size = 0; //default size of 0 to show no allocation
    }

    Buffer::~Buffer()
    {
        vkDestroyBuffer(device, buffer, nullptr);
    }

    int Buffer::assignAllocation(DeviceAllocation *allocation)
    {
        VulkanResource::assignAllocation(allocation);

        needsFlush = allocation->getFlushRequirement();

        //bind memory
        bindingInfo = allocation->bindBuffer(buffer, memRequirements.memoryRequirements);
        size = memRequirements.memoryRequirements.size;
        if(bindingInfo.allocatedSize == 0)
        {
            return 1; //error in this case should just be that its out of memory, or wrong memory type was used
        }

        hostDataPtr = (char*)allocation->getMappedPtr() + bindingInfo.allocationLocation;

        return 0; //success, also VK_SUCCESS
    }

    int Buffer::writeToBuffer(const std::vector<BufferWrite>& writes) const
    {
        //make sure memory is even mappable
        if(hostDataPtr)
        {
            //gather ranges to flush and invalidate (if needed)
            std::vector<VkMappedMemoryRange> toFlushRanges;
            if(needsFlush)
            {
                for(const BufferWrite& write : writes)
                {
                    VkMappedMemoryRange range = {};
                    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                    range.pNext = NULL;
                    range.memory = allocationPtr->getAllocation();
                    range.offset = write.offset;
                    range.size = write.size;

                    toFlushRanges.push_back(range);
                }

                //invalidate all the ranges for coherency
                if(vkInvalidateMappedMemoryRanges(device, toFlushRanges.size(), toFlushRanges.data()) != VK_SUCCESS) return 1;
            }

            //write data
            for(const BufferWrite& write : writes)
            {
                memcpy(hostDataPtr, (char*)write.data + write.offset, write.size); //cast to char for 1 byte increment pointer arithmetic
            }

            //flush data from cache if needed
            if(needsFlush)
            {
                if(vkFlushMappedMemoryRanges(device, toFlushRanges.size(), toFlushRanges.data()) != VK_SUCCESS) return 1;
            }

            return 0;
        }
        else
        {
            throw std::runtime_error("Tried to write to unmapped memory");
        }
    }

    CommandBuffer Buffer::copyFromBufferRanges(Buffer &src, const std::vector<VkBufferCopy>& regions, const SynchronizationInfo& synchronizationInfo) const
    {
        VkCommandBuffer transferBuffer = Commands::getCommandBuffer(device, QueueType::TRANSFER); //note theres only 1 transfer cmd buffer

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(transferBuffer, &beginInfo);
        vkCmdCopyBuffer(transferBuffer, src.getBuffer(), this->buffer, regions.size(), regions.data());
        vkEndCommandBuffer(transferBuffer);

        std::vector<VkCommandBuffer> commandBuffers = {
            transferBuffer
        };

        Commands::submitToQueue(device, synchronizationInfo, commandBuffers);
        
        return { transferBuffer, TRANSFER };
    }

    VkDeviceAddress Buffer::getBufferDeviceAddress() const
    {
        if(buffer != VK_NULL_HANDLE)
        {
            VkBufferDeviceAddressInfo deviceAddressInfo = {};
            deviceAddressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            deviceAddressInfo.pNext = NULL;
            deviceAddressInfo.buffer = buffer;

            return vkGetBufferDeviceAddress(device, &deviceAddressInfo);
        }
        else
        {
            return 0;
        }
    }

    //----------FRAGMENTABLE BUFFER DEFINITIONS----------//

    FragmentableBuffer::FragmentableBuffer(VkDevice device, const BufferInfo &bufferInfo)
        :device(device)
    {
        buffer = std::make_unique<Buffer>(device, bufferInfo);
    }

    FragmentableBuffer::~FragmentableBuffer()
    {
    }

    void FragmentableBuffer::verifyFragmentation()
    {
    }

    void FragmentableBuffer::assignAllocation(DeviceAllocation* newAllocation)
    {
        if(newAllocation->getMemoryType().propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            buffer->assignAllocation(newAllocation);
            allocationPtr = newAllocation;
        }
        else
        {
            throw std::runtime_error("Tried to assign allocation which was created with neither VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT or VK_MEMORY_PROPERTY_HOST_COHERENT_BIT to a fragmentable buffer");
        }
    }

    FragmentableBuffer::WriteResult FragmentableBuffer::newWrite(void* data, VkDeviceSize size, VkDeviceSize minAlignment, VkDeviceSize* returnLocation)
    {
        WriteResult result = SUCCESS;
        desiredLocation += DeviceAllocation::padToMultiple(size, minAlignment);

        if(stackLocation + DeviceAllocation::padToMultiple(size, minAlignment) > buffer->getSize())
        {
            //if compaction gives back no results then there's no more available memory
            if(!compact().size() || stackLocation + size > buffer->getSize())
            {
                if(returnLocation) *returnLocation = UINT64_MAX;

                result = OUT_OF_MEMORY;
                return result;
            }

            //otherwise the compaction succeeded and enough available memory was created;
            result = COMPACTED;
        }
        
        memcpy((char*)buffer->getHostDataPtr() + DeviceAllocation::padToMultiple(stackLocation, minAlignment), data, size);
        if(returnLocation) *returnLocation = DeviceAllocation::padToMultiple(stackLocation, minAlignment);

        stackLocation = desiredLocation;

        return result;
    }

    void FragmentableBuffer::removeFromRange(VkDeviceSize offset, VkDeviceSize size)
    {
        Chunk memoryFragment = {
            .location = offset,
            .size = size
        };
        memoryFragments.push(memoryFragment);
    }

    std::vector<CompactionResult> FragmentableBuffer::compact()
    {
        std::vector<CompactionResult> compactionLocations;

        //if statement because the compaction callback shouldnt be invoked if no memory fragments exists, which leads to no effective compaction
        if(memoryFragments.size())
        {
            while(memoryFragments.size())
            {
                //move data from memory fragment (location + size) into (location), subtract (size) from (stackLocation), and remove memory fragment from stack
                const Chunk& chunk = memoryFragments.top();
                memmove((char*)buffer->getHostDataPtr() + chunk.location, (char*)buffer->getHostDataPtr() + chunk.location + chunk.size, stackLocation - (chunk.location + chunk.size));
                stackLocation -= chunk.size;
                memoryFragments.pop();

                //create compaction result
                compactionLocations.push_back({
                    .location = chunk.location,
                    .shiftSize = chunk.size
                });
            }

            //call callback function
            if(compactionCallback) compactionCallback(compactionLocations);
        }

        return compactionLocations;
    }

    //----------IMAGE DEFINITIONS----------//

    Image::Image(VkDevice device, const ImageInfo& imageInfo)
        :VulkanResource(device),
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

        std::vector<uint32_t> queueFamilyIndices;
        if(imageInfo.queueFamiliesIndices.graphicsFamilyIndex != -1) queueFamilyIndices.push_back(imageInfo.queueFamiliesIndices.graphicsFamilyIndex);
        if(imageInfo.queueFamiliesIndices.computeFamilyIndex != -1) queueFamilyIndices.push_back(imageInfo.queueFamiliesIndices.computeFamilyIndex);
        if(imageInfo.queueFamiliesIndices.transferFamilyIndex != -1) queueFamilyIndices.push_back(imageInfo.queueFamiliesIndices.transferFamilyIndex);
        if(imageInfo.queueFamiliesIndices.presentationFamilyIndex != -1) queueFamilyIndices.push_back(imageInfo.queueFamiliesIndices.presentationFamilyIndex);
        std::sort(queueFamilyIndices.begin(), queueFamilyIndices.end());
        auto uniqueIndices = std::unique(queueFamilyIndices.begin(), queueFamilyIndices.end());
        queueFamilyIndices.erase(uniqueIndices, queueFamilyIndices.end());
        
        imageCreateInfo.queueFamilyIndexCount = queueFamilyIndices.size();
        imageCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data();
        
        vkCreateImage(device, &imageCreateInfo, nullptr, &image);

        //get memory requirements
        imageMemRequirements.sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS;
        imageMemRequirements.pNext = NULL;
        imageMemRequirements.pCreateInfo = &imageCreateInfo;
        imageMemRequirements.planeAspect = imageInfo.imageAspect;

        vkGetDeviceImageMemoryRequirements(device, &imageMemRequirements, &memRequirements);
    }

    Image::~Image()
    {
        vkDestroyImage(device, image, nullptr);
    }

    int Image::assignAllocation(DeviceAllocation* allocation)
    {
        VulkanResource::assignAllocation(allocation);

        //bind memory
        bindingInfo = allocationPtr->bindImage(image, memRequirements.memoryRequirements);
        size = memRequirements.memoryRequirements.size;
        if(bindingInfo.allocatedSize == 0)
        {
            return 1; //error in this case should just be that its out of memory, or wrong memory type was used
        }

        return 0;
    }

    VkImageView Image::getNewImageView(const Image& image, VkDevice device, VkImageAspectFlags aspectMask, VkImageViewType viewType, VkFormat format)
    {
        VkImageSubresourceRange subresource = {};
        subresource.aspectMask = aspectMask;
        subresource.baseMipLevel = 0;
        subresource.levelCount = image.mipmapLevels;
        subresource.baseArrayLayer = 0;
        subresource.layerCount = 1;

        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.pNext = NULL;
        viewInfo.flags = 0;
        viewInfo.image = image.getImage();
        viewInfo.viewType = viewType;
        viewInfo.format = format;
        viewInfo.subresourceRange = subresource;

        VkImageView view;
        VkResult result = vkCreateImageView(device, &viewInfo, nullptr, &view);

        return view;
    }

    void Image::setImageData(const Buffer &imageStagingBuffer)
    {
        //get synchronization stuff
        VkSemaphore copySemaphore = Commands::getSemaphore(device);
        VkFence blitFence = Commands::getUnsignaledFence(device);

        //copy staging buffer into image
        SynchronizationInfo copySynchronizationInfo = {
            .queueType = QueueType::TRANSFER,
            .binaryWaitPairs = {},
            .binarySignalPairs = { { copySemaphore, VK_PIPELINE_STAGE_2_TRANSFER_BIT } },
            .timelineWaitPairs = {},
            .timelineSignalPairs = {},
            .fence = VK_NULL_HANDLE
        };
        creationBuffers.push_back(copyBufferToImage(imageStagingBuffer.getBuffer(), image, copySynchronizationInfo));

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
        vkWaitForFences(device, 1, &blitFence, VK_TRUE, UINT64_MAX);
        vkDestroySemaphore(device, copySemaphore, nullptr);
        vkDestroyFence(device, blitSynchronization.fence, nullptr);

        //destroy old command buffers
        Commands::freeCommandBuffers(device, creationBuffers);
        creationBuffers.clear();
    }

    VkSampler Image::getNewSampler(const Image& image, VkDevice device, VkPhysicalDevice gpu)
    {
        VkPhysicalDeviceFeatures2 features = {};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = NULL;
        vkGetPhysicalDeviceFeatures2(gpu, &features);

        VkPhysicalDeviceProperties2 properties = {};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = NULL;
        vkGetPhysicalDeviceProperties2(gpu, &properties);

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
        samplerInfo.maxLod = image.mipmapLevels;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        VkSampler sampler;
        VkResult result = vkCreateSampler(device, &samplerInfo, nullptr, &sampler);

        return sampler;
    }

    CommandBuffer Image::copyBufferToImage(VkBuffer src, VkImage dst, const SynchronizationInfo& synchronizationInfo)
    {
        VkCommandBuffer transferBuffer = Commands::getCommandBuffer(device, QueueType::TRANSFER);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

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
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
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

        Commands::submitToQueue(device, synchronizationInfo, { transferBuffer });

        return { transferBuffer, TRANSFER };
    }
    
    CommandBuffer Image::generateMipmaps(const SynchronizationInfo& synchronizationInfo)
    {
        //command buffer
        VkCommandBuffer blitBuffer = Commands::getCommandBuffer(device, QueueType::GRAPHICS);

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
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
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
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
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
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = i - 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            };
            imageBlit.srcOffsets[1] = { int32_t(imageInfo.extent.width >> (i - 1)), int32_t(imageInfo.extent.height >> (i - 1)), 1 };
            imageBlit.dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
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
        for(uint32_t i = 0; i < mipmapLevels - 1; i++)
        {
            VkImageMemoryBarrier2 finalImageBarrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout = imageInfo.desiredLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
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

        //final mip level transition (outlier from bad code)
        VkImageMemoryBarrier2 finalImageBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = imageInfo.desiredLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = mipmapLevels - 1,
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
        
        vkEndCommandBuffer(blitBuffer);

        Commands::submitToQueue(device, synchronizationInfo, { blitBuffer });

        return { blitBuffer, synchronizationInfo.queueType };
    }
}