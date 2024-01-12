#pragma once
#include "vulkan/vulkan.hpp"
#include "Buffer.h"

#include <unordered_map>
#include <memory>

namespace Renderer
{
    //----------DYNAMIC UBO STRUCTS----------//

    const uint32_t TEXTURE_ARRAY_SIZE = 8;
    const uint32_t MAX_POINT_LIGHTS = 8;
    
    class DescriptorAllocator
    {
    private:
        std::vector<std::vector<VkDescriptorPool>> descriptorPools;
        std::vector<VkDescriptorPool*> currentPools;
        std::shared_ptr<Texture> defaultTexture;

        Device* devicePtr;
        Commands* commandsPtr;

        VkDescriptorPool allocateDescriptorPool();

    public:
        DescriptorAllocator(Device* device, Commands* commands);
        ~DescriptorAllocator();

        VkDescriptorSet allocateDescriptorSet(VkDescriptorSetLayout setLayout, uint32_t frameIndex);
        void writeUniform(const VkBuffer& buffer, uint32_t size, uint32_t offset, uint32_t binding, VkDescriptorType type, const VkDescriptorSet& set);
        void writeImageArray(std::vector<Texture const*> textures, uint32_t binding, const VkDescriptorSet& set);
        void refreshPools(uint32_t frameIndex);
    };
}