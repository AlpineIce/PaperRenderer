#pragma once
#include "vulkan/vulkan.hpp"
#include "Buffer.h"

#include <unordered_map>
#include <memory>

namespace Renderer
{
    const uint32_t TEXTURE_ARRAY_SIZE = 8;
    
    class Descriptors
    {
    private:
        
        VkDescriptorPool descriptorPool;
        VkDescriptorSetLayout descriptorLayout;
        VkDescriptorSet descriptorSet;
        std::shared_ptr<UniformBuffer> UBO;
        std::vector<uint32_t> offsets;

        Device* devicePtr;
        Commands* commandsPtr;
        std::shared_ptr<Texture> defaultTexture;

        uint32_t getOffsetOf(uint32_t bytesSize);

        void createLayout();
        void createDescriptorPool();
        void allocateDescriptors();
        void writeUniform();

    public:
        Descriptors(Device* device, Commands* commands);
        ~Descriptors();

        void updateUniforms(UniformBufferObject* uploadData);
        void updateTextures(std::vector<Texture const*> textures);

        VkDescriptorSetLayout const* getSetLayoutPtr() const { return &descriptorLayout; }
        VkDescriptorSet const* getDescriptorSetPtr() const { return &descriptorSet; }
        std::vector<uint32_t> getOffsets() const { return offsets; }

    };
}