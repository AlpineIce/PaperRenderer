#include "Material.h"

namespace PaperRenderer
{
    //----------MATERIAL DEFINITIONS----------//

    MaterialRendererInfo Material::rendererInfo;

    Material::Material(std::string materialName)
        :matName(materialName)
    {
        //descriptor set 0 (global)
        set0Descriptors.setNumber = 0;
        descriptorSets[0] = &set0Descriptors;

        //descriptor set 1 (material)
        set1Descriptors.setNumber = 1;
        descriptorSets[1] = &set1Descriptors;

        //descriptor set 2 (object)
        set2Descriptors.setNumber = 2;
        descriptorSets[2] = &set2Descriptors;

        rasterInfo.shaderInfo = &shaderPairs;
        rasterInfo.descriptors = &descriptorSets;
        rtInfo.descriptors = &rtDescriptorSets;
        rtInfo.shaderInfo = &rtShaderPairs;
    }

    Material::~Material()
    {
    }

    void Material::buildPipelines(PipelineBuildInfo const* rasterInfo, PipelineBuildInfo const* rtInfo)
    {
        if(rasterInfo) this->rasterPipeline = rendererInfo.pipelineBuilderPtr->buildRasterPipeline(*rasterInfo);
        if(rtInfo) this->rtPipeline = rendererInfo.pipelineBuilderPtr->buildRTPipeline(*rtInfo);
    }

    void Material::initRendererInfo(Device* device, DescriptorAllocator *descriptors, PipelineBuilder *pipelineBuilder)
    {
        rendererInfo = {
            .devicePtr = device,
            .descriptorsPtr = descriptors,
            .pipelineBuilderPtr = pipelineBuilder
        };
    }

    void Material::bind(VkCommandBuffer cmdBuffer, uint32_t currentImage) const
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline->getPipeline());
        if(descriptorWrites.bufferViewWrites.size() || descriptorWrites.bufferWrites.size() || descriptorWrites.imageWrites.size())
        {
            VkDescriptorSet materialDescriptorSet = rendererInfo.descriptorsPtr->allocateDescriptorSet(rasterPipeline->getDescriptorSetLayouts().at(RasterDescriptorScopes::MATERIAL), currentImage);
            DescriptorAllocator::writeUniforms(rendererInfo.devicePtr->getDevice(), materialDescriptorSet, descriptorWrites);

            DescriptorBind bindingInfo = {};
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            bindingInfo.set = materialDescriptorSet;
            bindingInfo.setNumber = RasterDescriptorScopes::MATERIAL;
            bindingInfo.layout = rasterPipeline->getLayout();
            
            DescriptorAllocator::bindSet(rendererInfo.devicePtr->getDevice(), cmdBuffer, bindingInfo);
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
            VkDescriptorSet instDescriptorSet = Material::getRendererInfo().descriptorsPtr->allocateDescriptorSet(baseMaterial->getRasterPipeline()->getDescriptorSetLayouts().at(RasterDescriptorScopes::MATERIAL_INSTANCE), currentImage);
            DescriptorAllocator::writeUniforms(Material::getRendererInfo().devicePtr->getDevice(), instDescriptorSet, descriptorWrites);

            DescriptorBind bindingInfo = {};
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            bindingInfo.set = instDescriptorSet;
            bindingInfo.setNumber = RasterDescriptorScopes::MATERIAL_INSTANCE;
            bindingInfo.layout = baseMaterial->getRasterPipeline()->getLayout();
            
            DescriptorAllocator::bindSet(Material::getRendererInfo().devicePtr->getDevice(), cmdBuffer, bindingInfo);
        }
    }

    //----------DEFAULT MATERIAL DEFINITIONS----------//

    DefaultMaterial::DefaultMaterial(std::string vertexShaderPath, std::string fragmentShaderPath)
        :Material("m_Default")
    {
        //----------RASTER PIPELINE INFO----------//

        ShaderPair vert = {
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .directory = vertexShaderPath
        };
        shaderPairs.push_back(vert);
        ShaderPair frag = {
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .directory = fragmentShaderPath
        };
        shaderPairs.push_back(frag);

        VkDescriptorSetLayoutBinding objDescriptor = {};
        objDescriptor.binding = 0;
        objDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        objDescriptor.descriptorCount = 1;
        objDescriptor.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        set2Descriptors.descriptorBindings[0] = objDescriptor;

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