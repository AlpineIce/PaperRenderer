#pragma once
#include "VulkanMemory.h"
#include "Command.h"
#include "glm/glm.hpp"

#include <cstring> //linux bs
#include <functional>
#include <stack>
#include <set>

namespace PaperRenderer
{
    namespace PaperMemory
    {
        //MISC RESOURCES 

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
        };

        struct BufferWrite
        {
            VkDeviceSize offset;
            VkDeviceSize size;
            void const* data;
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

            virtual int assignAllocation(DeviceAllocation* allocation); //uses vulkan result for convenience

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
            bool needsFlush = true;
            void* hostDataPtr = NULL;

            void transferQueueFamilyOwnership(
                VkCommandBuffer cmdBuffer,
                VkPipelineStageFlags2 srcStageMask,
                VkAccessFlags2 srcAccessMask,
                VkPipelineStageFlags2 dstStageMask,
                VkAccessFlags2 dstAccessMask,
                uint32_t srcFamily,
                uint32_t dstFamily);
            
        public:
            Buffer(VkDevice device, const BufferInfo& bufferInfo);
            ~Buffer() override;

            int assignAllocation(DeviceAllocation* allocation) override;
            int writeToBuffer(const std::vector<BufferWrite>& writes) const; //returns 0 if successful, 1 if unsuccessful (probably because not host visible)
            CommandBuffer copyFromBufferRanges(Buffer &src, uint32_t transferQueueFamily, const std::vector<VkBufferCopy>& regions, const SynchronizationInfo& synchronizationInfo) const;

            const VkBuffer& getBuffer() const { return buffer; }
            VkDeviceSize getAllocatedSize() const { return bindingInfo.allocatedSize; }
            VkDeviceAddress getBufferDeviceAddress() const;
            void* getHostDataPtr() const { return hostDataPtr; }
            const VkDeviceBufferMemoryRequirements& getBufferMemoryRequirements() const { return bufferMemRequirements; }
        };

        //----------FRAGMENTABLE BUFFER DECLARATIONS----------//

        ///Fragmentable buffers are host visible and can have memory removed from the middle just like normal buffers, but the difference is that the size of the fragment is stored. Once the size of 
        ///the fragments reaches the threshold, as defined in the constructor, the buffer gets compacted, which will move all data in the buffer next to each other. After a compaction, 
        ///any pointers to the data in the buffer should be considered invalid.

        class FragmentableBuffer
        {
        private:
            std::unique_ptr<Buffer> buffer;
            VkDeviceSize stackLocation = 0;
            VkDeviceSize fragmentedSize = 0;
            void* dataPtr = NULL;

            struct Chunk
            {
                VkDeviceSize location;
                VkDeviceSize size;
            };
            std::stack<Chunk> memoryFragments;
            std::set<VkDeviceSize> memoryWriteLocations; //mainly used to redefine old pointers for when compaction occurs

            void verifyFragmentation();

            float fragmentationThreshold;
            float maxFreeSizeThreshold;
            float rebuildOverheadPercentage;

            std::function<void()> compactionCallback;
            std::function<DeviceAllocation*()> newAllocationCallback;

            const VkDevice& device;
            DeviceAllocation* allocationPtr;

        public:
        /// @param device device handle
        /// @param bufferInfo starting buffer info
        /// @param startingAllocation host visible allocation to start with; will need to be updated with callback function if the allocation no longer has a large enough size to support a buffer rebuild
        /// @param fragmentationThreshold percentage value between 0.0 and 1.0 (where 0.2 would be 20%) which specifies when the buffer will automatically defragment. 0.0 would mean defragmentation on every write
        ///        while 1.0 would mean no defragmentation
        /// @param maxFreeSizeThreshold percentage value between 0.0 and 1.0 (like fragmentation threshold) which specifies when the buffer will be rebuilt to avoid using unnecessary allocation space
        /// @param rebuildOverheadPercentage percentage value between 1.0 and some arbitrarily high number which acts as a multiplier to the buffer rebuild size to allocate extra overhead in order to avoid 
        ///        rebuilding the buffer on every new write without a complimentary removal beforehand. Does not apply to initial buffer creation
            FragmentableBuffer(VkDevice device, const BufferInfo& bufferInfo, DeviceAllocation* startingAllocation, float fragmentationThreshold, float maxFreeSizeThreshold, float rebuildOverheadPercentage);
            ~FragmentableBuffer();

            //callback for when a compaction occurs
            void setCompactionCallback(std::function<void()> compactionCallback) { this->compactionCallback = compactionCallback; }
            void setOutOfMemoryCallback(std::function<DeviceAllocation*()> newAllocationCallback) { this->newAllocationCallback = newAllocationCallback; }

            enum ReadWriteResult
            {
                SUCCESS = 0, //read/write occured without any notable side effects
                COMPACTED = 1, //buffer was compacted after reaching fragmentation threshold, or to squeeze more available memory out of the buffer
                REBUILT = 2, //buffer was rebuilt to fit size requirements
                OUT_OF_MEMORY = 3 //allocation has no available memory for a resize
            };

            ReadWriteResult writeToRange(void* data, VkDeviceSize offset, VkDeviceSize size);
            ReadWriteResult removeFromRange(VkDeviceSize offset, VkDeviceSize size);

            void assignNewAllocation(DeviceAllocation* newAllocation);
            void compact(); //inkoves on demand compaction; useful for when recreating an allocation to get the actual current size requirement

            Buffer const* getBufferPtr() { return buffer.get(); }
            float getFragmentedRatio() const { return (buffer->getSize() - fragmentedSize) / buffer->getSize(); } //returns ratio of the allocated buffer size to fragmented size
            const VkDeviceSize& getStackLocation() const { return stackLocation; } //returns the location relative to the start of the buffer (always 0) of where unwritten data is
            const VkDeviceSize& getFragmentedSize() const { return fragmentedSize; } //returns total size of all fragments
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

            int assignAllocation(DeviceAllocation* allocation);
            void setImageData(const Buffer& imageStagingBuffer, VkQueue transferQueue, VkQueue graphicsQueue);

            static VkImageView getNewImageView(const Image& image, VkDevice device, VkImageAspectFlags aspectMask, VkImageViewType viewType, VkFormat format);
            static VkSampler getNewSampler(const Image& image, VkDevice device, VkPhysicalDevice gpu);

            const VkImage& getImage() const { return image; }

            const VkDeviceImageMemoryRequirements& getImageMemoryRequirements() const { return imageMemRequirements; }
        };
    }
}