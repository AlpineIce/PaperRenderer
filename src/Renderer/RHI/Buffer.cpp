#include "Buffer.h"

namespace Renderer
{
    //-----------BASE BUFFER DEFINITIONS----------//

    Buffer::Buffer(Device* device, Commands* commands)
        :devicePtr(device),
        commandsPtr(commands)
    {
    }

    Buffer::~Buffer()
    {
        if(stagingCreated)
        {
            destroyStagingAlloc();
        }
    }

    VmaAllocationInfo Buffer::createStagingBuffer(VkDeviceSize bufferSize)
    {
        stagingCreated = true;
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = NULL;
        bufferInfo.flags = 0;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.queueFamilyIndexCount = 0;
        bufferInfo.pQueueFamilyIndices = NULL;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VmaAllocationInfo returnInfo;
        VkResult result = vmaCreateBuffer(devicePtr->getAllocator(), &bufferInfo, &allocCreateInfo, &stagingBuffer, &stagingAllocation, &returnInfo);

        return returnInfo;
    }

    void Buffer::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
    {
        VkCommandBuffer transferBuffer = commandsPtr->getCommandBuffersPtr()->transfer.at(0); //note theres only 1 transfer cmd buffer

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

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &transferBuffer;

        VkResult result = vkQueueSubmit(devicePtr->getQueues().transfer.at(0), 1, &submitInfo, VK_NULL_HANDLE);

        vkQueueWaitIdle(devicePtr->getQueues().transfer.at(0));
        vkResetCommandBuffer(transferBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
    }

    void Buffer::changeImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        VkCommandBuffer transferBuffer = commandsPtr->getCommandBuffersPtr()->transfer.at(0); //note theres only 1 transfer cmd buffer

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

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &transferBuffer;

        VkResult result = vkQueueSubmit(devicePtr->getQueues().transfer.at(0), 1, &submitInfo, VK_NULL_HANDLE);

        vkQueueWaitIdle(devicePtr->getQueues().transfer.at(0));
        vkResetCommandBuffer(transferBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
    }

    void Buffer::copyBufferToImage(VkBuffer src, VkImage dst, Image* imageData)
    {
        VkCommandBuffer transferBuffer = commandsPtr->getCommandBuffersPtr()->transfer.at(0); //note theres only 1 transfer cmd buffer

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

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &transferBuffer;

        VkResult result = vkQueueSubmit(devicePtr->getQueues().transfer.at(0), 1, &submitInfo, VK_NULL_HANDLE);

        vkQueueWaitIdle(devicePtr->getQueues().transfer.at(0));
        vkResetCommandBuffer(transferBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
    }

    void Buffer::destroyStagingAlloc()
    {
        if(!isDestoyed)
        {
            isDestoyed = true;
            vmaDestroyBuffer(devicePtr->getAllocator(), stagingBuffer, stagingAllocation);
        }
    }

    //-----------VERTEX BUFFER DEFINITIONS----------//

    VertexBuffer::VertexBuffer(Device* device, Commands* commands, std::vector<Vertex>* vertices)
        :Buffer(device, commands)
    {
        //get size in bytes
        VkDeviceSize bytesSize = vertices->size() * sizeof(Vertex);

        //staging buffer
        VmaAllocationInfo allocInfo = createStagingBuffer(bytesSize);

        //copy data
        memcpy(allocInfo.pMappedData, vertices->data(), bytesSize);

        //then vertex buffer
        VmaAllocationInfo allocInfo2 = createVertexBuffer(bytesSize);

        //copy and destroy staging
        copyBuffer(getStagingBuffer(), buffer, bytesSize);
        destroyStagingAlloc();
    }

    VertexBuffer::~VertexBuffer()
    {
        vmaDestroyBuffer(devicePtr->getAllocator(), buffer, allocation);
    }

    VmaAllocationInfo VertexBuffer::createVertexBuffer(VkDeviceSize bufferSize)
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = NULL;
        bufferInfo.flags = 0;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.queueFamilyIndexCount = 0;
        bufferInfo.pQueueFamilyIndices = NULL;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VmaAllocationInfo returnInfo;
        VkResult result = vmaCreateBuffer(devicePtr->getAllocator(), &bufferInfo, &allocCreateInfo, &buffer, &allocation, &returnInfo);

        return returnInfo;
    }

    //----------INDEX BUFFER DEFINITIONS----------//

    IndexBuffer::IndexBuffer(Device* device, Commands* commands, std::vector<uint32_t>* indices)
        :Buffer(device, commands)
    {
        //get sizes
        VkDeviceSize bytesSize = indices->size() * sizeof(uint32_t);
        indicesLength = indices->size();

        //staging buffer
        VmaAllocationInfo allocInfo = createStagingBuffer(bytesSize);

        //copy data
        memcpy(allocInfo.pMappedData, indices->data(), bytesSize);

        //then index buffer
        VmaAllocationInfo allocInfo2 = createIndexBuffer(bytesSize);

        //copy and destroy staging
        copyBuffer(getStagingBuffer(), buffer, bytesSize);
        destroyStagingAlloc();
    }

    IndexBuffer::~IndexBuffer()
    {
        vmaDestroyBuffer(devicePtr->getAllocator(), buffer, allocation);
    }

    VmaAllocationInfo IndexBuffer::createIndexBuffer(VkDeviceSize bufferSize)
    {
        
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = NULL;
        bufferInfo.flags = 0;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.queueFamilyIndexCount = 0;
        bufferInfo.pQueueFamilyIndices = NULL;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VmaAllocationInfo returnInfo;
        VkResult result = vmaCreateBuffer(devicePtr->getAllocator(), &bufferInfo, &allocCreateInfo, &buffer, &allocation, &returnInfo);

        return returnInfo;
    }

    //----------TEXTURE "BUFFER" DEFINITIONS----------//

    Texture::Texture(Device* device, Commands* commands, Image* imageData)
        :Buffer(device, commands)
    {
        //staging buffer
        VmaAllocationInfo allocInfo = createStagingBuffer(imageData->size);

        //copy data
        memcpy(allocInfo.pMappedData, imageData->data, imageData->size);

        //create texture
        VmaAllocationInfo allocInfo2 = createTexture(imageData);

        //copy
        changeImageLayout(texture, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(getStagingBuffer(), texture, imageData);
        generateMipmaps(imageData);

        //staging buffer destruction
         destroyStagingAlloc();

        //create image view and sampler
        createTextureView();
        createSampler();
    }

    Texture::~Texture()
    {
        vkDestroySampler(devicePtr->getDevice(), sampler, nullptr);
        vkDestroyImageView(devicePtr->getDevice(), textureView, nullptr);
        vmaDestroyImage(devicePtr->getAllocator(), texture, allocation);
    }

    VmaAllocationInfo Texture::createTexture(Image* imageData)
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
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VmaAllocationInfo returnInfo;
        VkResult result = vmaCreateImage(devicePtr->getAllocator(), &imageInfo, &allocCreateInfo, &texture, &allocation, &returnInfo);

        return returnInfo;
    }

    void Texture::generateMipmaps(Image* imageData)
    {
        VkCommandBuffer blitBuffer = commandsPtr->getCommandBuffersPtr()->graphics.at(1);

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

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &blitBuffer;

        VkResult result = vkQueueSubmit(devicePtr->getQueues().graphics.at(0), 1, &submitInfo, VK_NULL_HANDLE);

        vkQueueWaitIdle(devicePtr->getQueues().graphics.at(0));
        vkResetCommandBuffer(blitBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
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
        samplerInfo.maxAnisotropy = devicePtr->getGPUProperties().limits.maxSamplerAnisotropy;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
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

    UniformBuffer::UniformBuffer(Device *device, Commands *commands, uint32_t size)
        :Buffer(device, commands),
        dataPtr(NULL),
        size(size)
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = NULL;
        bufferInfo.flags = 0;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.queueFamilyIndexCount = 0;
        bufferInfo.pQueueFamilyIndices = NULL;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        VmaAllocationInfo allocInfo;
        VkResult result = vmaCreateBuffer(devicePtr->getAllocator(), &bufferInfo, &allocCreateInfo, &buffer, &allocation, &allocInfo);
        dataPtr = allocInfo.pMappedData;
    }

    UniformBuffer::~UniformBuffer()
    {
        vmaDestroyBuffer(devicePtr->getAllocator(), buffer, allocation);
    }

    void UniformBuffer::updateUniformBuffer(void* updateData, uint32_t offset, uint32_t size)
    {
        memcpy((unsigned char*)dataPtr + offset, updateData, size);
        vmaFlushAllocation(devicePtr->getAllocator(), allocation, offset, size);
    }

    //----------MESH DEFINITIONS----------//

    Mesh::Mesh(Device* device, Commands* commands, std::vector<Vertex>* vertices, std::vector<uint32_t>* indices)
        :vbo(device, commands, vertices),
        ibo(device, commands, indices)
    {

    }

    Mesh::~Mesh()
    {

    }

}