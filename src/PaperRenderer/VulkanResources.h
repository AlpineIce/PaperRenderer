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
        QueueFamiliesIndices queueFamiliesIndices = {};
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
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceBufferMemoryRequirements bufferMemRequirements;
        bool needsFlush = true;
        void* hostDataPtr = NULL;
        
    public:
        Buffer(VkDevice device, const BufferInfo& bufferInfo);
        ~Buffer() override;

        int assignAllocation(DeviceAllocation* allocation) override;
        int writeToBuffer(const std::vector<BufferWrite>& writes) const; //returns 0 if successful, 1 if unsuccessful (probably because not host visible)
        CommandBuffer copyFromBufferRanges(Buffer &src, const std::vector<VkBufferCopy>& regions, const SynchronizationInfo& synchronizationInfo) const;

        const VkBuffer& getBuffer() const { return buffer; }
        VkDeviceSize getAllocatedSize() const { return bindingInfo.allocatedSize; }
        VkDeviceAddress getBufferDeviceAddress() const;
        void* getHostDataPtr() const { return hostDataPtr; }
        const VkDeviceBufferMemoryRequirements& getBufferMemoryRequirements() const { return bufferMemRequirements; }
    };

    //----------FRAGMENTABLE BUFFER DECLARATIONS----------//

    

    /// @brief location represents the location where all data after is to be shifted down by shiftSize
    struct CompactionResult
    {
        VkDeviceSize location;
        VkDeviceSize shiftSize;
    };

    /// @brief Fragmentable buffers are host visible and can have memory removed from the middle just like normal buffers, but the difference is that the size of the fragment is stored. Once the size of 
    ///the fragments reaches the threshold, as defined in the constructor, the buffer gets compacted, which will move all data in the buffer next to each other. After a compaction, 
    ///any pointers to the data in the buffer should be considered invalid.
    class FragmentableBuffer
    {
    private:
        std::unique_ptr<Buffer> buffer;
        VkDeviceSize desiredLocation = 0;
        VkDeviceSize stackLocation = 0;

        struct Chunk
        {
            VkDeviceSize location;
            VkDeviceSize size;
        };
        std::stack<Chunk> memoryFragments;
        std::set<VkDeviceSize> memoryWriteLocations; //mainly used to redefine old pointers for when compaction occurs

        void verifyFragmentation();

        std::function<void(std::vector<CompactionResult>)> compactionCallback = NULL;

        const VkDevice& device;
        DeviceAllocation* allocationPtr = NULL;

    public:
        FragmentableBuffer(VkDevice device, const BufferInfo& bufferInfo);
        ~FragmentableBuffer();

        //Callback for when a compaction occurs. Extremely useful for re-referencing, with the function taking in std::vector<CompactionResult>
        void setCompactionCallback(std::function<void(std::vector<CompactionResult>)> compactionCallback) { this->compactionCallback = compactionCallback; }

        enum WriteResult
        {
            SUCCESS = 0, //read/write occured without any notable side effects
            COMPACTED = 1, //buffer was compacted after reaching fragmentation threshold, or to squeeze more available memory out of the buffer
            OUT_OF_MEMORY = 2 //allocation has no available memory for a resize
        };

        void assignAllocation(DeviceAllocation* newAllocation);

        //Return location is a pointer to a variable where the write location relative to buffer will be returned. Returns UINT64_MAX into that variable if write failed
        WriteResult newWrite(void* data, VkDeviceSize size, VkDeviceSize minAlignment, VkDeviceSize* returnLocation); 
        void removeFromRange(VkDeviceSize offset, VkDeviceSize size);

        std::vector<CompactionResult> compact(); //inkoves on demand compaction; useful for when recreating an allocation to get the actual current size requirement

        Buffer* getBuffer() { return buffer.get(); }
        const VkDeviceSize& getStackLocation() const { return stackLocation; } //returns the location relative to the start of the buffer (always 0) of where unwritten data is
        const VkDeviceSize& getDesiredLocation() const { return desiredLocation; } //useful if write failed to give a the stackLocation + (size of last write)
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
        QueueFamiliesIndices queueFamiliesIndices = {};
        VkImageLayout desiredLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    class Image : public VulkanResource
    {
    private:
        VkImage image;
        const ImageInfo imageInfo;
        VkDeviceImageMemoryRequirements imageMemRequirements;
        uint32_t mipmapLevels;
        std::vector<CommandBuffer> creationBuffers;

        CommandBuffer copyBufferToImage(VkBuffer src, VkImage dst, const SynchronizationInfo& synchronizationInfo);
        CommandBuffer generateMipmaps(const SynchronizationInfo& synchronizationInfo);

    public:
        Image(VkDevice device, const ImageInfo& imageInfo);
        ~Image() override;

        int assignAllocation(DeviceAllocation* allocation);
        void setImageData(const Buffer& imageStagingBuffer);

        static VkImageView getNewImageView(const Image& image, VkDevice device, VkImageAspectFlags aspectMask, VkImageViewType viewType, VkFormat format);
        static VkSampler getNewSampler(const Image& image, VkDevice device, VkPhysicalDevice gpu);

        const VkImage& getImage() const { return image; }
        const VkExtent3D getExtent() const { return imageInfo.extent; }\
        const VkDeviceImageMemoryRequirements& getImageMemoryRequirements() const { return imageMemRequirements; }
    };
}