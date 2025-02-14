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

    struct DescriptorBinding
    {
        VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        uint32_t descriptorSetIndex = 0;
        std::vector<uint32_t> dynamicOffsets = {};
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
        
        VkDescriptorSet getDescriptorSet(VkDescriptorSetLayout setLayout);
        void freeDescriptorSet(VkDescriptorSet set);
    };

    //----------RAII DESCRIPTOR WRAPPERS----------//

    struct SetBinding
    {
        const class ResourceDescriptor& set;
        DescriptorBinding binding;
    };

    class ResourceDescriptor
    {
    private:
        const VkDescriptorSetLayout& layout;
        const VkDescriptorSet set;

        class RenderEngine& renderer;

    public:
        ResourceDescriptor(class RenderEngine& renderer, const VkDescriptorSetLayout& layout);
        ~ResourceDescriptor();
        ResourceDescriptor(const ResourceDescriptor&) = delete;

        void updateDescriptorSet(const DescriptorWrites& writes) const;
        void bindDescriptorSet(VkCommandBuffer cmdBuffer, const DescriptorBinding& binding) const;

        const VkDescriptorSetLayout& getLayout() const { return layout; }
        const VkDescriptorSet& getDescriptorSet() const { return set; }
    };

    class DescriptorSetLayout
    {
    private:
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;

        class RenderEngine& renderer;
    public:
        DescriptorSetLayout(class RenderEngine& renderer, const std::vector<VkDescriptorSetLayoutBinding>& bindings);
        ~DescriptorSetLayout();
        DescriptorSetLayout(const DescriptorSetLayout&) = delete;

        const VkDescriptorSetLayout& getSetLayout() const { return setLayout; }
    };
}