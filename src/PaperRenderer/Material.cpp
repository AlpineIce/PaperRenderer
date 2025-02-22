#include "Material.h"
#include "Camera.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    //----------MATERIAL DEFINITIONS----------//

    Material::Material(RenderEngine& renderer, const RasterPipelineInfo& pipelineInfo, const std::function<void(VkCommandBuffer, const Camera&)>& bindFunction)
        :bindFunction(bindFunction),
        rasterPipeline(renderer, pipelineInfo),
        renderer(renderer)
    {
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
    }

    void Material::bind(VkCommandBuffer cmdBuffer, const Camera& camera) const
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline.getPipeline());
        if(bindFunction) bindFunction(cmdBuffer, camera);
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
}