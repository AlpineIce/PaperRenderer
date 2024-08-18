#include "Descriptor.h"
#include "Device.h"
#include "AccelerationStructure.h"

namespace PaperRenderer
{
    DescriptorAllocator::DescriptorAllocator(Device *device)
        :devicePtr(device)
    {
    }
    
    DescriptorAllocator::~DescriptorAllocator()
    {   
        for(VkDescriptorPool& pool : descriptorPools)
        {
            vkDestroyDescriptorPool(devicePtr->getDevice(), pool, nullptr);
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

    VkDescriptorSet DescriptorAllocator::allocateDescriptorSet(VkDescriptorSetLayout setLayout)
    {
        VkDescriptorSet returnSet;

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.pNext = NULL;
        allocInfo.descriptorPool = *currentPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &setLayout;

        VkResult result = vkAllocateDescriptorSets(devicePtr->getDevice(), &allocInfo, &returnSet);

        if(result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
        {
            descriptorPools.push_back(allocateDescriptorPool());
            currentPool = &(descriptorPools.back());

            result = vkAllocateDescriptorSets(devicePtr->getDevice(), &allocInfo, &returnSet);
            if(result != VK_SUCCESS) throw std::runtime_error("Descriptor allocator failed");
        }

        return returnSet;
    }

    void DescriptorAllocator::writeUniforms(VkDevice device, VkDescriptorSet set, const DescriptorWrites& descriptorWritesInfo)
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

        tlasReferences.resize(descriptorWritesInfo.accelerationStructureWrites.size());
        tlasWriteInfos.reserve(descriptorWritesInfo.accelerationStructureWrites.size());
        uint32_t asIndex = 0;
        for(const AccelerationStructureDescriptorWrites& write : descriptorWritesInfo.accelerationStructureWrites)
        {
            if(write.accelerationStructures.size())
            {
                //get TLAS references
                tlasReferences.at(asIndex).reserve(write.accelerationStructures.size());
                for(AccelerationStructure const* accelerationStructure : write.accelerationStructures)
                {
                    tlasReferences.at(asIndex).push_back(accelerationStructure->getTLAS());
                }

                //descriptor write
                tlasWriteInfos.push_back({
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
                    .pNext = NULL,
                    .accelerationStructureCount = (uint32_t)tlasReferences.at(asIndex).size(),
                    .pAccelerationStructures = tlasReferences.at(asIndex).data()
                });

                VkWriteDescriptorSet writeInfo = {};
                writeInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writeInfo.pNext = &tlasWriteInfos.at(asIndex);
                writeInfo.dstSet = set;
                writeInfo.dstBinding = write.binding;
                writeInfo.dstArrayElement = 0;
                writeInfo.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                writeInfo.descriptorCount = descriptorWritesInfo.accelerationStructureWrites.size();
                writeInfo.pBufferInfo = NULL;
                writeInfo.pImageInfo = NULL;
                writeInfo.pTexelBufferView = NULL;

                descriptorWrites.push_back(writeInfo);

                asIndex++;
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

    void DescriptorAllocator::refreshPools()
    {
        for(VkDescriptorPool& pool : descriptorPools)
        {
            vkDestroyDescriptorPool(devicePtr->getDevice(), pool, nullptr);
        }
        descriptorPools.resize(0);
        descriptorPools.push_back(allocateDescriptorPool());
        currentPool = &(descriptorPools.back());
    }
}