#include "stb_image.h"
#include "Descriptor.h"

namespace Renderer
{
    DescriptorAllocator::DescriptorAllocator(Device *device, CmdBufferAllocator *commands)
        :devicePtr(device),
        commandsPtr(commands)
    {
        unsigned char pixel[4] = {(uint8_t)0, (uint8_t)0, (uint8_t)0, (uint8_t)255};
        Image imageData;
        imageData.channels = 4;
        imageData.width = 1;
        imageData.height = 1;
        imageData.size = 4;
        imageData.data = &pixel;

        defaultTexture = std::make_shared<Texture>(devicePtr, commandsPtr, &imageData);

        descriptorPools.resize(CmdBufferAllocator::getFrameCount());
        currentPools.resize(CmdBufferAllocator::getFrameCount());
        for(uint32_t i = 0; i < CmdBufferAllocator::getFrameCount(); i++)
        {
            descriptorPools.at(i).push_back(allocateDescriptorPool());
            currentPools.at(i) = &(descriptorPools.at(i).back());
        }
        
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

    void DescriptorAllocator::writeUniform(
        const VkBuffer& buffer,
        uint32_t size,
        uint32_t offset,
        uint32_t binding,
        VkDescriptorType type,
        const VkDescriptorSet& set)
    {
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = buffer;
        bufferInfo.offset = offset;
        bufferInfo.range = size;

        //write set
        VkWriteDescriptorSet uniformWriteInfo = {};
        uniformWriteInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uniformWriteInfo.dstSet = set;
        uniformWriteInfo.dstBinding = binding;
        uniformWriteInfo.dstArrayElement = 0;
        uniformWriteInfo.descriptorType = type;
        uniformWriteInfo.descriptorCount = 1;
        uniformWriteInfo.pBufferInfo = &bufferInfo;
        uniformWriteInfo.pImageInfo = NULL;
        uniformWriteInfo.pTexelBufferView = NULL;

        vkUpdateDescriptorSets(devicePtr->getDevice(), 1, &uniformWriteInfo, 0, nullptr);
    }

    void DescriptorAllocator::writeImageArray(std::vector<Texture const*> textures, uint32_t binding, const VkDescriptorSet& set)
    {
        textures.resize(8);
        std::vector<VkDescriptorImageInfo> imageInfos(TEXTURE_ARRAY_SIZE);
        for(int i = 0; i < TEXTURE_ARRAY_SIZE; i++)
        {
            if(textures.at(i))
            {
                imageInfos.at(i).imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfos.at(i).imageView = textures.at(i)->getTextureView();
                imageInfos.at(i).sampler = textures.at(i)->getTextureSampler();
            }
            else
            {
                //default texture (pink)
                imageInfos.at(i).imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfos.at(i).imageView = defaultTexture->getTextureView();
                imageInfos.at(i).sampler = defaultTexture->getTextureSampler();
            }
        }

        VkWriteDescriptorSet imageWriteInfo = {};
        imageWriteInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        imageWriteInfo.dstSet = set;
        imageWriteInfo.dstBinding = binding;
        imageWriteInfo.dstArrayElement = 0;
        imageWriteInfo.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imageWriteInfo.descriptorCount = TEXTURE_ARRAY_SIZE;
        imageWriteInfo.pBufferInfo = NULL;
        imageWriteInfo.pImageInfo = imageInfos.data();
        imageWriteInfo.pTexelBufferView = NULL;

        vkUpdateDescriptorSets(devicePtr->getDevice(), 1, &imageWriteInfo, 0, nullptr);
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