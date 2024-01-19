#include "Material.h"

namespace Renderer
{
    Material::Material(Device* device, CmdBufferAllocator* commands, Pipeline* pipeline, std::string matName, std::vector<Texture const*> textures, std::vector<glm::vec4> colors)
        :devicePtr(device),
        pipelinePtr(pipeline),
        matName(matName),
        textures(textures),
        colors(colors)

    {
        switch(pipelinePtr->getPipelineType())
        {
            case PipelineType::PBR:
                materialUBO = std::make_shared<UniformBuffer>(devicePtr, commandsPtr, (uint32_t)sizeof(PBRpipelineUniforms));
                textures.resize(TEXTURE_ARRAY_SIZE);
                break;

            case PipelineType::TexturelessPBR:
                materialUBO = std::make_shared<UniformBuffer>(devicePtr, commandsPtr, (uint32_t)sizeof(TexturelessPBRpipelineUniforms));
                colors.resize(TEXTURE_ARRAY_SIZE);
                break;
        }
    }

    Material::~Material()
    {
    }

    void Material::updateUniforms(DescriptorAllocator *descriptor, const VkCommandBuffer& command, uint32_t currentFrame) const
    {
        //0 is non-global descriptor for material
        VkDescriptorSet materialDescriptorSet = descriptor->allocateDescriptorSet(pipelinePtr->getDescriptorSetLayouts().at(1), currentFrame);

        if(pipelinePtr->getPipelineType() == PBR)
        {
            PBRpipelineUniforms data;
            
            data.bruh = glm::vec4(0.5f, 0.5f, 1.0f, 1.0f);

            materialUBO->updateUniformBuffer(&data, sizeof(PBRpipelineUniforms));

            descriptor->writeImageArray(textures, 1, materialDescriptorSet);

            descriptor->writeUniform(
            materialUBO->getBuffer(),
            sizeof(PBRpipelineUniforms),
            0,
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            materialDescriptorSet);
        }
        else if(pipelinePtr->getPipelineType() == TexturelessPBR)
        {
            TexturelessPBRpipelineUniforms data;

            memcpy(data.inColors, colors.data(), colors.size() * sizeof(glm::vec4));

            materialUBO->updateUniformBuffer(&data, sizeof(TexturelessPBRpipelineUniforms));

            descriptor->writeUniform(
            materialUBO->getBuffer(),
            sizeof(TexturelessPBRpipelineUniforms),
            0,
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            materialDescriptorSet);

        }
        
        vkCmdBindDescriptorSets(command,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelinePtr->getLayout(),
            1, //material bind point
            1,
            &materialDescriptorSet,
            0,
            0);
    }
}