#pragma once
#include "VulkanMemory.h"
#include "Command.h"
#include "glm/glm.hpp"

namespace PaperRenderer
{
    namespace PaperMemory
    {
        //MISC RESOURCES 

        struct Vertex
        {
            glm::vec3 position;
            glm::vec3 normal;
            glm::vec2 texCoord;
        };

        struct ImageData
        {
            void* data = NULL;
            VkDeviceSize size = 0;
            int width, height, channels = 0;
        };

        struct BufferInfo
        {
            VkDeviceSize size = 0;
            VkBufferUsageFlagBits2KHR usageFlags;
            std::vector<uint32_t> queueFamilyIndices = {};
        };

        //----------RESOURCE BASE CLASS DECLARATIONS----------//

        class VulkanResource
        {
        protected:
            VkDeviceSize size = 0;
            ResourceBindingInfo bindingInfo;
            VkMemoryRequirements2 memRequirements;

            VkDevice device;
            DeviceAllocation* allocationPtr;

            virtual VkResult assignAllocation(DeviceAllocation* allocation); //uses vulkan result for convenience

        public:
            VulkanResource(VkDevice device);
            virtual ~VulkanResource();

            VkDeviceSize getSize() const { return size; }
            VkMemoryRequirements getMemoryRequirements() const { return memRequirements.memoryRequirements; }
        };

        //----------BUFFER DECLARATIONS----------//

        class Buffer : public VulkanResource
        {
        private:
            VkBuffer buffer;
            VkDeviceBufferMemoryRequirements bufferMemRequirements;
            
        public:
            Buffer(VkDevice device, const BufferInfo& bufferInfo);
            ~Buffer() override;

            VkResult assignAllocation(DeviceAllocation* allocation) override;
            CommandBuffer copyFromBufferRanges(Buffer& src, const std::vector<VkBufferCopy>& regions, const SynchronizationInfo& synchronizationInfo);

            const VkBuffer& getBuffer() const { return buffer; }
            VkDeviceSize getAllocatedSize() const { return bindingInfo.allocatedSize; }
            VkDeviceAddress getBufferDeviceAddress() const;

            const VkDeviceBufferMemoryRequirements& getBufferMemoryRequirements() const { return bufferMemRequirements; }
        };

        //----------IMAGE DECLARATIONS----------//

        struct ImageInfo
        {
            VkImageType imageType = VK_IMAGE_TYPE_2D;
            VkFormat format;
            VkExtent3D extent;
            uint32_t maxMipLevels = 1; //maximum number of mip levels to create, including the base level. arbitrarily high number can be used for maximum mip levels, such as UINT32_MAX
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
            VkImageUsageFlags usage;
            VkImageAspectFlagBits imageAspect;
            std::vector<uint32_t> queueFamilyIndices = {};
        };

        class Image : public VulkanResource
        {
        private:
            VkImage image;
            ImageInfo imageInfo;
            VkDeviceImageMemoryRequirements imageMemRequirements;
            uint32_t mipmapLevels;
            std::vector<CommandBuffer> creationBuffers;

            struct ImageMemoryBarrierInfo
            {
                const VkCommandBuffer& command;
                const VkImage& image;
                VkAccessFlags srcAccess;
                VkAccessFlags dstAccess;
                VkImageLayout srcLayout;
                VkImageLayout dstLayout;
                VkPipelineStageFlags srcMask;
                VkPipelineStageFlags dstMask;
                uint32_t baseMipLevel;
                uint32_t levels;
            };

            CommandBuffer changeImageLayout(VkImage image, const SynchronizationInfo& synchronizationInfo, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout); //mostly from vulkan tutorial
            CommandBuffer copyBufferToImage(VkBuffer src, VkImage dst, VkExtent3D imageExtent, const SynchronizationInfo& synchronizationInfo);
            CommandBuffer generateMipmaps(VkExtent3D imageExtent, const SynchronizationInfo& synchronizationInfo);
            void injectMemBarrier(ImageMemoryBarrierInfo barrierInfo);

        public:
            Image(VkDevice device, const ImageInfo& imageInfo);
            ~Image() override;

            VkResult assignAllocation(DeviceAllocation* allocation);
            void setImageData(const Buffer& imageStagingBuffer, VkQueue transferQueue, VkQueue graphicsQueue);

            static VkImageView getNewImageView(const Image& image, VkDevice device, VkImageAspectFlags aspectMask, VkImageViewType viewType, VkFormat format);
            static VkSampler getNewSampler(const Image& image, VkDevice device, VkPhysicalDevice gpu);

            const VkImage& getImage() const { return image; }

            const VkDeviceImageMemoryRequirements& getImageMemoryRequirements() const { return imageMemRequirements; }
        };
    }
}