#include "Material.h"

namespace PaperRenderer
{
    //----------MATERIAL DEFINITIONS----------//

    Material::Material(std::string materialName)
        :matName(materialName)
    {
        rasterInfo.shaderInfo = &shaderPairs;
        rasterInfo.descriptors = &rasterDescriptorSets;

        rtInfo.shaderInfo = &rtShaderPairs;
        rtInfo.descriptors = &rtDescriptorSets;

        rasterDescriptorSets[DescriptorScopes::RASTER_MATERIAL];
        rasterDescriptorSets[DescriptorScopes::RASTER_MATERIAL_INSTANCE];
        rasterDescriptorSets[DescriptorScopes::RASTER_OBJECT];
    }

    Material::~Material()
    {
    }

    void Material::buildPipelines(PipelineBuildInfo const* rasterInfo, const RasterPipelineProperties& rasterProperties, PipelineBuildInfo const* rtInfo, const RTPipelineProperties& rtProperties)
    {
        if(rasterInfo) this->rasterPipeline = PipelineBuilder::getRendererInfo().pipelineBuilderPtr->buildRasterPipeline(*rasterInfo, rasterProperties);
        if(rtInfo) this->rtPipeline = PipelineBuilder::getRendererInfo().pipelineBuilderPtr->buildRTPipeline(*rtInfo, rtProperties);
    }

    void Material::bind(VkCommandBuffer cmdBuffer, uint32_t currentImage)
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline->getPipeline());

        if(rasterDescriptorWrites.bufferViewWrites.size() || rasterDescriptorWrites.bufferWrites.size() || rasterDescriptorWrites.imageWrites.size())
        {
            VkDescriptorSet materialDescriptorSet = PipelineBuilder::getRendererInfo().descriptorsPtr->allocateDescriptorSet(rasterPipeline->getDescriptorSetLayouts().at(RASTER_MATERIAL), currentImage);
            DescriptorAllocator::writeUniforms(PipelineBuilder::getRendererInfo().devicePtr->getDevice(), materialDescriptorSet, rasterDescriptorWrites);

            DescriptorBind bindingInfo = {};
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            bindingInfo.set = materialDescriptorSet;
            bindingInfo.descriptorScope = RASTER_MATERIAL;
            bindingInfo.layout = rasterPipeline->getLayout();
            
            DescriptorAllocator::bindSet(PipelineBuilder::getRendererInfo().devicePtr->getDevice(), cmdBuffer, bindingInfo);
        }
    }

    //----------MATERIAL INSTANCE DEFINITIONS----------//

    MaterialInstance::MaterialInstance(Material const *baseMaterial)
        :baseMaterial(baseMaterial)
    {
    }

    MaterialInstance::~MaterialInstance()
    {
    }

    void MaterialInstance::bind(VkCommandBuffer cmdBuffer, uint32_t currentImage)
    {
        if(descriptorWrites.bufferViewWrites.size() || descriptorWrites.bufferWrites.size() || descriptorWrites.imageWrites.size())
        {
            VkDescriptorSet instDescriptorSet = PipelineBuilder::getRendererInfo().descriptorsPtr->allocateDescriptorSet(baseMaterial->getRasterPipeline()->getDescriptorSetLayouts().at(RASTER_MATERIAL_INSTANCE), currentImage);
            DescriptorAllocator::writeUniforms(PipelineBuilder::getRendererInfo().devicePtr->getDevice(), instDescriptorSet, descriptorWrites);

            DescriptorBind bindingInfo = {};
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            bindingInfo.set = instDescriptorSet;
            bindingInfo.descriptorScope = RASTER_MATERIAL_INSTANCE;
            bindingInfo.layout = baseMaterial->getRasterPipeline()->getLayout();
            
            DescriptorAllocator::bindSet(PipelineBuilder::getRendererInfo().devicePtr->getDevice(), cmdBuffer, bindingInfo);
        }
    }
}