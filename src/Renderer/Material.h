#pragma once
#include "vulkan/vulkan.hpp"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"

#include <string>
#include <memory>

namespace Renderer
{
    struct MaterialParameters
    {
        int a;
    };

    //----------MATERIAL DECLARATIONS----------//

    class Material
    {
    private:
        std::string matName;
        const PipelineType pipelineType;
        MaterialParameters parameters;

        std::vector<Texture const*> textures;
        Device* devicePtr;

    public:
        Material(Device* device, PipelineType pipelineType, std::string matName, std::vector<Texture const*> textures);
        ~Material();

        std::string getMatName() const { return matName; }
        const PipelineType getPipelineType() const { return pipelineType; }
        void setParameters(MaterialParameters parameters) { this->parameters = parameters; }
        MaterialParameters getParameterValues() const { return parameters; }
        std::vector<Texture const*> getTextures() const { return textures; }
    };
}