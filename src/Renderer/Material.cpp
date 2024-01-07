#include "Material.h"

namespace Renderer
{
    Material::Material(Device* device, PipelineType pipelineType, std::string matName, std::vector<Texture const*> textures)
        :devicePtr(device),
        pipelineType(pipelineType),
        matName(matName),
        textures(textures)

    {
        switch(pipelineType)
        {
            case PipelineType::PBR:
                break;
        }
    }

    Material::~Material()
    {
    }
}