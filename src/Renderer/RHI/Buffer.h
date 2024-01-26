#pragma once
#include "glm/glm.hpp"
#include "Device.h"
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

        QueueReturn copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size, bool useFence);
    public:
        Buffer(Device* device, CmdBufferAllocator* commands, VkDeviceSize size);
        virtual ~Buffer();

        void createBuffer(VkBufferUsageFlags usage, VmaMemoryUsage memUsage, VmaAllocationCreateFlags memFlag);
        QueueReturn copyFromBuffer(Buffer& src, bool useFence);

        const VkBuffer& getBuffer() const { return buffer; }
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

    //----------MESH DECLARATION----------//

    class Mesh
    {
    private:
        const VertexBuffer vbo;
        const IndexBuffer ibo;

    public:
        Mesh(Device* device, CmdBufferAllocator* commands, std::vector<Vertex>* vertices, std::vector<uint32_t>* indices);
        ~Mesh();

        const VertexBuffer& getVertexBuffer() const { return vbo; };
        const IndexBuffer& getIndexBuffer() const { return ibo; };
    };

    struct ModelMesh
    {
        std::shared_ptr<Mesh> mesh;
        uint32_t materialIndex;
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

        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;

        void copyBufferToImage(VkBuffer src, VkImage dst, Image* imageData);
        void changeImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout); //mostly from vulkan tutorial
        void createTexture(Image* imageData);
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

        void updateUniformBuffer(void* updateData, VkDeviceSize size);
    };

    //----------STORAGE BUFFER DECLARATION----------//

    class StorageBuffer : public Buffer
    {
    private:
        void createStorageBuffer();

    public:
        StorageBuffer(Device *device, CmdBufferAllocator *commands, VkDeviceSize size);
        ~StorageBuffer() override;

        QueueReturn setDataFromStaging(const StagingBuffer& stagingBuffer, VkDeviceSize size);
        const VkBuffer& getBuffer() const { return buffer; }
    };
}