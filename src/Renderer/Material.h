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
        Pipeline const* pipelinePtr;
        MaterialParameters parameters;

        std::vector<Texture const*> textures;
        std::shared_ptr<UniformBuffer> materialUBO;
        std::vector<glm::vec4> colors;

        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;

        UniformBuffer* getUBO() const { return materialUBO.get(); }

    public:
        Material(Device* device, CmdBufferAllocator* commands, Pipeline* pipeline, std::string matName, std::vector<Texture const*> textures, std::vector<glm::vec4> colors);
        ~Material();

        void updateUniforms(DescriptorAllocator* descriptor, const VkCommandBuffer& command, uint32_t currentFrame) const;

        std::string getMatName() const { return matName; }
        const PipelineType getPipelineType() const { return pipelinePtr->getPipelineType(); }
        void setParameters(MaterialParameters parameters) { this->parameters = parameters; }
        MaterialParameters getParameterValues() const { return parameters; }
        std::vector<Texture const*> getTextures() const { return textures; }
        std::vector<glm::vec4> getColors() const { return colors; }
    };
}