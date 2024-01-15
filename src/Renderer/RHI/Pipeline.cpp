#include "Pipeline.h"
#include "Command.h"
#include "Buffer.h"

#include <fstream>

namespace Renderer
{
    //----------SHADER DEFINITIONS----------//

    Shader::Shader(Device *device, std::string location)
        : devicePtr(device)
    {
        compiledShader = getShaderData(location);

        VkShaderModuleCreateInfo creationInfo;
        creationInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        creationInfo.pNext = NULL;
        creationInfo.flags = 0;
        creationInfo.codeSize = compiledShader.size();
        creationInfo.pCode = compiledShader.data();

        VkResult result = vkCreateShaderModule(devicePtr->getDevice(), &creationInfo, nullptr, &program);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Creation of shader at location " + location + " failed.");
        }
    }

    Shader::~Shader()
    {
        vkDestroyShaderModule(devicePtr->getDevice(), program, nullptr);
    }

    std::vector<uint32_t> Shader::getShaderData(std::string location)
    {
        
        std::ifstream file(location, std::ios::binary);
        std::vector<uint32_t> buffer;

        if(file.is_open())
        {
            file.seekg (0, file.end);
            uint32_t length = file.tellg();
            file.seekg (0, file.beg);

            buffer.resize(length);
            file.read((char*)buffer.data(), length);

            file.close();

            return buffer;
        }
        else
        {
            throw std::runtime_error("Couldnt open pipeline shader file " + location);
        }

        return std::vector<uint32_t>();
    }

    //----------COMPUTE PIPELINE DEFINITIONS---------//

    ComputePipeline::ComputePipeline(Device *device, DescriptorAllocator* descriptors, std::string shaderLocation)
    {/*
        shader = std::make_shared<Shader>(devicePtr, shaderLocation);

        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(glm::mat4x4) * 2;
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkPipelineLayoutCreateInfo layoutInfo;
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = NULL;
        layoutInfo.flags = 0;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = descriptorsPtr->getSetLayoutPtr();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        VkResult result = vkCreatePipelineLayout(devicePtr->getDevice(), &layoutInfo, nullptr, &pipelineLayout);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Pipeline layout creation failed");
        }

        VkPipelineShaderStageCreateInfo pipelineInfo;
        pipelineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.pNext = NULL;
        pipelineInfo.flags = 0;
        pipelineInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.module = shader->getModule();
        pipelineInfo.pName = "main"; //use main() function in shaders
        pipelineInfo.pSpecializationInfo = NULL;

        VkComputePipelineCreateInfo creationInfo;
        creationInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        creationInfo.pNext = NULL;
        creationInfo.flags = 0;
        creationInfo.stage = pipelineInfo;
        creationInfo.layout = pipelineLayout;
        
        VkResult result2 = vkCreateComputePipelines(devicePtr->getDevice(), VK_NULL_HANDLE, 1, &creationInfo, nullptr, &pipeline);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create compute pipeline");
        }*/
    }

    ComputePipeline::~ComputePipeline()
    {
    }

    //----------PIPELINE DEFINITIONS---------//

    VkPipelineCache Pipeline::cache;
    VkDescriptorSetLayout Pipeline::globalDescriptorLayout;

    Pipeline::Pipeline(Device *device, CmdBufferAllocator* commands, std::vector<std::string> &shaderFiles, DescriptorAllocator *descriptors)
        :devicePtr(device),
        commandsPtr(commands),
        descriptorsPtr(descriptors)
    {
        createShaders(shaderFiles);
    }

    Pipeline::~Pipeline()
    {
        vkDestroyPipeline(devicePtr->getDevice(), pipeline, nullptr);
        vkDestroyPipelineLayout(devicePtr->getDevice(), pipelineLayout, nullptr);
    }

    void Pipeline::createCache(Device* device)
    {
        VkPipelineCacheCreateInfo creationInfo;
        creationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        creationInfo.pNext = NULL;
        creationInfo.flags = 0;
        creationInfo.initialDataSize = 0;
        creationInfo.pInitialData = NULL;
        vkCreatePipelineCache(device->getDevice(), &creationInfo, nullptr, &cache);
    }

    void Pipeline::destroyCache(Device* device)
    {
        vkDestroyPipelineCache(device->getDevice(), cache, nullptr);
    }

    void Pipeline::createGlobalDescriptorLayout(Device *device)
    {
        VkDescriptorSetLayoutBinding uniformDescriptor = {};
        uniformDescriptor.binding = 0;
        uniformDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformDescriptor.descriptorCount = 1;
        uniformDescriptor.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        uniformDescriptor.pImmutableSamplers = NULL;
        
        //descriptor info
        VkDescriptorSetLayoutCreateInfo descriptorInfo = {};
        descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorInfo.pNext = NULL;
        descriptorInfo.flags = 0;
        descriptorInfo.bindingCount = 1;
        descriptorInfo.pBindings = &uniformDescriptor;

        VkResult result = vkCreateDescriptorSetLayout(device->getDevice(), &descriptorInfo, nullptr, &globalDescriptorLayout);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor set layout");
        }
    }

    void Pipeline::destroyGlobalDescriptorLayout(Device *device)
    {
        vkDestroyDescriptorSetLayout(device->getDevice(), globalDescriptorLayout, nullptr);
    }

    void Pipeline::createShaders(std::vector<std::string>& shaderFiles)
    {
        for(const std::string& shaderFile : shaderFiles)
        {
            std::string a = shaderFile.substr(shaderFile.size() - sizeof("vert.spv"));
            if(shaderFile.substr(shaderFile.size() - sizeof("vert.spv") + 1) == std::string("vert.spv"))
            {
                shaders.emplace(VK_SHADER_STAGE_VERTEX_BIT, std::make_shared<Shader>(devicePtr, shaderFile));
            }
            else if(shaderFile.substr(shaderFile.size() - sizeof("frag.spv") + 1) == std::string("frag.spv"))
            {
                shaders.emplace(VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<Shader>(devicePtr, shaderFile));
            }
            else
            {
                throw std::runtime_error("Couldn't find shader stage for " + shaderFile);
            }
        }
    }

    //----------RASTER PIPELINE DEFINITIONS---------//

    RasterPipeline::RasterPipeline(Device* device, CmdBufferAllocator* commands, std::vector<std::string>& shaderFiles, DescriptorAllocator* descriptors, PipelineType pipelineType, Swapchain* swapchain)
        :Pipeline(device, commands, shaderFiles, descriptors)
    {
        this->pipelineType = pipelineType;
        if(pipelineType == PBR) materialUBO = std::make_shared<UniformBuffer>(devicePtr, commandsPtr, (uint32_t)sizeof(PBRpipelineUniforms));
        else materialUBO = std::make_shared<UniformBuffer>(devicePtr, commandsPtr, (uint32_t)sizeof(TexturelessPBRpipelineUniforms));
        
        createDescriptorLayout();

        vertexDescription.binding = 0;
        vertexDescription.stride = sizeof(Vertex);
        vertexDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        //vertex position
        vertexAttributes.push_back(VkVertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT, //vec3
            .offset = offsetof(Vertex, position)
        });
        //normals
        vertexAttributes.push_back(VkVertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT, //vec3
            .offset = offsetof(Vertex, normal)
        });
        //texture Coordinates
        vertexAttributes.push_back(VkVertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT, //vec2
            .offset = offsetof(Vertex, texCoord)
        });

        //pipeline info from here on
        VkPipelineRenderingCreateInfo renderingInfo = {};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.pNext = NULL;
        renderingInfo.viewMask = 0;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = swapchain->getFormatPtr();
        renderingInfo.depthAttachmentFormat = swapchain->getDepthFormat();
        renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
            VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT
        };
        
        VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
        dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateInfo.pNext = NULL;
        dynamicStateInfo.flags = 0;
        dynamicStateInfo.dynamicStateCount = dynamicStates.size();
        dynamicStateInfo.pDynamicStates = dynamicStates.data();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.pNext = NULL;
        vertexInputInfo.flags = 0;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &vertexDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = vertexAttributes.size();
        vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
        inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyInfo.pNext = NULL;
        inputAssemblyInfo.flags = 0;
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

        VkPipelineTessellationStateCreateInfo tessellationInfo = {};
        tessellationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
        tessellationInfo.pNext = NULL;
        tessellationInfo.flags = 0;
        tessellationInfo.patchControlPoints = 1;

        VkPipelineViewportStateCreateInfo viewportInfo = {};
        viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportInfo.pNext = NULL;
        viewportInfo.flags = 0;
        viewportInfo.viewportCount = 0;
        viewportInfo.scissorCount = 0;

        VkPipelineRasterizationStateCreateInfo rasterInfo = {};
        rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterInfo.pNext = NULL;
        rasterInfo.flags = 0;
        rasterInfo.depthClampEnable = VK_FALSE;
        rasterInfo.rasterizerDiscardEnable = VK_FALSE;
        rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
        rasterInfo.lineWidth = 1.0f;
        rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterInfo.depthBiasEnable = VK_FALSE;
        rasterInfo.depthBiasConstantFactor = 0.0f;
        rasterInfo.depthBiasClamp = 0.0f;
        rasterInfo.depthBiasSlopeFactor = 0.0f;

        VkPipelineMultisampleStateCreateInfo MSAA = {};
        MSAA.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        MSAA.pNext = NULL;
        MSAA.flags = 0;
        MSAA.sampleShadingEnable = VK_FALSE;
        MSAA.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        MSAA.minSampleShading = 1.0f;
        MSAA.pSampleMask = NULL;
        MSAA.alphaToCoverageEnable = VK_FALSE;
        MSAA.alphaToOneEnable = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo  depthStencilInfo = {};
        depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilInfo.pNext = NULL;
        depthStencilInfo.flags = 0;
        depthStencilInfo.depthTestEnable = VK_TRUE;
        depthStencilInfo.depthWriteEnable = VK_TRUE;
        depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
        depthStencilInfo.stencilTestEnable = VK_FALSE;
        depthStencilInfo.front = {};
        depthStencilInfo.back = {};

        VkPipelineColorBlendAttachmentState colorAttachment = {};
        colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorAttachment.blendEnable = VK_FALSE;
        colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        
        VkPipelineColorBlendStateCreateInfo colorInfo = {};
        colorInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorInfo.pNext = NULL;
        colorInfo.flags = 0;
        colorInfo.logicOpEnable = VK_FALSE;
        colorInfo.logicOp = VK_LOGIC_OP_COPY;
        colorInfo.attachmentCount = 1;
        colorInfo.pAttachments = &colorAttachment;
        colorInfo.blendConstants[0] = 0.0f;
        colorInfo.blendConstants[1] = 0.0f;
        colorInfo.blendConstants[2] = 0.0f;
        colorInfo.blendConstants[3] = 0.0f;
        
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        for(const auto& [shaderStage, shader] : shaders)
        {
            VkPipelineShaderStageCreateInfo stageInfo = {};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            stageInfo.pNext = NULL,
            stageInfo.flags = 0,
            stageInfo.stage = shaderStage,
            stageInfo.module = shader->getModule(),
            stageInfo.pName = "main", //use main() function in shaders
            stageInfo.pSpecializationInfo = NULL;
            
            shaderStages.push_back(stageInfo);
        }

        VkGraphicsPipelineCreateInfo creationInfo = {};
        creationInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        creationInfo.pNext = &renderingInfo;
        creationInfo.flags = 0;
        creationInfo.stageCount = shaderStages.size();
        creationInfo.pStages = shaderStages.data();
        creationInfo.pVertexInputState = &vertexInputInfo;
        creationInfo.pInputAssemblyState = &inputAssemblyInfo;
        creationInfo.pTessellationState = &tessellationInfo;
        creationInfo.pViewportState = &viewportInfo;
        creationInfo.pRasterizationState = &rasterInfo;
        creationInfo.pMultisampleState = &MSAA;
        creationInfo.pDepthStencilState = &depthStencilInfo;
        creationInfo.pColorBlendState = &colorInfo;
        creationInfo.pDynamicState = &dynamicStateInfo;
        creationInfo.layout = pipelineLayout;
        creationInfo.renderPass = NULL;
        creationInfo.subpass = 0;
        creationInfo.basePipelineHandle = VK_NULL_HANDLE;
        creationInfo.basePipelineIndex = -1;
        
        VkResult result = vkCreateGraphicsPipelines(devicePtr->getDevice(), VK_NULL_HANDLE, 1, &creationInfo, nullptr, &pipeline);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create a graphics pipeline");
        }
    }

    RasterPipeline::~RasterPipeline()
    {
        vkDestroyDescriptorSetLayout(devicePtr->getDevice(), descriptorLayout, nullptr);
    }

    void RasterPipeline::createDescriptorLayout()
    {
        std::vector<VkDescriptorSetLayoutBinding> descriptors;

        //material uniforms
        VkDescriptorSetLayoutBinding uniformDescriptor = {};
        uniformDescriptor.binding = 0;
        uniformDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformDescriptor.descriptorCount = 1;
        uniformDescriptor.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        uniformDescriptor.pImmutableSamplers = NULL;
        descriptors.push_back(uniformDescriptor);

        //material textures
        if(pipelineType == PBR)
        {
            VkDescriptorSetLayoutBinding textureDescriptor = {};
            textureDescriptor.binding = 1;
            textureDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            textureDescriptor.descriptorCount = TEXTURE_ARRAY_SIZE;
            textureDescriptor.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            textureDescriptor.pImmutableSamplers = NULL;
            descriptors.push_back(textureDescriptor);
        }
        
        //descriptor info
        VkDescriptorSetLayoutCreateInfo descriptorInfo = {};
        descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorInfo.pNext = NULL;
        descriptorInfo.flags = 0;
        descriptorInfo.bindingCount = descriptors.size();
        descriptorInfo.pBindings = descriptors.data();

        VkResult result = vkCreateDescriptorSetLayout(devicePtr->getDevice(), &descriptorInfo, nullptr, &descriptorLayout);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor set layout");
        }
        VkDescriptorSetLayout descriptorLayouts[2] = {globalDescriptorLayout, descriptorLayout};

        //push constants
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(glm::mat4x4); //64 byte model matrix
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; //TODO RT STAGE FLAG

        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = NULL;
        layoutInfo.flags = 0;
        layoutInfo.setLayoutCount = 2;
        layoutInfo.pSetLayouts = descriptorLayouts;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        VkResult result2 = vkCreatePipelineLayout(devicePtr->getDevice(), &layoutInfo, nullptr, &pipelineLayout);
        if(result2 != VK_SUCCESS)
        {
            throw std::runtime_error("Pipeline layout creation failed");
        }
    }

    //----------RT PIPELINE DEFINITTIONS----------//

    RTPipeline::RTPipeline(Device *device, std::vector<std::string>& shaderFiles, DescriptorAllocator* descriptors)
        :Pipeline(device, NULL, shaderFiles, descriptors)
    {
    }

    RTPipeline::~RTPipeline()
    {
    }

}