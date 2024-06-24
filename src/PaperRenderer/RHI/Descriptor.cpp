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
        //4 different writes for 4 different types
        std::vector<VkWriteDescriptorSet> descriptorWrites;

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
                std::vector<VkAccelerationStructureKHR> tlasReferences;
                tlasReferences.reserve(write.accelerationStructures.size());
                for(AccelerationStructure const* accelerationStructure : write.accelerationStructures)
                {
                    tlasReferences.push_back(accelerationStructure->getTLAS());
                }

                //descriptor write
                VkWriteDescriptorSetAccelerationStructureKHR tlasWriteInfo = {};
                tlasWriteInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                tlasWriteInfo.pNext = NULL;
                tlasWriteInfo.accelerationStructureCount = tlasReferences.size();
                tlasWriteInfo.pAccelerationStructures = tlasReferences.data();

                VkWriteDescriptorSet writeInfo = {};
                writeInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writeInfo.pNext = &tlasWriteInfo; //VkWriteDescriptorSetAccelerationStructureKHR (above) is fed into pNext
                writeInfo.dstSet = set;
                writeInfo.dstBinding = write.binding;
                writeInfo.dstArrayElement = 0;
                writeInfo.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                writeInfo.descriptorCount = descriptorWritesInfo.accelerationStructureWrites.size();
                writeInfo.pBufferInfo = NULL;
                writeInfo.pImageInfo = NULL;
                writeInfo.pTexelBufferView = NULL;

                descriptorWrites.push_back(writeInfo);
            }
        }
        
        if(descriptorWrites.size()) //valid usage per spec, encouraging runtime safety
        {
            vkUpdateDescriptorSets(device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
        }
    }

    void DescriptorAllocator::bindSet(VkDevice device, VkCommandBuffer cmdBuffer, const DescriptorBind& bindingInfo)
    {
        vkCmdBindDescriptorSets(
            cmdBuffer,
            bindingInfo.bindingPoint,
            bindingInfo.layout,
            bindingInfo.descriptorScope,
            1,
            &bindingInfo.set,
            0,
            NULL);
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