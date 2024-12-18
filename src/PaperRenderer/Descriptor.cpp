#include "Descriptor.h"
#include "PaperRenderer.h"

#include <functional>
#include <future>
#include <array>

namespace PaperRenderer
{
    DescriptorAllocator::DescriptorAllocator(RenderEngine& renderer)
        :descriptorPoolDatas(coreCount),
        renderer(renderer)
    {
        //initialize one descriptor pool
        for(DescriptorPoolData& poolData : descriptorPoolDatas)
        {
            poolData.descriptorPools = { allocateDescriptorPool() };
        }

        //log constructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "DescriptorAllocator constructor finished"
        });
    }
    
    DescriptorAllocator::~DescriptorAllocator()
    {   
        for(DescriptorPoolData& poolsData : descriptorPoolDatas)
        {
            std::lock_guard<std::recursive_mutex> guard(poolsData.threadLock);
            for(VkDescriptorPool& pool : poolsData.descriptorPools)
            {
                vkDestroyDescriptorPool(renderer.getDevice().getDevice(), pool, nullptr);
            }
        }

        //log destructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "DescriptorAllocator destructor initialized"
        });
    }

    VkDescriptorPool DescriptorAllocator::allocateDescriptorPool() const
    {
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
            {
                .type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                .descriptorCount = descriptorCount
            }
        }};
        
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.pNext = NULL;
        poolInfo.flags = 0;
        poolInfo.maxSets = descriptorCount;
        poolInfo.poolSizeCount = poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();

        VkDescriptorPool returnPool;
        VkResult result = vkCreateDescriptorPool(renderer.getDevice().getDevice(), &poolInfo, nullptr, &returnPool);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor pool");
        }
        
        return returnPool;
    }

    VkDescriptorSet DescriptorAllocator::allocateDescriptorSet(VkDescriptorSetLayout setLayout)
    {
        //find a descriptor pool to lock
        bool threadLocked = false;
        DescriptorPoolData* lockedPool = NULL;
        while(!threadLocked)
        {
            for(DescriptorPoolData& pool : descriptorPoolDatas)
            {
                if(pool.threadLock.try_lock())
                {
                    threadLocked = true;
                    lockedPool = &pool;

                    break;
                }
            }
        }

        VkDescriptorSet returnSet;

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.pNext = NULL;
        allocInfo.descriptorPool = lockedPool->descriptorPools[lockedPool->currentPoolIndex];
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &setLayout;

        VkResult result = vkAllocateDescriptorSets(renderer.getDevice().getDevice(), &allocInfo, &returnSet);

        if(result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
        {
            //increment pool index
            lockedPool->currentPoolIndex++;

            //verify another pool exists in vector
            if(lockedPool->descriptorPools.size() <= lockedPool->currentPoolIndex)
            {
                lockedPool->descriptorPools.push_back(allocateDescriptorPool());
            }

            //recursive retry
            return allocateDescriptorSet(setLayout);
        }

        //unlock mutex
        lockedPool->threadLock.unlock();

        return returnSet;
    }

    void DescriptorAllocator::writeUniforms(VkDescriptorSet set, const DescriptorWrites& descriptorWritesInfo) const
    {
        //4 different writes for 4 different types
        std::vector<VkWriteDescriptorSet> descriptorWrites;
        std::vector<std::vector<VkAccelerationStructureKHR>> tlasReferences;
        std::vector<VkWriteDescriptorSetAccelerationStructureKHR> tlasWriteInfos;

        for(const BuffersDescriptorWrites& write : descriptorWritesInfo.bufferWrites)
        {
            if(write.infos.size())
            {
                VkWriteDescriptorSet writeInfo = {};
                writeInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writeInfo.dstSet = set;
                writeInfo.dstBinding = write.binding;
                writeInfo.dstArrayElement = 0;
                writeInfo.descriptorType = write.type;
                writeInfo.descriptorCount = write.infos.size();
                writeInfo.pBufferInfo = write.infos.data();
                writeInfo.pImageInfo = NULL;
                writeInfo.pTexelBufferView = NULL;

                descriptorWrites.push_back(writeInfo);
            }
        }

        for(const ImagesDescriptorWrites& write : descriptorWritesInfo.imageWrites)
        {
            if(write.infos.size())
            {
                VkWriteDescriptorSet writeInfo = {};
                writeInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writeInfo.dstSet = set;
                writeInfo.dstBinding = write.binding;
                writeInfo.dstArrayElement = 0;
                writeInfo.descriptorType = write.type;
                writeInfo.descriptorCount = write.infos.size();
                writeInfo.pBufferInfo = NULL;
                writeInfo.pImageInfo = write.infos.data();
                writeInfo.pTexelBufferView = NULL;

                descriptorWrites.push_back(writeInfo);
            }
        }
        
        for(const BufferViewsDescriptorWrites& write : descriptorWritesInfo.bufferViewWrites)
        {
            if(write.infos.size())
            {
                VkWriteDescriptorSet writeInfo = {};
                writeInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writeInfo.dstSet = set;
                writeInfo.dstBinding = write.binding;
                writeInfo.dstArrayElement = 0;
                writeInfo.descriptorType = write.type;
                writeInfo.descriptorCount = write.infos.size();
                writeInfo.pBufferInfo = NULL;
                writeInfo.pImageInfo = NULL;
                writeInfo.pTexelBufferView = write.infos.data();

                descriptorWrites.push_back(writeInfo);
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

                    VkWriteDescriptorSet writeInfo = {};
                    writeInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writeInfo.pNext = &(*tlasWriteInfos.rbegin());
                    writeInfo.dstSet = set;
                    writeInfo.dstBinding = write.binding;
                    writeInfo.dstArrayElement = 0;
                    writeInfo.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                    writeInfo.descriptorCount = tlasReferences.size();
                    writeInfo.pBufferInfo = NULL;
                    writeInfo.pImageInfo = NULL;
                    writeInfo.pTexelBufferView = NULL;

                    descriptorWrites.push_back(writeInfo);
                }
            }
        }
        
        if(descriptorWrites.size()) //valid usage per spec, encouraging runtime safety
        {
            vkUpdateDescriptorSets(renderer.getDevice().getDevice(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
        }
    }

    void DescriptorAllocator::bindSet(VkCommandBuffer cmdBuffer, const DescriptorBind& bindingInfo) const
    {
        vkCmdBindDescriptorSets(
            cmdBuffer,
            bindingInfo.bindingPoint,
            bindingInfo.layout,
            bindingInfo.descriptorSetIndex,
            1,
            &bindingInfo.set,
            0,
            NULL
        );
    }

    void DescriptorAllocator::refreshPools()
    {
        //Timer
        Timer timer(renderer, "Reset Descriptor Pools");

        //reset pools
        for(DescriptorPoolData& poolData : descriptorPoolDatas)
        {
            //wait for any non-submitted command buffers (potentially a deadlock problem)
            std::lock_guard<std::recursive_mutex> guard(poolData.threadLock);

            //reset pool and current pool index
            for(VkDescriptorPool pool : poolData.descriptorPools)
            {
                vkResetDescriptorPool(renderer.getDevice().getDevice(), pool, 0);
            }
            poolData.currentPoolIndex = 0;
        }
    }
}