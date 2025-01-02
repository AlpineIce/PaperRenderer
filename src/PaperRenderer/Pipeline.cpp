#include "PaperRenderer.h"

#include <functional>
#include "Pipeline.h"

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
        //SBT important alignments and sizes
        const uint32_t handleSize = renderer.getDevice().getRTproperties().shaderGroupHandleSize;
        const uint32_t handleAlignment = renderer.getDevice().getRTproperties().shaderGroupHandleAlignment;
        const uint32_t groupBaseAlignment = renderer.getDevice().getRTproperties().shaderGroupBaseAlignment;
        const uint32_t alignedGroupSize = renderer.getDevice().getAlignment(handleSize, handleAlignment);

        //clear old SBT data
        sbtRawData.clear();

        //shaders
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> rtShaderGroups;
        rtShaderGroups.reserve(creationInfo.missShaders.size() + creationInfo.callableShaders.size() + creationInfo.materials.size() + 1 /*raygen is 1*/);
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        shaderStages.reserve(creationInfo.missShaders.size() + creationInfo.callableShaders.size() + creationInfo.materials.size() + 1 + (creationInfo.materials.size() * 3));

        //enumerate raygen shader groups (there should only be one but whatever)
        enumerateShaders({ creationInfo.raygenShader }, shaderBindingTableData.shaderBindingTableOffsets.raygenGroupOffsets, rtShaderGroups, shaderStages, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
        shaderBindingTableData.raygenShaderBindingTable.size = groupBaseAlignment; //edge case
        shaderBindingTableData.raygenShaderBindingTable.stride = groupBaseAlignment; //edge case

        //enumerate miss shader groups
        enumerateShaders(creationInfo.missShaders, shaderBindingTableData.shaderBindingTableOffsets.missGroupOffsets, rtShaderGroups, shaderStages, VK_SHADER_STAGE_MISS_BIT_KHR);
        shaderBindingTableData.missShaderBindingTable.size = Device::getAlignment(creationInfo.missShaders.size() * alignedGroupSize, groupBaseAlignment);
        shaderBindingTableData.missShaderBindingTable.stride = handleAlignment;
        const uint32_t missOffset = 1;
        
        //enumerate callable shader groups
        enumerateShaders(creationInfo.callableShaders, shaderBindingTableData.shaderBindingTableOffsets.callableGroupOffsets, rtShaderGroups, shaderStages, VK_SHADER_STAGE_CALLABLE_BIT_KHR);
        shaderBindingTableData.callableShaderBindingTable.size = Device::getAlignment(creationInfo.callableShaders.size() * alignedGroupSize, groupBaseAlignment);
        shaderBindingTableData.callableShaderBindingTable.stride = handleAlignment;
        const uint32_t callableOffset = missOffset + creationInfo.missShaders.size();

        //enumerate hit shader groups
        std::vector<uint32_t> hitGroupCounts(creationInfo.materials.size());
        const uint32_t hitGroupsStartIndex = rtShaderGroups.size();
        for(uint32_t i = 0; i < creationInfo.materials.size(); i++)
        {
            if(!creationInfo.materials[i])
            {
                continue;
            }

            //set offset
            shaderBindingTableData.shaderBindingTableOffsets.materialShaderGroupOffsets.emplace(creationInfo.materials[i], i);
            
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
            for(auto& [shaderStage, shader] : creationInfo.materials[i]->getShaderHitGroup())
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
                hitGroupCounts[i]++;
                shaderStages.push_back(shaderStageInfo);
            }

            //set group type based on filled shaders
            if(shaderGroupInfo.intersectionShader != VK_SHADER_UNUSED_KHR)
            {
                shaderGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
            }
            else if(shaderGroupInfo.closestHitShader != VK_SHADER_UNUSED_KHR || shaderGroupInfo.anyHitShader != VK_SHADER_UNUSED_KHR)
            {
                shaderGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            }
            else
            {
                renderer.getLogger().recordLog({
                    .type = WARNING,
                    .text = "Invalid RTMaterial shader group must contain either a closest hit or intersection shader"
                });
            }

            //push shader group
            rtShaderGroups.push_back(shaderGroupInfo);
        }
        shaderBindingTableData.hitShaderBindingTable.stride = handleAlignment;

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

        VkResult result = vkCreateRayTracingPipelinesKHR(renderer.getDevice().getDevice(), VK_NULL_HANDLE, creationInfo.cache, 1, &pipelineCreateInfo, nullptr, &pipeline);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create a ray tracing pipeline");
        }


        //get general shader SBT data
        insertGroupSBTData(sbtRawData, 0, 1); //only 1 raygen, offset is always 0
        if(creationInfo.missShaders.size()) insertGroupSBTData(sbtRawData, missOffset, creationInfo.missShaders.size());
        if(creationInfo.callableShaders.size()) insertGroupSBTData(sbtRawData, callableOffset, creationInfo.callableShaders.size());

        //set material hit groups data
        const uint32_t hitShaderBindingTableLocation = sbtRawData.size();
        for(uint32_t i = 0; i < hitGroupCounts.size(); i++)
        {
            insertGroupSBTData(sbtRawData, hitGroupsStartIndex + i, hitGroupCounts[i]);
        }
        shaderBindingTableData.hitShaderBindingTable.size = sbtRawData.size() - hitShaderBindingTableLocation;

        //set SBT data
        rebuildSBTBuffer(renderer);
    }

    RTPipeline::~RTPipeline()
    {
    }

    void RTPipeline::enumerateShaders(
        const std::vector<ShaderDescription>& shaders,
        std::unordered_map<Shader const*, uint32_t>& offsets,
        std::vector<VkRayTracingShaderGroupCreateInfoKHR>& shaderGroups,
        std::vector<VkPipelineShaderStageCreateInfo>& shaderStages,
        VkShaderStageFlagBits stage
    )
    {
        //general shader groups (easy because there's 1 shader per group)
        for(uint32_t i = 0; i < shaders.size(); i++)//const ShaderDescription& shader : creationInfo.generalShaders)
        {
            if(!shaders[i].shader)
            {
                continue;
            }

            //set offset
            offsets[shaders[i].shader] = i;
            
            //setup group (1 to 1 with shader)
            VkRayTracingShaderGroupCreateInfoKHR groupInfo = {
                .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
                .pNext = NULL,
                .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
                .generalShader = (uint32_t)shaderStages.size(),
                .closestHitShader  = VK_SHADER_UNUSED_KHR,
                .anyHitShader = VK_SHADER_UNUSED_KHR,
                .intersectionShader = VK_SHADER_UNUSED_KHR,
                .pShaderGroupCaptureReplayHandle = NULL
            };
            shaderGroups.push_back(groupInfo);

            //setup stage (1 to 1 with group)
            shaderStages.push_back({
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
                .stage = shaders[i].stage,
                .module = shaders[i].shader->getModule(),
                .pName = "main", //use main() function in shaders
                .pSpecializationInfo = NULL
            });
        }
    }

    uint32_t RTPipeline::insertGroupSBTData(std::vector<char>& toInsertData, uint32_t groupOffset, uint32_t handleCount) const
    {
        const uint32_t handleSize = renderer.getDevice().getRTproperties().shaderGroupHandleSize;
        const uint32_t handleAlignment = renderer.getDevice().getRTproperties().shaderGroupHandleAlignment;
        const uint32_t groupBaseAlignment = renderer.getDevice().getRTproperties().shaderGroupBaseAlignment;
        const uint32_t alignedGroupSize = renderer.getDevice().getAlignment(handleSize, handleAlignment);

        //pad toInsertData to the group base alignment
        toInsertData.resize(Device::getAlignment(sbtRawData.size(), groupBaseAlignment));

        //initialize group data
        std::vector<char> groupData(handleAlignment * handleCount); //group data size is equal to the number of handles * the alignment of handles

        //get shader handles
        std::vector<char> groupHandles(handleSize * handleCount);
        VkResult result2 = vkGetRayTracingShaderGroupHandlesKHR(renderer.getDevice().getDevice(), pipeline, groupOffset, handleCount, groupHandles.size(), groupHandles.data());
        if(result2 != VK_SUCCESS)
        {
            renderer.getLogger().recordLog({
                .type = WARNING,
                .text = "vkGetRayTracingShaderGroupHandlesKHR failed on RT pipeline creation"
            });
            return 0;
        }

        //transfer handle data
        for(uint32_t i = 0; i < handleCount; i++)
        {
            memcpy(groupData.data() + (handleAlignment * i), groupHandles.data() + (handleSize * i), handleSize);
        }

        //insert data
        toInsertData.insert(toInsertData.end(), groupData.begin(), groupData.end());

        //return total group size
        return groupData.size();
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
        shaderBindingTableData.callableShaderBindingTable.deviceAddress = dynamicOffset;
        dynamicOffset += shaderBindingTableData.callableShaderBindingTable.size;
        shaderBindingTableData.hitShaderBindingTable.deviceAddress = dynamicOffset;
        dynamicOffset += shaderBindingTableData.hitShaderBindingTable.size;
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
        //vkCreatePipelineCache(renderer.getDevice().getDevice(), &creationInfo, nullptr, &cache); //use driver cache instead of this cache

        //log constructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "PipelineBuilder constructor finished"
        });
    }

    PipelineBuilder::~PipelineBuilder()
    {
        if(cache) vkDestroyPipelineCache(renderer.getDevice().getDevice(), cache, nullptr);

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
            info.raygenShader,
            info.missShaders,
            info.callableShaders
        };

        return std::make_unique<RTPipeline>(pipelineInfo, info.properties);
    }
}