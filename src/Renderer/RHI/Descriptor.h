#pragma once
#include "Device.h"
#include "Memory/VulkanResources.h"

#include <unordered_map>

namespace PaperRenderer
{
    //----------DESCRIPTOR WRITE STRUCTS----------//

    struct BuffersDescriptorWrites
    {
        std::vector<VkDescriptorBufferInfo> infos;
        VkDescriptorType type;
        uint32_t binding;
    };

    struct ImagesDescriptorWrites
    {
        std::vector<VkDescriptorImageInfo> infos; //VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        VkDescriptorType type;
        uint32_t binding;
    };

    struct BufferViewsDescriptorWrites
    {
        std::vector<VkBufferView> infos;
        VkDescriptorType type;
        uint32_t binding;
    };

    struct DescriptorWrites
    {
        VkDescriptorSetLayout setLayout;
        BuffersDescriptorWrites bufferWrites;
        ImagesDescriptorWrites imageWrites;
        BufferViewsDescriptorWrites bufferViewWrites;
    };

    //----------DESCRIPTOR ALLOCATOR DECLARATIONS----------//

    class DescriptorAllocator
    {
    private:
        std::vector<std::vector<VkDescriptorPool>> descriptorPools;
        std::vector<VkDescriptorPool*> currentPools;

        Device* devicePtr;

        VkDescriptorPool allocateDescriptorPool();

    public:
        DescriptorAllocator(Device* device);
        ~DescriptorAllocator();

        static void writeUniforms(VkDevice device, VkDescriptorSet set, const DescriptorWrites& descriptorWritesInfo);

        VkDescriptorSet allocateDescriptorSet(VkDescriptorSetLayout setLayout, uint32_t frameIndex);
        void refreshPools(uint32_t frameIndex);
    };
}