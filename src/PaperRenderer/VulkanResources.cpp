#include "VulkanResources.h"
#include "PaperRenderer.h"

#include <stdexcept>
#include <algorithm>

namespace PaperRenderer
{
    //----------RESOURCE BASE CLASS DEFINITIONS----------//

    VulkanResource::VulkanResource(RenderEngine* renderer)
        :rendererPtr(renderer)
    {
    }

    VulkanResource::~VulkanResource()
    {
    }
    
    //----------BUFFER DEFINITIONS----------//

    Buffer::Buffer(RenderEngine* renderer, const BufferInfo& bufferInfo)
        :VulkanResource(renderer)
    {
        VkBufferCreateInfo bufferCreateInfo = {};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.pNext = NULL;
        bufferCreateInfo.flags = 0;
        bufferCreateInfo.size = bufferInfo.size;
        bufferCreateInfo.usage = bufferInfo.usageFlags;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;

        const QueueFamiliesIndices& deviceQueueFamilies = rendererPtr->getDevice()->getQueueFamiliesIndices();
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
        VkResult result = vmaCreateBuffer(rendererPtr->getDevice()->getAllocator(), &bufferCreateInfo, &allocCreateInfo, &buffer, &allocation, &allocInfo);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Buffer creation failed");
        }

        size = bufferInfo.size;
    }

    Buffer::~Buffer()
    {
        vkDestroyBuffer(rendererPtr->getDevice()->getDevice(), buffer, nullptr);
    }

    int Buffer::writeToBuffer(const std::vector<BufferWrite>& writes) const
    {
        //write data
        for(const BufferWrite& write : writes)
        {
            if(write.data && write.size)
            {
                if(vmaCopyMemoryToAllocation(rendererPtr->getDevice()->getAllocator(), write.data, allocation, write.offset, write.size) != VK_SUCCESS) return 1;
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
               if(vmaCopyAllocationToMemory(rendererPtr->getDevice()->getAllocator(), allocation, read.offset, read.data, read.size) != VK_SUCCESS) return 1;
            }
        }

        return 0;
    }

    CommandBuffer Buffer::copyFromBufferRanges(Buffer &src, const std::vector<VkBufferCopy>& regions, const SynchronizationInfo& synchronizationInfo) const
    {
        VkCommandBuffer transferBuffer = Commands::getCommandBuffer(rendererPtr, QueueType::TRANSFER); //note theres only 1 transfer cmd buffer

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

        Commands::submitToQueue(synchronizationInfo, commandBuffers);
        
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

            return vkGetBufferDeviceAddress(rendererPtr->getDevice()->getDevice(), &deviceAddressInfo);
        }
        else
        {
            return 0;
        }
    }

    //----------FRAGMENTABLE BUFFER DEFINITIONS----------//

    FragmentableBuffer::FragmentableBuffer(RenderEngine* renderer, const BufferInfo &bufferInfo)
        :rendererPtr(renderer)
    {
        buffer = std::make_unique<Buffer>(rendererPtr, bufferInfo);
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
        desiredLocation += rendererPtr->getDevice()->getAlignment(size, minAlignment);
        desiredLocation += rendererPtr->getDevice()->getAlignment(size, minAlignment);

        if(stackLocation + rendererPtr->getDevice()->getAlignment(size, minAlignment) > buffer->getSize())
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

        BufferWrite write = {};
        write.data = data;
        write.offset = rendererPtr->getDevice()->getAlignment(stackLocation, minAlignment);
        write.size = size;
        
        buffer->writeToBuffer({ write });
        if(returnLocation) *returnLocation = rendererPtr->getDevice()->getAlignment(stackLocation, minAlignment);

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
            //move data from memory fragment (location + size) into (location), subtract (size) from (stackLocation), and remove memory fragment from stack
            while(memoryFragments.size())
            {

                //init temporary variables and chunk ref
                const Chunk& chunk = memoryFragments.top();
                std::vector<char> readData(stackLocation - (chunk.location + chunk.size));

                //read data
                BufferWrite read = {};
                read.offset = chunk.location + chunk.size;
                read.size = stackLocation - (chunk.location + chunk.size);
                read.data = readData.data();
                buffer->readFromBuffer({ read });

                //copy data into buffer location
                BufferWrite write = {};
                write.offset = chunk.location;
                write.size = stackLocation - (chunk.location + chunk.size);
                write.data = readData.data();
                buffer->writeToBuffer({ write });

                //move stack "pointer" and remove fragment
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

    Image::Image(RenderEngine* renderer, const ImageInfo& imageInfo)
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

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = 0;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VmaAllocationInfo allocInfo = {};
        if(vmaCreateImage(rendererPtr->getDevice()->getAllocator(), &imageCreateInfo, &allocCreateInfo, &image, &allocation, &allocInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("Buffer creation failed");
        }

        size = 0; //TODO?
    }

    Image::~Image()
    {
        vkDestroyImage(rendererPtr->getDevice()->getDevice(), image, nullptr);
    }

    VkImageView Image::getNewImageView(const Image& image, RenderEngine* renderer, VkImageAspectFlags aspectMask, VkImageViewType viewType, VkFormat format)
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
        VkResult result = vkCreateImageView(renderer->getDevice()->getDevice(), &viewInfo, nullptr, &view);

        return view;
    }

    void Image::setImageData(const Buffer &imageStagingBuffer)
    {
        //get synchronization stuff
        VkSemaphore copySemaphore = Commands::getSemaphore(rendererPtr);
        VkFence blitFence = Commands::getUnsignaledFence(rendererPtr);

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
        vkWaitForFences(rendererPtr->getDevice()->getDevice(), 1, &blitFence, VK_TRUE, UINT64_MAX);
        vkDestroySemaphore(rendererPtr->getDevice()->getDevice(), copySemaphore, nullptr);
        vkDestroyFence(rendererPtr->getDevice()->getDevice(), blitSynchronization.fence, nullptr);

        //destroy old command buffers
        Commands::freeCommandBuffers(rendererPtr, creationBuffers);
        creationBuffers.clear();
    }

    VkSampler Image::getNewSampler(const Image& image, RenderEngine* renderer)
    {
        VkPhysicalDeviceFeatures2 features = {};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = NULL;
        vkGetPhysicalDeviceFeatures2(renderer->getDevice()->getGPU(), &features);

        VkPhysicalDeviceProperties2 properties = {};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = NULL;
        vkGetPhysicalDeviceProperties2(renderer->getDevice()->getGPU(), &properties);

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
        VkResult result = vkCreateSampler(renderer->getDevice()->getDevice(), &samplerInfo, nullptr, &sampler);

        return sampler;
    }

    CommandBuffer Image::copyBufferToImage(VkBuffer src, VkImage dst, const SynchronizationInfo& synchronizationInfo)
    {
        VkCommandBuffer transferBuffer = Commands::getCommandBuffer(rendererPtr, QueueType::TRANSFER);

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

        Commands::submitToQueue(synchronizationInfo, { transferBuffer });

        return { transferBuffer, TRANSFER };
    }
    
    CommandBuffer Image::generateMipmaps(const SynchronizationInfo& synchronizationInfo)
    {
        //command buffer
        VkCommandBuffer blitBuffer = Commands::getCommandBuffer(rendererPtr, QueueType::GRAPHICS);

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

        Commands::submitToQueue(synchronizationInfo, { blitBuffer });

        return { blitBuffer, synchronizationInfo.queueType };
    }
}