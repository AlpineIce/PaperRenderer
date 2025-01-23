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
        std::vector<VkDescriptorBufferInfo> infos = {};
        VkDescriptorType type;
        uint32_t binding;
    };

    struct ImagesDescriptorWrites
    {
        std::vector<VkDescriptorImageInfo> infos = {};
        VkDescriptorType type;
        uint32_t binding;
    };

    struct BufferViewsDescriptorWrites
    {
        std::vector<VkBufferView> infos = {};
        VkDescriptorType type;
        uint32_t binding;
    };

    struct AccelerationStructureDescriptorWrites
    {
        std::vector<class TLAS const*> accelerationStructures = {};
        uint32_t binding;
    };

    struct DescriptorWrites
    {
        std::vector<BuffersDescriptorWrites> bufferWrites = {};
        std::vector<ImagesDescriptorWrites> imageWrites = {};
        std::vector<BufferViewsDescriptorWrites> bufferViewWrites = {};
        std::vector<AccelerationStructureDescriptorWrites> accelerationStructureWrites = {};
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
            std::vector<VkDescriptorPool> descriptorPools = {};
            uint32_t currentPoolIndex = 0;
            std::recursive_mutex threadLock = {};
        };
        DescriptorPoolData descriptorPoolData = {}; //thread safe command pool wrapper
        std::unordered_map<VkDescriptorSet, uint32_t> allocatedSetPoolIndices = {};
        
        class RenderEngine& renderer;

        VkDescriptorPool allocateDescriptorPool() const;

    public:
        DescriptorAllocator(class RenderEngine& renderer);
        ~DescriptorAllocator();
        DescriptorAllocator(const DescriptorAllocator&) = delete;

        void updateDescriptorSet(VkDescriptorSet set, const DescriptorWrites& descriptorWritesInfo) const;
        
        VkDescriptorSetLayout createDescriptorSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings) const;
        VkDescriptorSet getDescriptorSet(VkDescriptorSetLayout setLayout);
        void freeDescriptorSet(VkDescriptorSet set);
    };

    //----------RAII DESCRIPTOR WRAPPER----------//

    class DescriptorGroup
    {
    private:
        std::unordered_map<uint32_t, VkDescriptorSet> descriptorSets = {};

        class RenderEngine& renderer;

    public:
        DescriptorGroup(class RenderEngine& renderer, const std::unordered_map<uint32_t, VkDescriptorSetLayout>& setLayouts);
        ~DescriptorGroup();
        DescriptorGroup(const DescriptorGroup&) = delete;

        void updateDescriptorSets(const std::unordered_map<uint32_t, DescriptorWrites>& descriptorWrites) const;
        void bindSets(VkCommandBuffer cmdBuffer, VkPipelineBindPoint bindingPoint, VkPipelineLayout layout) const;
    };
}