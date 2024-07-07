#include "Pipeline.h"
#include "Descriptor.h"
#include "Swapchain.h"

#include <fstream>

namespace PaperRenderer
{
    PipelineRendererInfo PipelineBuilder::rendererInfo;

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

    ComputePipeline::ComputePipeline(const PipelineCreationInfo& creationInfo)
        :Pipeline(creationInfo)
    {
        VkPipelineShaderStageCreateInfo stageInfo;
        for(const auto& [shaderStage, shader] : shaders)
        {
            if(shaderStage == VK_SHADER_STAGE_COMPUTE_BIT)
            {
                stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                stageInfo.pNext = NULL,
                stageInfo.flags = 0,
                stageInfo.stage = shaderStage,
                stageInfo.module = shader->getModule(),
                stageInfo.pName = "main", //use main() function in shaders
                stageInfo.pSpecializationInfo = NULL;
            }
        }

        VkComputePipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = NULL;
        pipelineInfo.flags = 0;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = pipelineLayout;
        
        VkResult result = vkCreateComputePipelines(devicePtr->getDevice(), creationInfo.cache, 1, &pipelineInfo, nullptr, &pipeline);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create compute pipeline");
        }
    }

    ComputePipeline::~ComputePipeline()
    {
    }

    //----------PIPELINE DEFINITIONS---------//

    Pipeline::Pipeline(const PipelineCreationInfo& creationInfo)
        :devicePtr(creationInfo.device),
        descriptorsPtr(creationInfo.descriptors),
        shaders(creationInfo.shaders),
        setLayouts(creationInfo.setLayouts),
        pipelineLayout(creationInfo.pipelineLayout)
    {
    }

    Pipeline::~Pipeline()
    {
        for(auto& [setNum, set] : setLayouts)
        {
            vkDestroyDescriptorSetLayout(devicePtr->getDevice(), set, nullptr);
        }
        vkDestroyPipeline(devicePtr->getDevice(), pipeline, nullptr);
        vkDestroyPipelineLayout(devicePtr->getDevice(), pipelineLayout, nullptr);
    }

    //----------RASTER PIPELINE DEFINITIONS---------//

    RasterPipeline::RasterPipeline(const PipelineCreationInfo& creationInfo, const RasterPipelineProperties& pipelineProperties, Swapchain* swapchain)
        :Pipeline(creationInfo),
        pipelineProperties(pipelineProperties)
    {
        //pipeline info from here on
        VkPipelineRenderingCreateInfo renderingInfo = {};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.pNext = NULL;
        renderingInfo.viewMask = 0;
        renderingInfo.colorAttachmentCount = pipelineProperties.colorAttachmentFormats.size();
        renderingInfo.pColorAttachmentFormats = pipelineProperties.colorAttachmentFormats.data();
        renderingInfo.depthAttachmentFormat = pipelineProperties.depthAttachmentFormat;
        renderingInfo.stencilAttachmentFormat = pipelineProperties.stencilAttachmentFormat;

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.pNext = NULL;
        vertexInputInfo.flags = 0;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &pipelineProperties.vertexDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = pipelineProperties.vertexAttributes.size();
        vertexInputInfo.pVertexAttributeDescriptions = pipelineProperties.vertexAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
        inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyInfo.pNext = NULL;
        inputAssemblyInfo.flags = 0;
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportInfo = {};
        viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportInfo.pNext = NULL;
        viewportInfo.flags = 0;
        viewportInfo.viewportCount = 0;
        viewportInfo.scissorCount = 0;

        VkPipelineMultisampleStateCreateInfo MSAAInfo = {};
        MSAAInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        MSAAInfo.pNext = NULL;
        MSAAInfo.flags = 0;
        MSAAInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        MSAAInfo.sampleShadingEnable = VK_TRUE;
        MSAAInfo.minSampleShading = 1.0f;
        MSAAInfo.pSampleMask = NULL;
        MSAAInfo.alphaToCoverageEnable = VK_FALSE;
        MSAAInfo.alphaToOneEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorInfo = {};
        colorInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorInfo.pNext = NULL;
        colorInfo.flags = 0;
        colorInfo.logicOpEnable = VK_FALSE;
        colorInfo.logicOp = VK_LOGIC_OP_COPY;
        colorInfo.attachmentCount = pipelineProperties.colorAttachments.size();
        colorInfo.pAttachments = pipelineProperties.colorAttachments.data();
        colorInfo.blendConstants[0] = 0.0f;
        colorInfo.blendConstants[1] = 0.0f;
        colorInfo.blendConstants[2] = 0.0f;
        colorInfo.blendConstants[3] = 0.0f;

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
            VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
            VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT
        };
        
        VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
        dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateInfo.pNext = NULL;
        dynamicStateInfo.flags = 0;
        dynamicStateInfo.dynamicStateCount = dynamicStates.size();
        dynamicStateInfo.pDynamicStates = dynamicStates.data();
        
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        for(const auto& [shaderStage, shader] : shaders)
        {
            VkPipelineShaderStageCreateInfo stageInfo = {};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.pNext = NULL;
            stageInfo.flags = 0;
            stageInfo.stage = shaderStage;
            stageInfo.module = shader->getModule();
            stageInfo.pName = "main"; //use main() function in shaders
            stageInfo.pSpecializationInfo = NULL;
            
            shaderStages.push_back(stageInfo);
        }

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.pNext = &renderingInfo;
        pipelineCreateInfo.flags = 0;
        pipelineCreateInfo.stageCount = shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();
        pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyInfo;
        pipelineCreateInfo.pTessellationState = &pipelineProperties.tessellationInfo;
        pipelineCreateInfo.pViewportState = &viewportInfo;
        pipelineCreateInfo.pRasterizationState = &pipelineProperties.rasterInfo;
        pipelineCreateInfo.pMultisampleState = &MSAAInfo;
        pipelineCreateInfo.pDepthStencilState = &pipelineProperties.depthStencilInfo;
        pipelineCreateInfo.pColorBlendState = &colorInfo;
        pipelineCreateInfo.pDynamicState = &dynamicStateInfo;
        pipelineCreateInfo.layout = pipelineLayout;
        pipelineCreateInfo.renderPass = NULL;
        pipelineCreateInfo.subpass = 0;
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex = -1;
        
        VkResult result = vkCreateGraphicsPipelines(devicePtr->getDevice(), creationInfo.cache, 1, &pipelineCreateInfo, nullptr, &pipeline);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create a graphics pipeline");
        }
    }

    RasterPipeline::~RasterPipeline()
    {
    }

    //----------RT PIPELINE DEFINITTIONS----------//

    RTPipeline::RTPipeline(const PipelineCreationInfo& creationInfo, const RTPipelineProperties& pipelineProperties)
        :Pipeline(creationInfo),
        pipelineProperties(pipelineProperties)
    {
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

        std::vector<VkRayTracingShaderGroupCreateInfoKHR> rtShaderGroups;
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        uint32_t shaderIndex = 0;
        for(auto& [shaderStage, shader] : creationInfo.shaders)
        {
            VkPipelineShaderStageCreateInfo stageInfo = {};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.pNext = NULL;
            stageInfo.flags = 0;
            stageInfo.stage = shaderStage;
            stageInfo.module = shader->getModule();
            stageInfo.pName = "main"; //use main() function in shaders
            stageInfo.pSpecializationInfo = NULL;
            
            shaderStages.push_back(stageInfo);

            VkRayTracingShaderGroupCreateInfoKHR shaderGroup = {};
            shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shaderGroup.pNext = NULL;
            shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.closestHitShader  = VK_SHADER_UNUSED_KHR;
            shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            shaderGroup.pShaderGroupCaptureReplayHandle = NULL;

            switch(shaderStage)
            {
                case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:
                    shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                    shaderGroup.anyHitShader = shaderIndex;
                    break;
                case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
                    shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
                    shaderGroup.closestHitShader = shaderIndex;
                    break;
                case VK_SHADER_STAGE_RAYGEN_BIT_KHR || VK_SHADER_STAGE_MISS_BIT_KHR:
                    shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                    shaderGroup.generalShader = shaderIndex;
                    break;
                case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:
                    shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                    shaderGroup.intersectionShader = shaderIndex;
                    break;
            }
            rtShaderGroups.push_back(shaderGroup);
            shaderIndex++;
        }

        VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pipelineCreateInfo.pNext = NULL;
        pipelineCreateInfo.flags = 0;
        pipelineCreateInfo.stageCount = shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();
        pipelineCreateInfo.groupCount = rtShaderGroups.size();
        pipelineCreateInfo.pGroups = rtShaderGroups.data();
        pipelineCreateInfo.maxPipelineRayRecursionDepth = pipelineProperties.MAX_RT_RECURSION_DEPTH;
        pipelineCreateInfo.pLibraryInfo = NULL;
        pipelineCreateInfo.pLibraryInterface = NULL;
        pipelineCreateInfo.pDynamicState = &dynamicStateInfo;
        pipelineCreateInfo.layout = pipelineLayout;
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex = -1;

        vkCreateDeferredOperationKHR(devicePtr->getDevice(), nullptr, &deferredOperation);
        VkResult result = vkCreateRayTracingPipelinesKHR(devicePtr->getDevice(), deferredOperation, creationInfo.cache, 1, &pipelineCreateInfo, nullptr, &pipeline);
        if(result != VK_SUCCESS || result != VK_OPERATION_DEFERRED_KHR || result != VK_OPERATION_NOT_DEFERRED_KHR)
        {
            throw std::runtime_error("Failed to create a ray tracing pipeline");
        }
    }

    RTPipeline::~RTPipeline()
    {
    }

    bool RTPipeline::isBuilt()
    {
        VkResult result = vkDeferredOperationJoinKHR(devicePtr->getDevice(), deferredOperation);
        if(result == VK_SUCCESS || result == VK_THREAD_DONE_KHR)
        {
            VkResult result2 = vkGetDeferredOperationResultKHR(devicePtr->getDevice(), deferredOperation);
            if(result2 != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create a ray tracing pipeline");
            }
            vkDestroyDeferredOperationKHR(devicePtr->getDevice(), deferredOperation, nullptr);

            return true;
        }
        else if(result == VK_THREAD_IDLE_KHR)
        {
            return false;
        }
        else //VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY
        {
            throw std::runtime_error("Failed to create a ray tracing pipeline (likely out of vram)");
        }
    }

    //----------PIPELINE BUILDER DEFINITIONS----------//

    PipelineBuilder::PipelineBuilder(Device *device, DescriptorAllocator *descriptors, Swapchain *swapchain)
        :devicePtr(device),
        descriptorsPtr(descriptors),
        swapchainPtr(swapchain)
    {
        VkPipelineCacheCreateInfo creationInfo;
        creationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        creationInfo.pNext = NULL;
        creationInfo.flags = 0;
        creationInfo.initialDataSize = 0;
        creationInfo.pInitialData = NULL;
        vkCreatePipelineCache(device->getDevice(), &creationInfo, nullptr, &cache);
        
        rendererInfo.devicePtr = devicePtr;
        rendererInfo.descriptorsPtr = descriptorsPtr;
        rendererInfo.pipelineBuilderPtr = this;
    }

    PipelineBuilder::~PipelineBuilder()
    {
        vkDestroyPipelineCache(devicePtr->getDevice(), cache, nullptr);
    }

    std::shared_ptr<Shader> PipelineBuilder::createShader(const ShaderPair& pair) const
    {
        std::string shaderFile = pair.directory;

        return std::make_shared<Shader>(devicePtr, shaderFile);
    }

    std::unordered_map<uint32_t, VkDescriptorSetLayout> PipelineBuilder::createDescriptorLayouts(const std::unordered_map<uint32_t, DescriptorSet> &descriptorSets) const
    {
        std::unordered_map<uint32_t, VkDescriptorSetLayout> setLayouts;
        for(const auto& [setNum, set] : descriptorSets)
        {
            std::vector<VkDescriptorSetLayoutBinding> vBindings;
            for(const auto& [bindingNum, binding] : set.descriptorBindings)
            {
                vBindings.push_back(binding);
            }

            VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo = {};
            descriptorLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorLayoutInfo.pNext = NULL;
            descriptorLayoutInfo.flags = 0;
            descriptorLayoutInfo.bindingCount = vBindings.size();
            descriptorLayoutInfo.pBindings = vBindings.data();
            
            VkDescriptorSetLayout setLayout;
            VkResult result = vkCreateDescriptorSetLayout(devicePtr->getDevice(), &descriptorLayoutInfo, nullptr, &setLayout);
            if(result != VK_SUCCESS) throw std::runtime_error("Failed to create descriptor set layout");

            setLayouts[setNum] = setLayout;
        }

        return setLayouts;
    }

    VkPipelineLayout PipelineBuilder::createPipelineLayout(const std::unordered_map<uint32_t, VkDescriptorSetLayout>& setLayouts) const
    {
        std::vector<VkDescriptorSetLayout> vSetLayouts;
        for(const auto& [setNum, set] : setLayouts)
        {
            vSetLayouts.push_back(set);
        }

        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = NULL;
        layoutInfo.flags = 0;
        layoutInfo.setLayoutCount = vSetLayouts.size();
        layoutInfo.pSetLayouts = vSetLayouts.data();
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = NULL;

        VkPipelineLayout returnLayout;
        VkResult result = vkCreatePipelineLayout(devicePtr->getDevice(), &layoutInfo, nullptr, &returnLayout);
        if(result != VK_SUCCESS) throw std::runtime_error("Pipeline layout creation failed");

        return returnLayout;
    }

    PipelineCreationInfo PipelineBuilder::initPipelineInfo(PipelineBuildInfo info) const
    {
        PipelineCreationInfo pipelineInfo = {};
        pipelineInfo.device = devicePtr;
        pipelineInfo.descriptors = descriptorsPtr;
        pipelineInfo.cache = cache;

        std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>> shaders;
        for(const ShaderPair& pair : *info.shaderInfo)
        {
            shaders[pair.stage] = createShader(pair);
        }
        pipelineInfo.shaders = shaders;
        pipelineInfo.setLayouts = createDescriptorLayouts(*info.descriptors);

        VkPipelineLayout pipelineLayout = createPipelineLayout(pipelineInfo.setLayouts);
        pipelineInfo.pipelineLayout = pipelineLayout;
    
        return pipelineInfo;
    }

    std::unique_ptr<ComputePipeline> PipelineBuilder::buildComputePipeline(const ComputePipelineBuildInfo& info) const
    {
        //sketchy hack to avoid making more functions
        std::vector<ShaderPair> shaders;
        shaders.push_back(*info.shaderInfo);

        PipelineBuildInfo buildInfo;
        buildInfo.shaderInfo = &shaders;
        buildInfo.descriptors = info.descriptors;

        return std::make_unique<ComputePipeline>(initPipelineInfo(buildInfo));
    }

    std::unique_ptr<RasterPipeline> PipelineBuilder::buildRasterPipeline(const PipelineBuildInfo& info, const RasterPipelineProperties& pipelineProperties) const
    {
        return std::make_unique<RasterPipeline>(initPipelineInfo(info), pipelineProperties, swapchainPtr);
    }

    std::unique_ptr<RTPipeline> PipelineBuilder::buildRTPipeline(const PipelineBuildInfo& info, const RTPipelineProperties& pipelineProperties) const
    {
        return std::make_unique<RTPipeline>(initPipelineInfo(info), pipelineProperties);
    }
}