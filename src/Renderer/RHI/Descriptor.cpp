#include "stb_image.h"
#include "Descriptor.h"

namespace Renderer
{
    Descriptors::Descriptors(Device *device, Commands *commands)
        :devicePtr(device),
        commandsPtr(commands)
    {
        //make uniform buffer object
        UBO = std::make_shared<UniformBuffer>(devicePtr, commandsPtr, getOffsetOf(sizeof(UniformBufferObject)));

        uint8_t imageData[4] = {255, 0, 255, 255};
        Image defaultImage = {
            .data = imageData,
            .size = 4, //might be the wrong size
            .width = 1,
            .height = 1,
            .channels = 4
        };
        defaultTexture = std::make_shared<Texture>(devicePtr, commandsPtr, &defaultImage);

        createLayout();
        createDescriptorPool();
        allocateDescriptors();
    }
    
    Descriptors::~Descriptors()
    {
        vkDestroyDescriptorPool(devicePtr->getDevice(), descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(devicePtr->getDevice(), descriptorLayout, nullptr);
    }

    void Descriptors::createLayout()
    {
        std::vector<VkDescriptorSetLayoutBinding> descriptors;

        //dynamic UBO
        VkDescriptorSetLayoutBinding uniformDescriptor = {};
        uniformDescriptor.binding = 0;
        uniformDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        uniformDescriptor.descriptorCount = 1;
        uniformDescriptor.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uniformDescriptor.pImmutableSamplers = NULL;
        descriptors.push_back(uniformDescriptor);

        //texture array
        VkDescriptorSetLayoutBinding textureDescriptor = {};
        textureDescriptor.binding = 1;
        textureDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureDescriptor.descriptorCount = TEXTURE_ARRAY_SIZE;
        textureDescriptor.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        textureDescriptor.pImmutableSamplers = NULL;
        descriptors.push_back(textureDescriptor);

        //descriptor info
        VkDescriptorSetLayoutCreateInfo descriptorInfo = {};
        descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorInfo.pNext = NULL;
        descriptorInfo.flags = 0;
        descriptorInfo.bindingCount = descriptors.size();
        descriptorInfo.pBindings = descriptors.data();

        VkResult result = vkCreateDescriptorSetLayout(devicePtr->getDevice(), &descriptorInfo, nullptr, &descriptorLayout);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor set layout");
        }
    }

    uint32_t Descriptors::getOffsetOf(uint32_t bytesSize)
    {
        //most of this is from https://github.com/SaschaWillems/Vulkan/blob/master/examples/dynamicuniformbuffer/README.md
        uint32_t minUboAlignment = devicePtr->getGPUProperties().limits.minUniformBufferOffsetAlignment;
        uint32_t maxUboAlignment = devicePtr->getGPUProperties().limits.maxUniformBufferRange; //debug line
        if (minUboAlignment > 0)
        {
		    return (bytesSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
	    }
        else
        {
            throw std::runtime_error("uh oh GPU min uniform buffer offset alignment isn't greater than 0");
            return 0;
        }
    }

    void Descriptors::createDescriptorPool()
    {
        //UBO descriptor Pool
        std::vector<VkDescriptorPoolSize> poolSizes;
        
        VkDescriptorPoolSize UBOpoolSize = {};
        UBOpoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        UBOpoolSize.descriptorCount = Commands::getFrameCount();
        poolSizes.push_back(UBOpoolSize);

        VkDescriptorPoolSize samplerPoolSize = {};
        samplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerPoolSize.descriptorCount = Commands::getFrameCount();
        poolSizes.push_back(samplerPoolSize);
        
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.pNext = NULL;
        poolInfo.flags = 0;
        poolInfo.maxSets = Commands::getFrameCount();
        poolInfo.poolSizeCount = poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();

        VkResult result = vkCreateDescriptorPool(devicePtr->getDevice(), &poolInfo, nullptr, &descriptorPool);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor pool");
        }
    }

    void Descriptors::allocateDescriptors()
    {
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.pNext = NULL;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorLayout;

        VkResult result = vkAllocateDescriptorSets(devicePtr->getDevice(), &allocInfo, &descriptorSet);

        writeUniform();
    }

    void Descriptors::updateTextures(std::vector<Texture const*> textures)
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
                imageInfos.at(i).imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfos.at(i).imageView = defaultTexture->getTextureView();
                imageInfos.at(i).sampler = defaultTexture->getTextureSampler();
            }
        }

        VkWriteDescriptorSet imageWriteInfo = {};
        imageWriteInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        imageWriteInfo.dstSet = descriptorSet;
        imageWriteInfo.dstBinding = 1;
        imageWriteInfo.dstArrayElement = 0;
        imageWriteInfo.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imageWriteInfo.descriptorCount = TEXTURE_ARRAY_SIZE;
        imageWriteInfo.pBufferInfo = NULL;
        imageWriteInfo.pImageInfo = imageInfos.data();
        imageWriteInfo.pTexelBufferView = NULL;

        vkUpdateDescriptorSets(devicePtr->getDevice(), 1, &imageWriteInfo, 0, nullptr);
    }

    void Descriptors::writeUniform()
    {
        //view and projection matrix (per frame)
        VkDescriptorBufferInfo UBOinfo = {};
        UBOinfo.buffer = UBO->getBuffer();
        UBOinfo.offset = getOffsetOf(offsetof(UniformBufferObject, view));
        UBOinfo.range = getOffsetOf(offsetof(UniformBufferObject, testVec1));
        offsets.push_back(0);

        //write set
        VkWriteDescriptorSet uniformWriteInfo = {};
        uniformWriteInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uniformWriteInfo.dstSet = descriptorSet;
        uniformWriteInfo.dstBinding = 0;
        uniformWriteInfo.dstArrayElement = 0;
        uniformWriteInfo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        uniformWriteInfo.descriptorCount = 1;
        uniformWriteInfo.pBufferInfo = &UBOinfo;
        uniformWriteInfo.pImageInfo = NULL;
        uniformWriteInfo.pTexelBufferView = NULL;

        vkUpdateDescriptorSets(devicePtr->getDevice(), 1, &uniformWriteInfo, 0, nullptr);
    }

    void Descriptors::updateUniforms(UniformBufferObject *uploadData)
    {
        UBO->updateUniformBuffer(uploadData);
    }
}