#include "Material.h"
#include "Camera.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    //----------MATERIAL DEFINITIONS----------//

    Material::Material(RenderEngine& renderer, const RasterPipelineBuildInfo& pipelineInfo, const std::function<void(VkCommandBuffer, const Camera&)>& bindFunction)
        :bindFunction(bindFunction),
        renderer(renderer)
    {
        //build pipeline
        rasterPipeline = renderer.getPipelineBuilder().buildRasterPipeline(pipelineInfo);

        //assign indirect draw matrices descriptor index if used
        for(const auto& [index, layout] : pipelineInfo.descriptorSets)
        {
            if(layout == renderer.getDefaultDescriptorSetLayout(INDIRECT_DRAW_MATRICES))
            {
                indirectDrawMatricesLocation = index;
                break;
            }
        }
    }

    Material::~Material()
    {
        //destroy graphics pipeline
        rasterPipeline.reset();
    }

    void Material::bind(VkCommandBuffer cmdBuffer, const Camera& camera) const
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline->getPipeline());
        bindFunction(cmdBuffer, camera);
    }

    //----------MATERIAL INSTANCE DEFINITIONS----------//

    MaterialInstance::MaterialInstance(RenderEngine& renderer, const Material& baseMaterial, const std::function<void(VkCommandBuffer)>& bindFunction)
        :bindFunction(bindFunction),
        baseMaterial(baseMaterial),
        renderer(renderer)
    {
    }

    MaterialInstance::~MaterialInstance()
    {
    }

    void MaterialInstance::bind(VkCommandBuffer cmdBuffer) const
    {
        bindFunction(cmdBuffer);
    }

    //----------RT MATERIAL DEFINITIONS----------//

    PaperRenderer::RTMaterial::RTMaterial(RenderEngine& renderer, const ShaderHitGroup& hitGroup)
        :renderer(renderer)
    {
        if(hitGroup.chitShaderData.size())
        {
            shaderHitGroup.emplace(std::make_pair(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, std::make_unique<Shader>(renderer, hitGroup.chitShaderData)));
        }
        if(hitGroup.ahitShaderData.size())
        {
            shaderHitGroup.emplace(std::make_pair(VK_SHADER_STAGE_ANY_HIT_BIT_KHR, std::make_unique<Shader>(renderer, hitGroup.ahitShaderData)));
        }
        if(hitGroup.intShaderData.size())
        {
            shaderHitGroup.emplace(std::make_pair(VK_SHADER_STAGE_INTERSECTION_BIT_KHR, std::make_unique<Shader>(renderer, hitGroup.intShaderData)));
        }
    }

    PaperRenderer::RTMaterial::~RTMaterial()
    {
    }
}