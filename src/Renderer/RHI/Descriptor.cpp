#include "Descriptor.h"

namespace PaperRenderer
{
    DescriptorAllocator::DescriptorAllocator(Device *device)
        :devicePtr(device)
    {
        descriptorPools.resize(PaperMemory::Commands::getFrameCount());
        currentPools.resize(PaperMemory::Commands::getFrameCount());
    }
    
    DescriptorAllocator::~DescriptorAllocator()
    {   
        for(std::vector<VkDescriptorPool>& descriptorPoolSet : descriptorPools)
        {
            for(VkDescriptorPool& pool : descriptorPoolSet)
            {
                vkDestroyDescriptorPool(devicePtr->getDevice(), pool, nullptr);
            }
        }
    }

    VkDescriptorPool DescriptorAllocator::allocateDescriptorPool()
    {
        std::vector<VkDescriptorPoolSize> poolSizes;
        
        VkDescriptorPoolSize UBOpoolSize = {};
        UBOpoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        UBOpoolSize.descriptorCount = 256;
        poolSizes.push_back(UBOpoolSize);

        VkDescriptorPoolSize storagePoolSize = {};
        storagePoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        storagePoolSize.descriptorCount = 256;
        poolSizes.push_back(storagePoolSize);

        VkDescriptorPoolSize samplerPoolSize = {};
        samplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerPoolSize.descriptorCount = 256;
        poolSizes.push_back(samplerPoolSize);
        
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.pNext = NULL;
        poolInfo.flags = 0;
        poolInfo.maxSets = 256;
        poolInfo.poolSizeCount = poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();

        VkDescriptorPool returnPool;
        VkResult result = vkCreateDescriptorPool(devicePtr->getDevice(), &poolInfo, nullptr, &returnPool);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor pool");
        }
        
        return returnPool;
    }

    VkDescriptorSet DescriptorAllocator::allocateDescriptorSet(VkDescriptorSetLayout setLayout, uint32_t frameIndex)
    {
        VkDescriptorSet returnSet;

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.pNext = NULL;
        allocInfo.descriptorPool = *(currentPools.at(frameIndex));
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &setLayout;

        VkResult result = vkAllocateDescriptorSets(devicePtr->getDevice(), &allocInfo, &returnSet);

        if(result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
        {
            descriptorPools.at(frameIndex).push_back(allocateDescriptorPool());
            currentPools.at(frameIndex) = &(descriptorPools.at(frameIndex).back());

            result = vkAllocateDescriptorSets(devicePtr->getDevice(), &allocInfo, &returnSet);
            if(result != VK_SUCCESS) throw std::runtime_error("Descriptor allocator failed");
        }

        return returnSet;
    }

    void DescriptorAllocator::writeUniforms(VkDevice device, VkDescriptorSet set, const DescriptorWrites& descriptorWritesInfo)
    {
        //3 different writes for 3 different types
        std::vector<VkWriteDescriptorSet> descriptorWrites;

        if(descriptorWritesInfo.bufferWrites.infos.size())
        {
            VkWriteDescriptorSet writeInfo = {};
            writeInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeInfo.dstSet = set;
            writeInfo.dstBinding = descriptorWritesInfo.bufferWrites.binding;
            writeInfo.dstArrayElement = 0;
            writeInfo.descriptorType = descriptorWritesInfo.bufferWrites.type;
            writeInfo.descriptorCount = descriptorWritesInfo.bufferWrites.infos.size();
            writeInfo.pBufferInfo = descriptorWritesInfo.bufferWrites.infos.data();
            writeInfo.pImageInfo = NULL;
            writeInfo.pTexelBufferView = NULL;

            descriptorWrites.push_back(writeInfo);
        }

        if(descriptorWritesInfo.imageWrites.infos.size())
        {
            VkWriteDescriptorSet writeInfo = {};
            writeInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeInfo.dstSet = set;
            writeInfo.dstBinding = descriptorWritesInfo.imageWrites.binding;
            writeInfo.dstArrayElement = 0;
            writeInfo.descriptorType = descriptorWritesInfo.imageWrites.type;
            writeInfo.descriptorCount = descriptorWritesInfo.imageWrites.infos.size();
            writeInfo.pBufferInfo = NULL;
            writeInfo.pImageInfo = descriptorWritesInfo.imageWrites.infos.data();
            writeInfo.pTexelBufferView = NULL;

            descriptorWrites.push_back(writeInfo);
        }
        
        if(descriptorWritesInfo.bufferViewWrites.infos.size())
        {
            VkWriteDescriptorSet writeInfo = {};
            writeInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeInfo.dstSet = set;
            writeInfo.dstBinding = descriptorWritesInfo.bufferViewWrites.binding;
            writeInfo.dstArrayElement = 0;
            writeInfo.descriptorType = descriptorWritesInfo.bufferViewWrites.type;
            writeInfo.descriptorCount = descriptorWritesInfo.bufferViewWrites.infos.size();
            writeInfo.pBufferInfo = NULL;
            writeInfo.pImageInfo = NULL;
            writeInfo.pTexelBufferView = descriptorWritesInfo.bufferViewWrites.infos.data();

            descriptorWrites.push_back(writeInfo);
        }

        if(descriptorWrites.size()) //valid usage per spec, encouraging runtime safety
        {
            vkUpdateDescriptorSets(device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
        }
    }

    void DescriptorAllocator::refreshPools(uint32_t frameIndex)
    {
        for(VkDescriptorPool& pool : descriptorPools.at(frameIndex))
        {
            vkDestroyDescriptorPool(devicePtr->getDevice(), pool, nullptr);
        }
        descriptorPools.at(frameIndex).resize(0);
        descriptorPools.at(frameIndex).push_back(allocateDescriptorPool());
        currentPools.at(frameIndex) = &(descriptorPools.at(frameIndex).back());
    }
}