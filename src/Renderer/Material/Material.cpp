#include "Material.h"

namespace Renderer
{
    //----------MATERIAL DEFINITIONS----------//

    MaterialRendererInfo Material::rendererInfo;

    Material::Material( std::string materialName,uint32_t matUBOsize)
        :matName(materialName)
    {
        materialUBOs.resize(CmdBufferAllocator::getFrameCount());
        for(std::shared_ptr<UniformBuffer>& buffer : materialUBOs)
        {
            buffer = std::make_shared<UniformBuffer>(rendererInfo.devicePtr, rendererInfo.commandsPtr, matUBOsize);
        }
    }

    Material::~Material()
    {

    }

    void Material::buildPipelines(const PipelineBuildInfo &rasterInfo, const PipelineBuildInfo &rtInfo)
    {
        this->rasterPipeline = rendererInfo.pipelineBuilderPtr->buildRasterPipeline(rasterInfo);
        //this->rtPipeline = pipelineBuilderPtr->buildRTPipeline(rtInfo);               TODO RAY TRACING IS INCOMPLETE
    }

    void Material::initRendererInfo(Device* device, CmdBufferAllocator *commands, DescriptorAllocator *descriptors, PipelineBuilder *pipelineBuilder)
    {
        rendererInfo = {
            .devicePtr = device,
            .commandsPtr = commands,
            .descriptorsPtr = descriptors,
            .pipelineBuilderPtr = pipelineBuilder
        };
    }

    void Material::bindPipeline(const VkCommandBuffer &cmdBuffer, UniformBuffer &globalUBO, const GlobalDescriptor &globalData, uint32_t currentImage) const
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline->getPipeline());

        //update global uniform
        globalUBO.updateUniformBuffer(&globalData, sizeof(globalData));

        VkDescriptorSet globalDescriptorSet = rendererInfo.descriptorsPtr->allocateDescriptorSet(*rasterPipeline->getGlobalDescriptorLayoutPtr(), currentImage);
        rendererInfo.descriptorsPtr->writeUniform(globalUBO.getBuffer(), sizeof(globalData), 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, globalDescriptorSet);

        vkCmdBindDescriptorSets(cmdBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            rasterPipeline->getLayout(),
            0, //material bind point
            1,
            &globalDescriptorSet,
            0,
            0);
    }

    void Material::updateUniforms(void const* uniforms, VkDeviceSize uniformsSize, const std::vector<Renderer::Texture const*>& textures, const VkCommandBuffer &cmdBuffer, uint32_t currentImage) const
    {
        //0 is non-global descriptor for material
        VkDescriptorSet materialDescriptorSet = rendererInfo.descriptorsPtr->allocateDescriptorSet(rasterPipeline->getDescriptorSetLayouts().at(1), currentImage);

        materialUBOs.at(currentImage)->updateUniformBuffer(uniforms, uniformsSize);

        rendererInfo.descriptorsPtr->writeImageArray(textures, 1, materialDescriptorSet);
        rendererInfo.descriptorsPtr->writeUniform(
            materialUBOs.at(currentImage)->getBuffer(),
            uniformsSize,
            0,
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            materialDescriptorSet);
        
        vkCmdBindDescriptorSets(cmdBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            rasterPipeline->getLayout(),
            1, //material bind point
            1,
            &materialDescriptorSet,
            0,
            0);
    }

    //----------DEFAULT MATERIAL DEFINITIONS----------//

    DefaultMaterial::DefaultMaterial(std::string vertexShaderPath, std::string fragmentShaderPath)
        :Material("m_Default", sizeof(Uniforms))
            
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

        //descriptor set 1 (material)
        DescriptorSet set1Descriptors;
        
        VkDescriptorSetLayoutBinding uniformDescriptor = {};
        uniformDescriptor.binding = 0;
        uniformDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformDescriptor.descriptorCount = 1;
        uniformDescriptor.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        set1Descriptors.descriptorBindings.push_back(uniformDescriptor);

        VkDescriptorSetLayoutBinding textureDescriptor = {};
        textureDescriptor.binding = 1;
        textureDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureDescriptor.descriptorCount = TEXTURE_ARRAY_SIZE;
        textureDescriptor.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        set1Descriptors.descriptorBindings.push_back(textureDescriptor);

        //descriptor set 2 (object)
        DescriptorSet set2Descriptors;

        VkDescriptorSetLayoutBinding objDescriptor = {};
        objDescriptor.binding = 0;
        objDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        objDescriptor.descriptorCount = 1;
        objDescriptor.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        set2Descriptors.descriptorBindings.push_back(objDescriptor);

        std::vector<DescriptorSet> descriptorSets = {set1Descriptors, set2Descriptors};

        PipelineBuildInfo rasterInfo;
        rasterInfo.shaderInfo = shaderPairs;
        rasterInfo.useGlobalDescriptor = true;
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
        rtInfo.descriptors = std::vector<DescriptorSet>();
        rtInfo.shaderInfo = RTshaderPairs;
        rtInfo.useGlobalDescriptor = true;

        buildPipelines(rasterInfo, rtInfo);

        //setup default instance
        defaultInstance.parentMaterial = this;
        defaultInstance.uniformSize = sizeof(Uniforms);
        defaultInstance.uniformData = &defaultUniforms;
        defaultInstance.textures = std::vector<const Renderer::Texture*>(); //no textures in texture array
    }

    DefaultMaterial::~DefaultMaterial()
    {

    }
}