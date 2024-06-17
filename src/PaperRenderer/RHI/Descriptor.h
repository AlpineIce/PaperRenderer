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
        std::vector<VkDescriptorImageInfo> infos;
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
        std::vector<BuffersDescriptorWrites> bufferWrites = std::vector<BuffersDescriptorWrites>();
        std::vector<ImagesDescriptorWrites> imageWrites = std::vector<ImagesDescriptorWrites>();
        std::vector<BufferViewsDescriptorWrites> bufferViewWrites = std::vector<BufferViewsDescriptorWrites>();
    };

    enum DescriptorScopes
    {
        RASTER_MATERIAL = 0,
        RASTER_MATERIAL_INSTANCE = 1,
        RASTER_OBJECT = 2
    };

    struct DescriptorBind
    {
        VkPipelineBindPoint bindingPoint;
        VkPipelineLayout layout;
        uint32_t descriptorScope;
        VkDescriptorSet set;
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
        static void bindSet(VkDevice device, VkCommandBuffer cmdBuffer, const DescriptorBind& bindingInfo);

        VkDescriptorSet allocateDescriptorSet(VkDescriptorSetLayout setLayout, uint32_t frameIndex);
        void refreshPools(uint32_t frameIndex);
    };
}