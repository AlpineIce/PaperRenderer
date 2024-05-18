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

    void Material::buildPipelines(PipelineBuildInfo const* rasterInfo, PipelineBuildInfo const* rtInfo)
    {
        if(rasterInfo) this->rasterPipeline = PipelineBuilder::getRendererInfo().pipelineBuilderPtr->buildRasterPipeline(*rasterInfo);
        if(rtInfo) this->rtPipeline = PipelineBuilder::getRendererInfo().pipelineBuilderPtr->buildRTPipeline(*rtInfo);
    }

    void Material::bind(VkCommandBuffer cmdBuffer, uint32_t currentImage) const
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

    void MaterialInstance::bind(VkCommandBuffer cmdBuffer, uint32_t currentImage) const
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

    //----------DEFAULT MATERIAL DEFINITIONS----------//

    DefaultMaterial::DefaultMaterial(std::string fileDir)
        :Material("m_Default")
    {
        //----------RASTER PIPELINE INFO----------//

        ShaderPair vert = {
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .directory = fileDir + vertexFileName
        };
        shaderPairs.push_back(vert);
        ShaderPair frag = {
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .directory = fileDir + fragFileName
        };
        shaderPairs.push_back(frag);

        VkDescriptorSetLayoutBinding objDescriptor = {};
        objDescriptor.binding = 0;
        objDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        objDescriptor.descriptorCount = 1;
        objDescriptor.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        rasterDescriptorSets[RASTER_OBJECT].descriptorBindings[0] = objDescriptor;

        //----------RT PIPELINE INFO----------//

        ShaderPair anyHitShader = {
            .stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
            .directory = "resources/shaders/RT/RTanyHit.spv"
        };
        rtShaderPairs.push_back(anyHitShader);
        ShaderPair missShader = {
            .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
            .directory = "resources/shaders/RT/RTmiss.spv"
        };
        rtShaderPairs.push_back(missShader);
        ShaderPair closestShader = {
            .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            .directory = "resources/shaders/RT/RTclosestHit.spv"
        };
        rtShaderPairs.push_back(closestShader);
        ShaderPair raygenShader = {
            .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            .directory = "resources/shaders/RT/RTraygen.spv"
        };
        rtShaderPairs.push_back(raygenShader);
        ShaderPair intersectionShader = {
            .stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
            .directory = "resources/shaders/RT/RTintersection.spv"
        };
        rtShaderPairs.push_back(intersectionShader);

        buildPipelines(&rasterInfo, NULL); //no RT for now
    }

    DefaultMaterial::~DefaultMaterial()
    {

    }

    void DefaultMaterial::bind(VkCommandBuffer cmdBuffer, uint32_t currentImage)
    {
        Material::bind(cmdBuffer, currentImage);
    }

    DefaultMaterialInstance::DefaultMaterialInstance(Material const* baseMaterial)
        :MaterialInstance(baseMaterial)
    {

    }

    DefaultMaterialInstance::~DefaultMaterialInstance()
    {

    }

    void DefaultMaterialInstance::bind(VkCommandBuffer cmdBuffer, uint32_t currentImage)
    {
        MaterialInstance::bind(cmdBuffer, currentImage);
    }
}