#include "Material.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    //----------MATERIAL DEFINITIONS----------//

    Material::Material(class RenderEngine* renderer, std::string materialName)
        :rendererPtr(renderer),
        matName(materialName),
        rasterInfo({
            .shaderInfo = shaderPairs,
            .descriptors = rasterDescriptorSets
        })
    {
        rasterDescriptorSets[DescriptorScopes::RASTER_MATERIAL];
        rasterDescriptorSets[DescriptorScopes::RASTER_MATERIAL_INSTANCE];
        rasterDescriptorSets[DescriptorScopes::RASTER_OBJECT];
    }

    Material::~Material()
    {
    }

    void Material::buildRasterPipeline(RasterPipelineBuildInfo const* rasterInfo, const RasterPipelineProperties& rasterProperties)
    {
        if(rasterInfo) this->rasterPipeline = rendererPtr->getPipelineBuilder()->buildRasterPipeline(*rasterInfo, rasterProperties);
    }

    void Material::bind(VkCommandBuffer cmdBuffer, uint32_t currentImage)
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline->getPipeline());

        if(rasterDescriptorWrites.bufferViewWrites.size() || rasterDescriptorWrites.bufferWrites.size() || rasterDescriptorWrites.imageWrites.size())
        {
            VkDescriptorSet materialDescriptorSet = rendererPtr->getDescriptorAllocator()->allocateDescriptorSet(rasterPipeline->getDescriptorSetLayouts().at(RASTER_MATERIAL), currentImage);
            DescriptorAllocator::writeUniforms(rendererPtr->getDevice()->getDevice(), materialDescriptorSet, rasterDescriptorWrites);

            DescriptorBind bindingInfo = {};
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            bindingInfo.set = materialDescriptorSet;
            bindingInfo.descriptorScope = RASTER_MATERIAL;
            bindingInfo.layout = rasterPipeline->getLayout();
            
            DescriptorAllocator::bindSet(rendererPtr->getDevice()->getDevice(), cmdBuffer, bindingInfo);
        }
    }

    //----------MATERIAL INSTANCE DEFINITIONS----------//

    MaterialInstance::MaterialInstance(class RenderEngine* renderer, Material const *baseMaterial)
        :rendererPtr(renderer),
        baseMaterial(baseMaterial)
    {
    }

    MaterialInstance::~MaterialInstance()
    {
    }

    void MaterialInstance::bind(VkCommandBuffer cmdBuffer, uint32_t currentImage)
    {
        if(descriptorWrites.bufferViewWrites.size() || descriptorWrites.bufferWrites.size() || descriptorWrites.imageWrites.size())
        {
            VkDescriptorSet instDescriptorSet = rendererPtr->getDescriptorAllocator()->allocateDescriptorSet(baseMaterial->getRasterPipeline()->getDescriptorSetLayouts().at(RASTER_MATERIAL_INSTANCE), currentImage);
            DescriptorAllocator::writeUniforms(rendererPtr->getDevice()->getDevice(), instDescriptorSet, descriptorWrites);

            DescriptorBind bindingInfo = {};
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            bindingInfo.set = instDescriptorSet;
            bindingInfo.descriptorScope = RASTER_MATERIAL_INSTANCE;
            bindingInfo.layout = baseMaterial->getRasterPipeline()->getLayout();
            
            DescriptorAllocator::bindSet(rendererPtr->getDevice()->getDevice(), cmdBuffer, bindingInfo);
        }
    }
}