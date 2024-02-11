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

    void Material::bindPipeline(const VkCommandBuffer &cmdBuffer, const StorageBuffer& lightingBuffer, uint32_t lightingBufferOffset, uint32_t lightCount, const UniformBuffer& lightingData, uint32_t currentImage) const
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline->getPipeline());

        VkDescriptorSet globalDescriptorSet = rendererInfo.descriptorsPtr->allocateDescriptorSet(rasterPipeline->getDescriptorSetLayouts().at(0), currentImage);

        //light buffer
        rendererInfo.descriptorsPtr->writeUniform(
            lightingBuffer.getBuffer(),
            sizeof(PointLight) * lightCount,
            lightingBufferOffset,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            globalDescriptorSet);

        //lighting data
        rendererInfo.descriptorsPtr->writeUniform(
            lightingData.getBuffer(),
            sizeof(ShaderLightingInformation),
            0,
            1,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            globalDescriptorSet);

        vkCmdBindDescriptorSets(cmdBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            rasterPipeline->getLayout(),
            0, //pipeline bind point
            1,
            &globalDescriptorSet,
            0,
            0);
    }

    void Material::updateUniforms(void const* uniforms, VkDeviceSize uniformsSize, const std::vector<Renderer::Texture const*>& textures, const VkCommandBuffer &cmdBuffer, uint32_t currentImage) const
    {
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