#include "Material.h"

namespace Renderer
{
    Material::Material(Device* device, PipelineType pipelineType, std::string matName, std::vector<Texture const*> textures, std::vector<glm::vec4> colors)
        :devicePtr(device),
        pipelineType(pipelineType),
        matName(matName),
        textures(textures),
        colors(colors)

    {
        switch(pipelineType)
        {
            case PipelineType::PBR:
                break;

            case PipelineType::TexturelessPBR:
                break;
        }
    }

    Material::~Material()
    {
    }
}