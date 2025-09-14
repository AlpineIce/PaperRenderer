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
        std::set<Queue*> owners;
        VmaAllocation allocation = VK_NULL_HANDLE;
        std::mutex resourceMutex;

        friend class FragmentableBuffer;

        class RenderEngine* renderer;

    public:
        VulkanResource(class RenderEngine& renderer);
        virtual ~VulkanResource();
        VulkanResource(const VulkanResource&) = delete;
        VulkanResource(VulkanResource&& other) noexcept;
        VulkanResource& operator=(VulkanResource&& other) noexcept;

        //thread safe
        void addOwner(Queue& queue);
        //thread safe
        void removeOwner(Queue& queue);
        void idleOwners();

        const VmaAllocation& getAllocation() const { return allocation; }
        VkMemoryType getMemoryType() const;
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
        Buffer(Buffer&& other) noexcept;
        Buffer& operator=(Buffer&& other) noexcept;

        int writeToBuffer(const std::vector<BufferWrite>& writes) const; //returns 0 if successful, 1 if unsuccessful (probably because not host visible)
        int readFromBuffer(const std::vector<BufferRead>& reads) const;
        Queue& copyFromBufferRanges(const Buffer &src, const std::vector<VkBufferCopy>& regions, const SynchronizationInfo& synchronizationInfo) const;

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
        VkDeviceSize minAlignment = 0;

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

        class RenderEngine* renderer;
        VmaAllocation allocation;

    public:
        FragmentableBuffer(class RenderEngine& renderer, const BufferInfo& bufferInfo, VkDeviceSize minAlignment);
        ~FragmentableBuffer();
        FragmentableBuffer(const FragmentableBuffer&) = delete;
        FragmentableBuffer(FragmentableBuffer&& other) noexcept;
        FragmentableBuffer& operator=(FragmentableBuffer&& other) noexcept;

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
        void addOwner(Queue& queue) { buffer.addOwner(queue); }
        void removeOwner(Queue& queue) { buffer.removeOwner(queue); };

        Buffer& getBuffer() { return buffer; }
        const VkDeviceSize& getStackLocation() const { return stackLocation; } //returns the location relative to the start of the buffer (always 0) of where unwritten data is
        const VkDeviceSize& getDesiredLocation() const { return desiredLocation; } //useful if write failed to give a the stackLocation + (size of last write)
    };

    //----------IMAGE DECLARATIONS----------//

    struct ImageInfo
    {
        VkImageType imageType = VK_IMAGE_TYPE_2D;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent3D extent = { 0, 0, 0 };
        uint32_t maxMipLevels = 1; //maximum number of mip levels to create, including the base level. arbitrarily high number can be used for maximum mip levels, such as UINT32_MAX
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        VkImageUsageFlags usage = 0;
        VkImageAspectFlags imageAspect = 0;
        VkImageLayout desiredLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    class Image : public VulkanResource
    {
    private:
        VkImage image = VK_NULL_HANDLE;
        ImageInfo imageInfo = {};
        uint32_t mipmapLevels = 0;

        void copyBufferToImage(VkBuffer src, VkImage dst, VkCommandBuffer cmdBuffer, const VkDeviceSize srcOffset, const VkOffset3D dstOffset);
        void generateMipmaps(VkCommandBuffer cmdBuffer);

    public:
        Image(class RenderEngine& renderer, const ImageInfo& imageInfo);
        ~Image() override;
        Image(const Image&) = delete;
        Image(Image&& other) noexcept;
        Image& operator=(Image&& other) noexcept;

        void setImageData(const VkDeviceSize size, void const* data, const VkOffset3D dstOffset);
        void setImageData(const Buffer& imageStagingBuffer, const VkDeviceSize srcOffset, const VkOffset3D dstOffset);

        VkImageView getNewImageView(VkImageAspectFlags aspectMask, VkImageViewType viewType, VkFormat format);
        VkSampler getNewSampler(VkFilter magFilter); //filter is whether the sampler uses linear or nearest sampling

        const VkImage& getImage() const { return image; }
        const VkExtent3D getExtent() const { return imageInfo.extent; }
    };
}