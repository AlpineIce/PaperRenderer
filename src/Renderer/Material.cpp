#include "Material.h"

namespace Renderer
{
    Material::Material(Device* device, Pipeline* pipeline, std::string matName, std::vector<Texture const*> textures, std::vector<glm::vec4> colors)
        :devicePtr(device),
        pipelinePtr(pipeline),
        matName(matName),
        textures(textures),
        colors(colors)

    {
        switch(pipelinePtr->getPipelineType())
        {
            case PipelineType::PBR:
                textures.resize(TEXTURE_ARRAY_SIZE);
                break;

            case PipelineType::TexturelessPBR:
                colors.resize(TEXTURE_ARRAY_SIZE);
                break;
        }
    }

    Material::~Material()
    {
    }

    void Material::updateUniforms(DescriptorAllocator *descriptor, const VkCommandBuffer& command, uint32_t currentFrame) const
    {
        VkDescriptorSet materialDescriptorSet = descriptor->allocateDescriptorSet(pipelinePtr->getDescriptorLayout(), currentFrame);

        if(pipelinePtr->getPipelineType() == PBR)
        {
            PBRpipelineUniforms data;
            data.bruh = glm::vec3(0.5f, 0.5f, 0.5f);

            pipelinePtr->getUBO()->updateUniformBuffer(&data, 0, sizeof(PBRpipelineUniforms));

            descriptor->writeImageArray(textures, 1, materialDescriptorSet);
        }
        else if(pipelinePtr->getPipelineType() == TexturelessPBR)
        {
            TexturelessPBRpipelineUniforms data;

            memcpy(data.inColors, colors.data(), colors.size());

            pipelinePtr->getUBO()->updateUniformBuffer(&data, 0, sizeof(TexturelessPBRpipelineUniforms));

        }
        
        descriptor->writeUniform(
            pipelinePtr->getUBO()->getBuffer(),
            sizeof(PBRpipelineUniforms),
            0,
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            materialDescriptorSet);

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