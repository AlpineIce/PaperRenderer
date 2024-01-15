#pragma once
#include "vulkan/vulkan.hpp"
#include "glm/glm.hpp"
#include "Device.h"
#include "Command.h"

#include <string>

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

    //----------BUFFER DECLARATIONS----------//

    class Buffer
    {
    private:
        VkBuffer stagingBuffer;
        VmaAllocation stagingAllocation;
        bool isDestoyed = false;
        bool stagingCreated = false;

    protected:
        VmaAllocation allocation;

        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;

        VmaAllocationInfo createStagingBuffer(VkDeviceSize bufferSize);
        void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
        void copyBufferToImage(VkBuffer src, VkImage dst, Image* imageData);
        void changeImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout); //mostly from vulkan tutorial
        void destroyStagingAlloc();

        VkBuffer getStagingBuffer() const { return stagingBuffer; }
        VmaAllocation getStagingAllocation() const { return stagingAllocation; }

    public:
        Buffer(Device* device, CmdBufferAllocator* commands);
        virtual ~Buffer();
    };

    //----------VERTEX BUFFER DECLARATIONS----------//

    class VertexBuffer : public Buffer
    {
    private:
        VkBuffer buffer;

        VmaAllocationInfo createVertexBuffer(VkDeviceSize bufferSize);

    public:
        VertexBuffer(Device* device, CmdBufferAllocator* commands, std::vector<Vertex>* vertices);
        ~VertexBuffer() override;

        const VkBuffer& getBuffer() const { return buffer; }
    };

    //----------INDEX BUFFER DECLARATION----------//

    class IndexBuffer : public Buffer
    {
    private:
        VkBuffer buffer;
        VmaAllocationInfo createIndexBuffer(VkDeviceSize bufferSize);
        uint32_t indicesLength;
        
    public:
        IndexBuffer(Device* device, CmdBufferAllocator* commands, std::vector<uint32_t>* indices);
        ~IndexBuffer() override;

        const VkBuffer& getBuffer() const { return buffer; }
        uint32_t getLength() const { return indicesLength; }
    };

    //----------TEXTURE "BUFFER" DECLARATION----------//

    class Texture : public Buffer
    {
    private:
        VkImage texture;
        VkImageView textureView;
        VkSampler sampler;
        uint32_t mipmapLevels;

        VmaAllocationInfo createTexture(Image* imageData);
        void generateMipmaps(Image* imageData);
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
        ~Texture() override;

        VkImage const* getTexturePtr() const { return &texture; }
        const VkImageView& getTextureView() const { return textureView; }
        const VkSampler& getTextureSampler() const { return sampler; }
    };

    //----------UNIFORM BUFFER DECLARATION----------//

    class UniformBuffer : public Buffer
    {
    private:
        VkBuffer buffer;
        VkDeviceSize size;
        void* dataPtr;

    public:
        UniformBuffer(Device *device, CmdBufferAllocator *commands, uint32_t size);
        ~UniformBuffer();

        void updateUniformBuffer(void* updateData, uint32_t offset, uint32_t size);

        const VkBuffer& getBuffer() { return buffer; }
    };

    //----------MESH DECLARATION----------//

    class Mesh
    {
    private:
        const VertexBuffer vbo;
        const IndexBuffer ibo;

    public:
        Mesh(Device* device, CmdBufferAllocator* commands, std::vector<Vertex>* vertices, std::vector<uint32_t>* indices);
        ~Mesh();

        const VkBuffer& getVertexBuffer() const { return vbo.getBuffer(); };
        const VkBuffer& getIndexBuffer() const { return ibo.getBuffer(); };
        uint32_t getIndexBufferSize() const { return ibo.getLength(); }
    };
}