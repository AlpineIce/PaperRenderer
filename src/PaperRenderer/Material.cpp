#include "Material.h"
#include "Camera.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    //----------MATERIAL DEFINITIONS----------//

    Material::Material(RenderEngine& renderer, const RasterPipelineBuildInfo& pipelineInfo, const std::unordered_map<uint32_t, VkDescriptorSetLayout>& materialDescriptorSets)
        :descriptorGroup(renderer, materialDescriptorSets),
        renderer(renderer)
    {
        //build pipeline
        rasterPipeline = renderer.getPipelineBuilder().buildRasterPipeline(pipelineInfo);
    }

    Material::~Material()
    {
        //destroy graphics pipeline
        rasterPipeline.reset();
    }

    void Material::updateDescriptors(std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> descriptorWrites) const
    {
        descriptorGroup.updateDescriptorSets(descriptorWrites);
    }

    void Material::bind(VkCommandBuffer cmdBuffer) const
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline->getPipeline());
        descriptorGroup.bindSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline->getLayout(), {});
    }

    //----------MATERIAL INSTANCE DEFINITIONS----------//

    MaterialInstance::MaterialInstance(RenderEngine& renderer, const Material& baseMaterial, const std::unordered_map<uint32_t, VkDescriptorSetLayout>& instanceDescriptorSets)
        :descriptorGroup(renderer, instanceDescriptorSets),
        baseMaterial(baseMaterial),
        renderer(renderer)
    {
    }

    MaterialInstance::~MaterialInstance()
    {
    }

    void MaterialInstance::updateDescriptors(std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> descriptorWrites) const
    {
        descriptorGroup.updateDescriptorSets(descriptorWrites);
    }

    void MaterialInstance::bind(VkCommandBuffer cmdBuffer) const
    {
        descriptorGroup.bindSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, baseMaterial.getRasterPipeline().getLayout(), {});
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