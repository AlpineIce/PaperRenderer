#include "PaperRenderer.h"

#include <functional>

namespace PaperRenderer
{
    //----------SHADER DEFINITIONS----------//

    Shader::Shader(RenderEngine& renderer, const std::vector<uint32_t>& data)
        :renderer(renderer)
    {
        VkShaderModuleCreateInfo creationInfo;
        creationInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        creationInfo.pNext = NULL;
        creationInfo.flags = 0;
        creationInfo.codeSize = data.size();
        creationInfo.pCode = data.data();

        VkResult result = vkCreateShaderModule(renderer.getDevice().getDevice(), &creationInfo, nullptr, &program);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Creation of shader module failed.");
        }
    }

    Shader::~Shader()
    {
        vkDestroyShaderModule(renderer.getDevice().getDevice(), program, nullptr);
    }

    //----------PIPELINE DEFINITIONS---------//

    Pipeline::Pipeline(const PipelineCreationInfo& creationInfo)
        :renderer(creationInfo.renderer),
        pipelineLayout(creationInfo.pipelineLayout),
        setLayouts(creationInfo.setLayouts)
    {
    }

    Pipeline::~Pipeline()
    {
        for(auto& [setNum, set] : setLayouts)
        {
            vkDestroyDescriptorSetLayout(renderer.getDevice().getDevice(), set, nullptr);
        }
        vkDestroyPipeline(renderer.getDevice().getDevice(), pipeline, nullptr);
        vkDestroyPipelineLayout(renderer.getDevice().getDevice(), pipelineLayout, nullptr);
    }

    //----------COMPUTE PIPELINE DEFINITIONS---------//

    ComputePipeline::ComputePipeline(const ComputePipelineCreationInfo& creationInfo)
        :Pipeline(creationInfo)
    {
        VkPipelineShaderStageCreateInfo stageInfo;
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        stageInfo.pNext = NULL,
        stageInfo.flags = 0,
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT,
        stageInfo.module = creationInfo.shader->getModule(),
        stageInfo.pName = "main", //use main() function in shaders
        stageInfo.pSpecializationInfo = NULL;

        VkComputePipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = NULL;
        pipelineInfo.flags = 0;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = pipelineLayout;
        
        VkResult result = vkCreateComputePipelines(renderer.getDevice().getDevice(), creationInfo.cache, 1, &pipelineInfo, nullptr, &pipeline);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create compute pipeline");
        }
    }

    ComputePipeline::~ComputePipeline()
    {
    }

    //----------RASTER PIPELINE DEFINITIONS---------//

    RasterPipeline::RasterPipeline(const RasterPipelineCreationInfo& creationInfo, const RasterPipelineProperties& pipelineProperties)
        :Pipeline(creationInfo),
        pipelineProperties(pipelineProperties),
        drawDescriptorIndex(creationInfo.drawDescriptorIndex)
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
        vertexInputInfo.vertexBindingDescriptionCount = pipelineProperties.vertexDescriptions.size();
        vertexInputInfo.pVertexBindingDescriptions = pipelineProperties.vertexDescriptions.data();
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
            VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT,
            VK_DYNAMIC_STATE_DEPTH_COMPARE_OP 
        };
        
        VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
        dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateInfo.pNext = NULL;
        dynamicStateInfo.flags = 0;
        dynamicStateInfo.dynamicStateCount = dynamicStates.size();
        dynamicStateInfo.pDynamicStates = dynamicStates.data();
        
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        for(const auto& [shaderStage, shader] : creationInfo.shaders)
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
        
        VkResult result = vkCreateGraphicsPipelines(renderer.getDevice().getDevice(), creationInfo.cache, 1, &pipelineCreateInfo, nullptr, &pipeline);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create a graphics pipeline");
        }
    }

    RasterPipeline::~RasterPipeline()
    {
    }

    //----------RT PIPELINE DEFINITIONS----------//

    RTPipeline::RTPipeline(const RTPipelineCreationInfo& creationInfo, const RTPipelineProperties& properties)
        :Pipeline(creationInfo),
        pipelineProperties(properties)
    {
        //shaders
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> rtShaderGroups;
        rtShaderGroups.reserve(creationInfo.generalShaders.size() + creationInfo.materials.size());
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        shaderStages.reserve(creationInfo.generalShaders.size() + (creationInfo.materials.size() * 3)); //up to 3 shaders in a material shader group

        //general shaders
        uint32_t raygenCount = 0;
        uint32_t missCount = 0;
        uint32_t callableCount = 0;
        for(const ShaderDescription& shader : creationInfo.generalShaders)
        {
            if(!shader.shader)
            {
                continue;
            }
            
            switch(shader.stage)
            {
            case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
                shaderBindingTableData.shaderBindingTableOffsets.raygenShaderOffsets.emplace(shader.shader, raygenCount);
                raygenCount++;
                break;
            case VK_SHADER_STAGE_MISS_BIT_KHR:
                shaderBindingTableData.shaderBindingTableOffsets.missShaderOffsets.emplace(shader.shader, missCount);
                missCount++;
                break;
            case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
                shaderBindingTableData.shaderBindingTableOffsets.callableShaderOffsets.emplace(shader.shader, callableCount);
                callableCount++;
                break;
            default:
                throw std::runtime_error("Invalid general shader type");
            }

            VkRayTracingShaderGroupCreateInfoKHR generalShaderGroup = {};
            generalShaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            generalShaderGroup.pNext = NULL;
            generalShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            generalShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            generalShaderGroup.closestHitShader  = VK_SHADER_UNUSED_KHR;
            generalShaderGroup.generalShader = shaderStages.size();
            generalShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            generalShaderGroup.pShaderGroupCaptureReplayHandle = NULL;
            rtShaderGroups.push_back(generalShaderGroup);

            VkPipelineShaderStageCreateInfo shaderStageInfo = {};
            shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStageInfo.pNext = NULL;
            shaderStageInfo.flags = 0;
            shaderStageInfo.stage = shader.stage;
            shaderStageInfo.module = shader.shader->getModule();
            shaderStageInfo.pName = "main"; //use main() function in shaders
            shaderStageInfo.pSpecializationInfo = NULL;
            shaderStages.push_back(shaderStageInfo);
        }

        //shader groups
        uint32_t hitCount = 0;
        uint32_t hitGroupIndex = 0;
        for(RTMaterial* material : creationInfo.materials)
        {
            if(!material)
            {
                continue;
            }
            
            //shader group
            VkRayTracingShaderGroupCreateInfoKHR shaderGroupInfo = {};
            shaderGroupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shaderGroupInfo.pNext = NULL;
            shaderGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_MAX_ENUM_KHR;
            shaderGroupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroupInfo.closestHitShader  = VK_SHADER_UNUSED_KHR;
            shaderGroupInfo.generalShader = VK_SHADER_UNUSED_KHR;
            shaderGroupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
            shaderGroupInfo.pShaderGroupCaptureReplayHandle = NULL;

            //individual shaders
            for(auto& [shaderStage, shader] : material->getShaderHitGroup())
            {
                //shader "descriptor"
                VkPipelineShaderStageCreateInfo shaderStageInfo = {};
                shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shaderStageInfo.pNext = NULL;
                shaderStageInfo.flags = 0;
                shaderStageInfo.stage = shaderStage;
                shaderStageInfo.module = shader->getModule();
                shaderStageInfo.pName = "main"; //use main() function in shaders
                shaderStageInfo.pSpecializationInfo = NULL;

                //fill shader group with corresponding shader stage
                switch(shaderStage)
                {
                    //case for triangle hit group
                    case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
                        shaderGroupInfo.closestHitShader = shaderStages.size();
                        break;
                    //case for procedural hit
                    case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:
                        shaderGroupInfo.intersectionShader = shaderStages.size();
                        break;
                    case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:
                        shaderGroupInfo.anyHitShader = shaderStages.size();
                        break;
                    
                }
                hitCount++;
                shaderStages.push_back(shaderStageInfo);
            }

            //set group type based on filled shaders
            if(shaderGroupInfo.intersectionShader)
            {
                shaderGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
            }
            else if(shaderGroupInfo.closestHitShader || shaderGroupInfo.anyHitShader)
            {
                shaderGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            }
            else
            {
                throw std::runtime_error("Invalid shader group must contain either a closest hit or intersection shader");
            }

            //add offset location and push back shader group
            shaderBindingTableData.shaderBindingTableOffsets.materialShaderGroupOffsets.emplace(material, hitGroupIndex);
            rtShaderGroups.push_back(shaderGroupInfo);

            hitGroupIndex++;
        }

        VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pipelineCreateInfo.pNext = NULL;
        pipelineCreateInfo.flags = 0;
        pipelineCreateInfo.stageCount = shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();
        pipelineCreateInfo.groupCount = rtShaderGroups.size();
        pipelineCreateInfo.pGroups = rtShaderGroups.data();
        pipelineCreateInfo.maxPipelineRayRecursionDepth = pipelineProperties.maxRecursionDepth;
        pipelineCreateInfo.pLibraryInfo = NULL;
        pipelineCreateInfo.pLibraryInterface = NULL;
        pipelineCreateInfo.pDynamicState = NULL;
        pipelineCreateInfo.layout = pipelineLayout;
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex = -1;

        vkCreateDeferredOperationKHR(renderer.getDevice().getDevice(), nullptr, &deferredOperation);
        VkResult result = vkCreateRayTracingPipelinesKHR(renderer.getDevice().getDevice(), deferredOperation, creationInfo.cache, 1, &pipelineCreateInfo, nullptr, &pipeline);
        if(result != VK_SUCCESS && result != VK_OPERATION_DEFERRED_KHR && result != VK_OPERATION_NOT_DEFERRED_KHR)
        {
            throw std::runtime_error("Failed to create a ray tracing pipeline");
        }

        //wait for deferred operation 
        while(!isBuilt()) {}

        //setup shader binding table
        const uint32_t handleCount = rtShaderGroups.size();
        const uint32_t handleSize  = renderer.getDevice().getRTproperties().shaderGroupHandleSize;
        const uint32_t handleAlignment = renderer.getDevice().getRTproperties().shaderGroupBaseAlignment;
        const uint32_t alignedSize = renderer.getDevice().getAlignment(handleSize, handleAlignment);
        
        shaderBindingTableData.raygenShaderBindingTable.stride = alignedSize;
        shaderBindingTableData.raygenShaderBindingTable.size   = renderer.getDevice().getAlignment(raygenCount * alignedSize, handleAlignment);
        shaderBindingTableData.missShaderBindingTable.stride = alignedSize;
        shaderBindingTableData.missShaderBindingTable.size   = renderer.getDevice().getAlignment(missCount * alignedSize, handleAlignment);
        shaderBindingTableData.callableShaderBindingTable.stride  = alignedSize;
        shaderBindingTableData.callableShaderBindingTable.size    = renderer.getDevice().getAlignment(callableCount * alignedSize, handleAlignment);
        shaderBindingTableData.hitShaderBindingTable.stride  = alignedSize;
        shaderBindingTableData.hitShaderBindingTable.size    = renderer.getDevice().getAlignment(hitCount * alignedSize, handleAlignment);
        
        //get shader handles
        std::vector<char> handleData(handleSize * handleCount);
        VkResult result2 = vkGetRayTracingShaderGroupHandlesKHR(renderer.getDevice().getDevice(), pipeline, 0, handleCount, handleData.size(), handleData.data());
        if(result2 != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to get RT shader group handles");
        }

        VkDeviceSize currentHandleIndex = 0;
        sbtRawData.resize(alignedSize * handleCount);

        //raygen (always at 0)
        memcpy(sbtRawData.data(), handleData.data(), handleSize);
        currentHandleIndex++;

        //set miss data
        for(uint32_t i = 0; i < missCount; i++)
        {
            memcpy(sbtRawData.data() + (alignedSize * currentHandleIndex), handleData.data() + (handleSize * currentHandleIndex), handleSize);
            currentHandleIndex++;
        }

        //set callable data
        for(uint32_t i = 0; i < callableCount; i++)
        {
            memcpy(sbtRawData.data() + (alignedSize * currentHandleIndex), handleData.data() + (handleSize * currentHandleIndex), handleSize);
            currentHandleIndex++;
        }
        
        //set hit data
        for(uint32_t i = 0; i < hitCount; i++)
        {
            memcpy(sbtRawData.data() + (alignedSize * currentHandleIndex), handleData.data() + (handleSize * currentHandleIndex), handleSize);
            currentHandleIndex++;
        }
        
        //set SBT data
        rebuildSBTBuffer(renderer);
    }

    RTPipeline::~RTPipeline()
    {
    }

    void RTPipeline::rebuildSBTBuffer(RenderEngine& renderer)
    {
        //create buffers
        BufferInfo deviceBufferInfo = {};
        deviceBufferInfo.allocationFlags = 0;
        deviceBufferInfo.size = sbtRawData.size();
        deviceBufferInfo.usageFlags = VK_BUFFER_USAGE_2_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR;
        sbtBuffer = std::make_unique<Buffer>(renderer, deviceBufferInfo);

        //queue data transfer
        renderer.getStagingBuffer().queueDataTransfers(*sbtBuffer, 0, sbtRawData);

        //set SBT addresses
        VkDeviceAddress dynamicOffset = sbtBuffer->getBufferDeviceAddress();

        shaderBindingTableData.raygenShaderBindingTable.deviceAddress = dynamicOffset;
        dynamicOffset += shaderBindingTableData.raygenShaderBindingTable.size;
        shaderBindingTableData.missShaderBindingTable.deviceAddress = dynamicOffset;
        dynamicOffset += shaderBindingTableData.missShaderBindingTable.size;
        shaderBindingTableData.hitShaderBindingTable.deviceAddress = dynamicOffset;
        dynamicOffset += shaderBindingTableData.hitShaderBindingTable.size;
        shaderBindingTableData.callableShaderBindingTable.deviceAddress = dynamicOffset;
        dynamicOffset += shaderBindingTableData.callableShaderBindingTable.size;
    }

    bool RTPipeline::isBuilt()
    {
        VkResult result = vkDeferredOperationJoinKHR(renderer.getDevice().getDevice(), deferredOperation);
        if(result == VK_SUCCESS || result == VK_THREAD_DONE_KHR)
        {
            VkResult result2 = vkGetDeferredOperationResultKHR(renderer.getDevice().getDevice(), deferredOperation);
            if(result2 != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create a ray tracing pipeline");
            }
            vkDestroyDeferredOperationKHR(renderer.getDevice().getDevice(), deferredOperation, nullptr);

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

    PipelineBuilder::PipelineBuilder(RenderEngine& renderer)
        :renderer(renderer)
    {
        VkPipelineCacheCreateInfo creationInfo;
        creationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        creationInfo.pNext = NULL;
        creationInfo.flags = 0;
        creationInfo.initialDataSize = 0;
        creationInfo.pInitialData = NULL;
        vkCreatePipelineCache(renderer.getDevice().getDevice(), &creationInfo, nullptr, &cache);

        //log constructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "PipelineBuilder constructor finished"
        });
    }

    PipelineBuilder::~PipelineBuilder()
    {
        vkDestroyPipelineCache(renderer.getDevice().getDevice(), cache, nullptr);

        //log destructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "PipelineBuilder destructor finished"
        });
    }

    std::unordered_map<uint32_t, VkDescriptorSetLayout> PipelineBuilder::createDescriptorLayouts(const std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>>& descriptorSets) const
    {
        std::unordered_map<uint32_t, VkDescriptorSetLayout> setLayouts;
        for(const auto& [setNum, set] : descriptorSets)
        {
            std::vector<VkDescriptorSetLayoutBinding> vBindings;
            vBindings.reserve(set.size());
            for(const VkDescriptorSetLayoutBinding& binding : set)
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
            VkResult result = vkCreateDescriptorSetLayout(renderer.getDevice().getDevice(), &descriptorLayoutInfo, nullptr, &setLayout);
            if(result != VK_SUCCESS) throw std::runtime_error("Failed to create descriptor set layout");

            setLayouts[setNum] = setLayout;
        }

        return setLayouts;
    }

    VkPipelineLayout PipelineBuilder::createPipelineLayout(const std::unordered_map<uint32_t, VkDescriptorSetLayout>& setLayouts, const std::vector<VkPushConstantRange>& pcRanges) const
    {
        std::vector<VkDescriptorSetLayout> vSetLayouts(setLayouts.size());
        for(const auto& [setNum, set] : setLayouts)
        {
            vSetLayouts[setNum] = set;
        }

        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = NULL;
        layoutInfo.flags = 0;
        layoutInfo.setLayoutCount = vSetLayouts.size();
        layoutInfo.pSetLayouts = vSetLayouts.data();
        layoutInfo.pushConstantRangeCount = pcRanges.size();
        layoutInfo.pPushConstantRanges = pcRanges.data();

        VkPipelineLayout returnLayout;
        VkResult result = vkCreatePipelineLayout(renderer.getDevice().getDevice(), &layoutInfo, NULL, &returnLayout);
        if(result != VK_SUCCESS) throw std::runtime_error("Pipeline layout creation failed");

        return returnLayout;
    }

    std::unique_ptr<ComputePipeline> PipelineBuilder::buildComputePipeline(const ComputePipelineBuildInfo& info) const
    {
        //Timer
        Timer timer(renderer, "Build Compute Pipeline", IRREGULAR);

        ComputePipelineCreationInfo pipelineInfo = {{
                .renderer = renderer,
                .cache = cache,
                .setLayouts = createDescriptorLayouts(info.descriptors),
                .pcRanges = info.pcRanges,
                .pipelineLayout = createPipelineLayout(pipelineInfo.setLayouts, info.pcRanges),
            },
            std::make_shared<Shader>(renderer, info.shaderInfo.data)
        };

        return std::make_unique<ComputePipeline>(pipelineInfo);
    }

    std::unique_ptr<RasterPipeline> PipelineBuilder::buildRasterPipeline(const RasterPipelineBuildInfo& info) const
    {
        //Timer
        Timer timer(renderer, "Build Raster Pipeline", IRREGULAR);

        //set shaders
        std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<PaperRenderer::Shader>> shaders;
        for(const ShaderPair& pair : info.shaderInfo)
        {
            shaders[pair.stage] = std::make_shared<Shader>(renderer, pair.data);
        }

        //pipeline info
        const RasterPipelineCreationInfo pipelineInfo = { {
                .renderer = renderer,
                .cache = cache,
                .setLayouts = createDescriptorLayouts(info.descriptorSets),
                .pcRanges = info.pcRanges,
                .pipelineLayout = createPipelineLayout(pipelineInfo.setLayouts, info.pcRanges),
            },
            shaders,
            info.drawDescriptorIndex
        };
        
        return std::make_unique<RasterPipeline>(pipelineInfo, info.properties);
    }

    std::unique_ptr<RTPipeline> PipelineBuilder::buildRTPipeline(const RTPipelineBuildInfo& info) const
    {
        //Timer
        Timer timer(renderer, "Build RT Pipeline", IRREGULAR);

        RTPipelineCreationInfo pipelineInfo = { {
                .renderer = renderer,
                .cache = cache,
                .setLayouts = createDescriptorLayouts(info.descriptorSets),
                .pcRanges = info.pcRanges,
                .pipelineLayout = createPipelineLayout(pipelineInfo.setLayouts, info.pcRanges)
            },
            info.materials,
            info.generalShaders,
        };

        return std::make_unique<RTPipeline>(pipelineInfo, info.properties);
    }
}