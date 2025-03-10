#pragma once
#include "Device.h"

#include <cstring> //linux bs
#include <functional>
#include <set>

namespace PaperRenderer
{
    struct BufferInfo
    {
        VkDeviceSize size = 0;
        VkBufferUsageFlagBits2KHR usageFlags = 0;
        VmaAllocationCreateFlags allocationFlags = 0;
    };

    struct BufferWrite
    {
        VkDeviceSize offset;
        VkDeviceSize size;
        void const* readData; //data is read from this pointer and written into the buffer
    };

    struct BufferRead
    {
        VkDeviceSize offset;
        VkDeviceSize size;
        void* writeData; //buffer is written to this pointer from the buffer
    };

    //----------RESOURCE BASE CLASS DECLARATIONS----------//

    class VulkanResource
    {
    protected:
        VkDeviceSize size = 0;
        std::set<Queue const*> owners;
        VmaAllocation allocation = VK_NULL_HANDLE;
        std::mutex resourceMutex;

        friend class FragmentableBuffer;

        class RenderEngine& renderer;

    public:
        VulkanResource(class RenderEngine& renderer);
        virtual ~VulkanResource();
        VulkanResource(const VulkanResource&) = delete;

        //thread safe
        void addOwner(const Queue& queue);
        //thread safe
        void removeOwner(const Queue& queue);
        void idleOwners() const;

        VkDeviceSize getSize() const { return size; }
    };

    //----------BUFFER DECLARATIONS----------//

    class Buffer : public VulkanResource
    {
    private:
        VkBuffer buffer = VK_NULL_HANDLE;
        bool writable = false;
        
    public:
        Buffer(class RenderEngine& renderer, const BufferInfo& bufferInfo);
        ~Buffer() override;
        Buffer(const Buffer&) = delete;

        int writeToBuffer(const std::vector<BufferWrite>& writes) const; //returns 0 if successful, 1 if unsuccessful (probably because not host visible)
        int readFromBuffer(const std::vector<BufferRead>& reads) const;
        const Queue& copyFromBufferRanges(const Buffer &src, const std::vector<VkBufferCopy>& regions, const SynchronizationInfo& synchronizationInfo) const;

        const VkBuffer& getBuffer() const { return buffer; }
        const bool& isWritable() const { return writable; }
        VkDeviceAddress getBufferDeviceAddress() const;
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
        Buffer buffer;
        VkDeviceSize desiredLocation = 0;
        VkDeviceSize stackLocation = 0;
        VkDeviceSize totalDataSize = 0;
        const VkDeviceSize minAlignment;

        struct Chunk
        {
            VkDeviceSize location;
            VkDeviceSize size;

            static bool compareByLocation(const Chunk &a, const Chunk& b)
            {
                return a.location < b.location;
            }

            bool operator<(const Chunk& other) const
            {
                return size < other.size;
            }
        };
        std::multiset<Chunk> memoryFragments;

        std::function<void(const std::vector<CompactionResult>&)> compactionCallback = NULL;

        class RenderEngine& renderer;
        VmaAllocation allocation;

    public:
        FragmentableBuffer(class RenderEngine& renderer, const BufferInfo& bufferInfo, VkDeviceSize minAlignment);
        ~FragmentableBuffer();
        FragmentableBuffer(const FragmentableBuffer&) = delete;

        //Callback for when a compaction occurs. Extremely useful for re-referencing, with the function taking in a sorted std::vector<CompactionResult>
        void setCompactionCallback(const std::function<void(std::vector<CompactionResult>)>& compactionCallback) { this->compactionCallback = compactionCallback; }

        enum WriteResult
        {
            SUCCESS = 0, //read/write occured without any notable side effects
            COMPACTED = 1, //buffer was compacted after reaching fragmentation threshold, or to squeeze more available memory out of the buffer
            OUT_OF_MEMORY = 2 //allocation has no available memory for a resize
        };

        //Return location is a pointer to a variable where the write location relative to buffer will be returned. Returns UINT64_MAX into that variable if write failed. Is thread safe
        WriteResult newWrite(void* data, VkDeviceSize size, VkDeviceSize* returnLocation); 
        //thread safe
        void removeFromRange(VkDeviceSize offset, VkDeviceSize size);

        std::vector<CompactionResult> compact(); //inkoves on demand compaction; useful for when recreating an allocation to get the actual current size requirement. results are sorted
        void addOwner(const Queue& queue) { buffer.addOwner(queue); }
        void removeOwner(const Queue& queue) { buffer.removeOwner(queue); };

        Buffer& getBuffer() { return buffer; }
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
        VkImageAspectFlags imageAspect;
        VkImageLayout desiredLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    class Image : public VulkanResource
    {
    private:
        VkImage image;
        const ImageInfo imageInfo;
        uint32_t mipmapLevels;

        void copyBufferToImage(VkBuffer src, VkImage dst, VkCommandBuffer cmdBuffer);
        void generateMipmaps(VkCommandBuffer cmdBuffer);

    public:
        Image(class RenderEngine& renderer, const ImageInfo& imageInfo);
        ~Image() override;
        Image(const Image&) = delete;

        void setImageData(const Buffer& imageStagingBuffer);

        VkImageView getNewImageView(VkImageAspectFlags aspectMask, VkImageViewType viewType, VkFormat format);
        VkSampler getNewSampler(VkFilter magFilter); //filter is whether the sampler uses linear or nearest sampling

        const VkImage& getImage() const { return image; }
        const VkExtent3D getExtent() const { return imageInfo.extent; }
    };
}