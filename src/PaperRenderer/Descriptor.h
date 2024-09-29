#pragma once
#include "VulkanResources.h"

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

    struct AccelerationStructureDescriptorWrites
    {
        std::vector<class TLAS const*> accelerationStructures;
        uint32_t binding;
    };

    struct DescriptorWrites
    {
        std::vector<BuffersDescriptorWrites> bufferWrites = std::vector<BuffersDescriptorWrites>();
        std::vector<ImagesDescriptorWrites> imageWrites = std::vector<ImagesDescriptorWrites>();
        std::vector<BufferViewsDescriptorWrites> bufferViewWrites = std::vector<BufferViewsDescriptorWrites>();
        std::vector<AccelerationStructureDescriptorWrites> accelerationStructureWrites = std::vector<AccelerationStructureDescriptorWrites>();
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
        std::vector<VkDescriptorPool> descriptorPools;
        VkDescriptorPool* currentPool = NULL;

        class RenderEngine* rendererPtr;

        VkDescriptorPool allocateDescriptorPool();

    public:
        DescriptorAllocator(class RenderEngine* renderer);
        ~DescriptorAllocator();
        DescriptorAllocator(const DescriptorAllocator&) = delete;

        static void writeUniforms(class RenderEngine* renderer, VkDescriptorSet set, const DescriptorWrites& descriptorWritesInfo);
        static void bindSet(VkCommandBuffer cmdBuffer, const DescriptorBind& bindingInfo);

        VkDescriptorSet allocateDescriptorSet(VkDescriptorSetLayout setLayout);
        void refreshPools();
    };
}