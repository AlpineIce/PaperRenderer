#pragma once
#include "glm/glm.hpp"
#include "Device.h"
#include "Memory/VulkanMemory.h"
#include "Command.h"

#include <list>
#include <memory>

namespace Renderer
{
    //----------BUFFER DATA STRUCTS----------//

    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texCoord;
    };

    struct Image
    {
        void* data = NULL;
        VkDeviceSize size = 0;
        int width, height, channels = 0;
    };

    struct SemaphorePair
    {
        VkSemaphore semaphore;
        VkPipelineStageFlagBits2 stage;
    };

    //----------BUFFER DECLARATIONS----------//

    class Buffer
    {
    protected:
        VkBuffer buffer;
        VmaAllocation allocation;
        VmaAllocationInfo allocInfo;
        VkDeviceSize size;

        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;

        CommandBuffer copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size, const std::vector<SemaphorePair>& waitPairs, const std::vector<SemaphorePair>& signalPairs, const VkFence& fence);
    public:
        Buffer(Device* device, CmdBufferAllocator* commands, VkDeviceSize size);
        virtual ~Buffer();

        void createBuffer(VkBufferUsageFlags usage, VmaMemoryUsage memUsage, VmaAllocationCreateFlags memFlag);
        CommandBuffer copyFromBuffer(Buffer& src, const std::vector<SemaphorePair>& waitPairs, const std::vector<SemaphorePair>& signalPairs, const VkFence& fence);
        CommandBuffer copyFromBufferRanges(Buffer& src, const std::vector<SemaphorePair>& waitPairs, const std::vector<SemaphorePair>& signalPairs, const VkFence& fence, const std::vector<VkBufferCopy>& regions);

        const VkBuffer& getBuffer() const { return buffer; }
        VkDeviceSize getAllocatedSize() const { return size; }
        VkDeviceAddress getBufferDeviceAddress() const;
    };

    //----------STAGING BUFFER DECLARATIONS----------//

    class StagingBuffer : public Buffer
    {
    private:
        
    public:
        StagingBuffer(Device* device, CmdBufferAllocator* commands, VkDeviceSize size);
        ~StagingBuffer() override;

        void mapData(void* data, VkDeviceSize bytesOffset, VkDeviceSize size);
    };

    //----------VERTEX BUFFER DECLARATIONS----------//

    class VertexBuffer : public Buffer
    {
    private:
        uint32_t verticesLength;

        void createVertexBuffer();

    public:
        VertexBuffer(Device* device, CmdBufferAllocator* commands, std::vector<Vertex>* vertices);
        ~VertexBuffer() override;

        uint32_t getLength() const { return verticesLength; }
    };

    //----------INDEX BUFFER DECLARATION----------//

    class IndexBuffer : public Buffer
    {
    private:
        uint32_t indicesLength;

        void createIndexBuffer();
        
    public:
        IndexBuffer(Device* device, CmdBufferAllocator* commands, std::vector<uint32_t>* indices);
        ~IndexBuffer() override;

        uint32_t getLength() const { return indicesLength; }
    };

    //----------TEXTURE DECLARATION----------//

    class Texture
    {
    private:
        VkImage texture;
        VkImageView textureView;
        VkSampler sampler;
        uint32_t mipmapLevels;
        VkDeviceSize size;
        VmaAllocation allocation;
        VmaAllocationInfo allocInfo;
        std::vector<CommandBuffer> creationBuffers;

        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;

        VkSemaphore changeImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout); //mostly from vulkan tutorial
        VkSemaphore copyBufferToImage(VkBuffer src, VkImage dst, Image* imageData, const VkSemaphore& waitSemaphore);
        void createTexture(Image* imageData);
        void generateMipmaps(Image* imageData, const VkSemaphore& waitSemaphore);
        void createTextureView();
        void createSampler();
        //monster function
        void injectMemBarrier(const VkCommandBuffer& command,
            const VkImage& image,
            VkAccessFlags srcAccess,
            VkAccessFlags dstAccess,
            VkImageLayout srcLayout,
            VkImageLayout dstLayout,
            VkPipelineStageFlags srcMask,
            VkPipelineStageFlags dstMask, 
            uint32_t baseMipLevel,
            uint32_t levels);

    public:
        Texture(Device* device, CmdBufferAllocator* commands, Image* imageData);
        ~Texture();

        VkImage const* getTexturePtr() const { return &texture; }
        const VkImageView& getTextureView() const { return textureView; }
        const VkSampler& getTextureSampler() const { return sampler; }
    };

    //----------UNIFORM BUFFER DECLARATION----------//

    class UniformBuffer : public Buffer
    {
    private:
        void* dataPtr;

    public:
        UniformBuffer(Device *device, CmdBufferAllocator *commands, VkDeviceSize size);
        ~UniformBuffer() override;

        void updateUniformBuffer(void const* updateData, VkDeviceSize size);
    };

    //----------STORAGE BUFFER DECLARATION----------//

    class StorageBuffer : public Buffer
    {
    private:
        void createStorageBuffer();

    public:
        StorageBuffer(Device *device, CmdBufferAllocator *commands, VkDeviceSize size);
        ~StorageBuffer() override;

        CommandBuffer setDataFromStaging(const StagingBuffer& stagingBuffer, VkDeviceSize size, const std::vector<SemaphorePair>& waitPairs, const std::vector<SemaphorePair>& signalPairs, const VkFence& fence);
        const VkBuffer& getBuffer() const { return buffer; }
    };
}