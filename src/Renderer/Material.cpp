#include "Material.h"

namespace PaperRenderer
{
    //----------MATERIAL DEFINITIONS----------//

    MaterialRendererInfo Material::rendererInfo;

    Material::Material(std::string materialName)
        :matName(materialName)
    {
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

        std::vector<ShaderPair> shaderPairs;
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

        //descriptor set 0 (global)
        DescriptorSet set0Descriptors;
        set0Descriptors.setNumber = 0;
        
        VkDescriptorSetLayoutBinding lights = {};
        lights.binding = 0;
        lights.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lights.descriptorCount = 1;
        lights.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        set0Descriptors.descriptorBindings[0] = lights;

        VkDescriptorSetLayoutBinding uniformDescriptor = {};
        uniformDescriptor.binding = 1;
        uniformDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformDescriptor.descriptorCount = 1;
        uniformDescriptor.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        set0Descriptors.descriptorBindings[1] = uniformDescriptor;

        //descriptor set 1 (material)
        DescriptorSet set1Descriptors;
        set1Descriptors.setNumber = 1;

        VkDescriptorSetLayoutBinding reservedforfutureuseidk = {};
        reservedforfutureuseidk.binding = 0;
        reservedforfutureuseidk.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        reservedforfutureuseidk.descriptorCount = 1;
        reservedforfutureuseidk.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        set1Descriptors.descriptorBindings[0] = reservedforfutureuseidk;

        VkDescriptorSetLayoutBinding reservedforfutureuseidk2 = {};
        reservedforfutureuseidk2.binding = 1;
        reservedforfutureuseidk2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        reservedforfutureuseidk2.descriptorCount = 8;
        reservedforfutureuseidk2.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        set1Descriptors.descriptorBindings[1] = reservedforfutureuseidk2;

        //descriptor set 2 (object)
        DescriptorSet set2Descriptors;
        set2Descriptors.setNumber = 1;

        VkDescriptorSetLayoutBinding objDescriptor = {};
        objDescriptor.binding = 0;
        objDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        objDescriptor.descriptorCount = 1;
        objDescriptor.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        set2Descriptors.descriptorBindings[0] = objDescriptor;

        std::unordered_map<uint32_t, DescriptorSet> descriptorSets;
        descriptorSets[0] = set0Descriptors;
        descriptorSets[1] = set1Descriptors;
        descriptorSets[2] = set2Descriptors;

        PipelineBuildInfo rasterInfo;
        rasterInfo.shaderInfo = shaderPairs;
        rasterInfo.descriptors = descriptorSets;

        //----------RT PIPELINE INFO----------//

        std::vector<ShaderPair> RTshaderPairs;
        ShaderPair anyHitShader = {
            .stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
            .directory = "resources/shaders/RT/RTanyHit.spv"
        };
        RTshaderPairs.push_back(anyHitShader);
        ShaderPair missShader = {
            .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
            .directory = "resources/shaders/RT/RTmiss.spv"
        };
        RTshaderPairs.push_back(missShader);
        ShaderPair closestShader = {
            .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            .directory = "resources/shaders/RT/RTclosestHit.spv"
        };
        RTshaderPairs.push_back(closestShader);
        ShaderPair raygenShader = {
            .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            .directory = "resources/shaders/RT/RTraygen.spv"
        };
        RTshaderPairs.push_back(raygenShader);
        ShaderPair intersectionShader = {
            .stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
            .directory = "resources/shaders/RT/RTintersection.spv"
        };
        RTshaderPairs.push_back(intersectionShader);

        PipelineBuildInfo rtInfo;
        rtInfo.descriptors = std::unordered_map<uint32_t, DescriptorSet>();
        rtInfo.shaderInfo = RTshaderPairs;

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