#include "Descriptor.h"
#include "PaperRenderer.h"

#include <functional>
#include <future>
#include <array>

namespace PaperRenderer
{
    DescriptorAllocator::DescriptorAllocator(RenderEngine& renderer)
        :renderer(renderer)
    {
        //initialize one descriptor pool
        descriptorPoolData.descriptorPools = { allocateDescriptorPool() };

        //log constructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "DescriptorAllocator constructor finished"
        });
    }
    
    DescriptorAllocator::~DescriptorAllocator()
    {   
        std::lock_guard<std::recursive_mutex> guard(descriptorPoolData.threadLock);
        for(VkDescriptorPool& pool : descriptorPoolData.descriptorPools)
        {
            vkDestroyDescriptorPool(renderer.getDevice().getDevice(), pool, nullptr);
        }

        //log destructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "DescriptorAllocator destructor initialized"
        });
    }

    VkDescriptorPool DescriptorAllocator::allocateDescriptorPool() const
    {
        //log creation
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "Allocating new descriptor pool"
        });

        //funny enough NVIDIA doesnt care about the following pool sizes... NVIDIA gpus work completely fine without them
        const uint32_t descriptorCount = 1024;
        const std::array<VkDescriptorPoolSize, 12> poolSizes{{
            {
                .type = VK_DESCRIPTOR_TYPE_SAMPLER,
                .descriptorCount = descriptorCount
            },
            {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = descriptorCount
            },
            {
                .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .descriptorCount = descriptorCount
            },
            {
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = descriptorCount
            },
            {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
                .descriptorCount = descriptorCount
            },
            {
                .type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                .descriptorCount = descriptorCount
            },
            {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = descriptorCount
            },
            {
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = descriptorCount
            },
            {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .descriptorCount = descriptorCount
            },
            {
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                .descriptorCount = descriptorCount
            },
            {
                .type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                .descriptorCount = descriptorCount
            },
            { //IMPORTANT THAT THIS IS LAST
                .type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                .descriptorCount = descriptorCount
            }
        }};
        
        const VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = NULL,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = descriptorCount,
            .poolSizeCount = (uint32_t)(renderer.getDevice().getRTSupport() ? poolSizes.size() : poolSizes.size() - 1),
            .pPoolSizes = poolSizes.data()
        };

        VkDescriptorPool returnPool;
        VkResult result = vkCreateDescriptorPool(renderer.getDevice().getDevice(), &poolInfo, nullptr, &returnPool);
        if(result != VK_SUCCESS)
        {
            renderer.getLogger().recordLog({
                .type = ERROR,
                .text = "Failed to create descriptor pool"
            });
        }
        
        return returnPool;
    }

    VkDescriptorSet DescriptorAllocator::getDescriptorSet(VkDescriptorSetLayout setLayout)
    {
        //thread lock
        std::lock_guard guard(descriptorPoolData.threadLock);

        //get set
        VkDescriptorSet returnSet;

        const VkDescriptorSetAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = NULL,
            .descriptorPool = descriptorPoolData.descriptorPools[descriptorPoolData.currentPoolIndex],
            .descriptorSetCount = 1,
            .pSetLayouts = &setLayout
        };

        VkResult result = vkAllocateDescriptorSets(renderer.getDevice().getDevice(), &allocInfo, &returnSet);

        if(result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
        {
            //increment pool index
            descriptorPoolData.currentPoolIndex++;

            //verify another pool exists in vector
            if(descriptorPoolData.descriptorPools.size() <= descriptorPoolData.currentPoolIndex)
            {
                descriptorPoolData.descriptorPools.push_back(allocateDescriptorPool());
            }

            //recursive retry
            return getDescriptorSet(setLayout);
        }
        else
        {
            allocatedSetPoolIndices[returnSet] = descriptorPoolData.currentPoolIndex;
        }

        //unlock mutex
        descriptorPoolData.threadLock.unlock();

        return returnSet;
    }

    VkDescriptorSetLayout DescriptorAllocator::createDescriptorSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings) const
    {
        const VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .bindingCount = (uint32_t)bindings.size(),
            .pBindings = bindings.data()
        };
        
        VkDescriptorSetLayout setLayout;
        VkResult result = vkCreateDescriptorSetLayout(renderer.getDevice().getDevice(), &descriptorLayoutInfo, nullptr, &setLayout);
        if(result != VK_SUCCESS)
        {
            renderer.getLogger().recordLog({
                .type = ERROR,
                .text = "Failed to create descriptor set layout"
            });
        }

        return setLayout;
    }

    void DescriptorAllocator::freeDescriptorSet(VkDescriptorSet set)
    {
        //free
        vkFreeDescriptorSets(renderer.getDevice().getDevice(), descriptorPoolData.descriptorPools[allocatedSetPoolIndices[set]], 1, &set);

        //move current pool index to the allocated set index
        descriptorPoolData.currentPoolIndex = std::min(descriptorPoolData.currentPoolIndex, allocatedSetPoolIndices[set]);

        //erase "reference"
        allocatedSetPoolIndices.erase(set);
    }

    void DescriptorAllocator::updateDescriptorSet(VkDescriptorSet set, const DescriptorWrites& descriptorWritesInfo) const
    {
        std::vector<VkWriteDescriptorSet> descriptorWrites;
        descriptorWrites.reserve(descriptorWritesInfo.bufferWrites.size() + descriptorWritesInfo.bufferViewWrites.size() + descriptorWritesInfo.imageWrites.size());
        std::vector<std::vector<VkAccelerationStructureKHR>> tlasReferences;
        tlasReferences.reserve(descriptorWritesInfo.accelerationStructureWrites.size());
        std::vector<VkWriteDescriptorSetAccelerationStructureKHR> tlasWriteInfos;
        tlasWriteInfos.reserve(descriptorWritesInfo.accelerationStructureWrites.size());

        for(const BuffersDescriptorWrites& write : descriptorWritesInfo.bufferWrites)
        {
            if(write.infos.size())
            {
                descriptorWrites.push_back({
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = set,
                    .dstBinding = write.binding,
                    .dstArrayElement = 0,
                    .descriptorCount = (uint32_t)write.infos.size(),
                    .descriptorType = write.type,
                    .pImageInfo = NULL,
                    .pBufferInfo = write.infos.data(),
                    .pTexelBufferView = NULL
                });
            }
        }

        for(const ImagesDescriptorWrites& write : descriptorWritesInfo.imageWrites)
        {
            if(write.infos.size())
            {
                descriptorWrites.push_back({
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = set,
                    .dstBinding = write.binding,
                    .dstArrayElement = 0,
                    .descriptorCount = (uint32_t)write.infos.size(),
                    .descriptorType = write.type,
                    .pImageInfo = write.infos.data(),
                    .pBufferInfo = NULL,
                    .pTexelBufferView = NULL
                });
            }
        }
        
        for(const BufferViewsDescriptorWrites& write : descriptorWritesInfo.bufferViewWrites)
        {
            if(write.infos.size())
            {
                descriptorWrites.push_back({
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = set,
                    .dstBinding = write.binding,
                    .dstArrayElement = 0,
                    .descriptorCount = (uint32_t)write.infos.size(),
                    .descriptorType = write.type,
                    .pImageInfo = NULL,
                    .pBufferInfo = NULL,
                    .pTexelBufferView = write.infos.data()
                });
            }
        }

        for(const AccelerationStructureDescriptorWrites& write : descriptorWritesInfo.accelerationStructureWrites)
        {
            if(write.accelerationStructures.size())
            {
                //get TLAS references
                tlasReferences.emplace_back();
                tlasReferences.rbegin()->reserve(write.accelerationStructures.size());
                for(TLAS const* accelerationStructure : write.accelerationStructures)
                {
                    tlasReferences.rbegin()->push_back(accelerationStructure->getAccelerationStructure());
                }

                //descriptor write
                if(tlasReferences.rbegin()->size())
                {
                    tlasWriteInfos.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
                        .pNext = NULL,
                        .accelerationStructureCount = (uint32_t)tlasReferences.rbegin()->size(),
                        .pAccelerationStructures = tlasReferences.rbegin()->data()
                    });

                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .pNext = &(*tlasWriteInfos.rbegin()),
                        .dstSet = set,
                        .dstBinding = write.binding,
                        .dstArrayElement = 0,
                        .descriptorCount = (uint32_t)tlasReferences.size(),
                        .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                        .pImageInfo = NULL,
                        .pBufferInfo = NULL,
                        .pTexelBufferView = NULL
                    });
                }
            }
        }
        
        if(descriptorWrites.size())
        {
            vkUpdateDescriptorSets(renderer.getDevice().getDevice(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
        }
    }

    //----------DESCRIPTOR WRAPPER DEFINITIONS----------//

    ResourceDescriptor::ResourceDescriptor(RenderEngine& renderer, const VkDescriptorSetLayout& layout)
        :layout(layout),
        set(renderer.getDescriptorAllocator().getDescriptorSet(layout)),
        renderer(renderer)
    {
    }

    ResourceDescriptor::~ResourceDescriptor()
    {
        renderer.getDescriptorAllocator().freeDescriptorSet(set);
    }

    void ResourceDescriptor::updateDescriptorSet(const DescriptorWrites& writes) const
    {
        renderer.getDescriptorAllocator().updateDescriptorSet(set, writes);
    }

    void ResourceDescriptor::bindDescriptorSet(VkCommandBuffer cmdBuffer, const DescriptorBinding& binding) const
    {
        vkCmdBindDescriptorSets(
            cmdBuffer,
            binding.bindPoint,
            binding.pipelineLayout,
            binding.descriptorSetIndex,
            1,
            &set,
            binding.dynamicOffsets.size(),
            binding.dynamicOffsets.data()
        );
    }
}