#pragma once
#include "VulkanResources.h"

#include <unordered_map>
#include <thread>
#include <mutex>

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

    struct DescriptorBind
    {
        VkPipelineBindPoint bindingPoint;
        VkPipelineLayout layout;
        uint32_t descriptorSetIndex;
        VkDescriptorSet set;
    };

    //----------DESCRIPTOR ALLOCATOR DECLARATIONS----------//

    class DescriptorAllocator
    {
    private:
        struct DescriptorPoolData
        {
            std::vector<VkDescriptorPool> descriptorPools;
            VkDescriptorPool currentPool = VK_NULL_HANDLE;
            std::recursive_mutex threadLock = {};
        };
        const uint32_t coreCount = std::thread::hardware_concurrency();
        std::vector<DescriptorPoolData> descriptorPoolDatas; //collection of pools that can be used async
        
        class RenderEngine& renderer;

        VkDescriptorPool allocateDescriptorPool() const;

    public:
        DescriptorAllocator(class RenderEngine& renderer);
        ~DescriptorAllocator();
        DescriptorAllocator(const DescriptorAllocator&) = delete;

        void writeUniforms(VkDescriptorSet set, const DescriptorWrites& descriptorWritesInfo) const;
        void bindSet(VkCommandBuffer cmdBuffer, const DescriptorBind& bindingInfo) const;

        VkDescriptorSet allocateDescriptorSet(VkDescriptorSetLayout setLayout);
        void refreshPools();
    };
}