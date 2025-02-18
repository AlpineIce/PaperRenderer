#include "PaperRenderer.h"
#include "Pipeline.h"

namespace PaperRenderer
{
    //----------PIPELINE DEFINITIONS---------//

    Pipeline::Pipeline(RenderEngine& renderer, const std::unordered_map<uint32_t, VkDescriptorSetLayout>& setLayouts, const std::vector<VkPushConstantRange>& pcRanges)
        :renderer(renderer),
        pipelineLayout(createPipelineLayout(renderer, setLayouts, pcRanges))
    {
    }

    Pipeline::~Pipeline()
    {
        //destroy pipeline and layout
        vkDestroyPipeline(renderer.getDevice().getDevice(), pipeline, nullptr);
        vkDestroyPipelineLayout(renderer.getDevice().getDevice(), pipelineLayout, nullptr);
    }

    VkPipelineLayout Pipeline::createPipelineLayout(RenderEngine& renderer, const std::unordered_map<uint32_t, VkDescriptorSetLayout> &setLayouts, const std::vector<VkPushConstantRange> &pcRanges) const noexcept
    {
        std::vector<VkDescriptorSetLayout> vSetLayouts(setLayouts.size());
        for(const auto& [setNum, set] : setLayouts)
        {
            vSetLayouts[setNum] = set;
        }

        const VkPipelineLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .setLayoutCount = (uint32_t)vSetLayouts.size(),
            .pSetLayouts = vSetLayouts.data(),
            .pushConstantRangeCount = (uint32_t)pcRanges.size(),
            .pPushConstantRanges = pcRanges.data()
        };

        VkPipelineLayout returnLayout;
        VkResult result = vkCreatePipelineLayout(renderer.getDevice().getDevice(), &layoutInfo, NULL, &returnLayout);
        if(result != VK_SUCCESS)
        {
            renderer.getLogger().recordLog({
                .type = CRITICAL_ERROR,
                .text = "Pipeline layout creation failed"
            });
        }

        return returnLayout;
    }

    //----------COMPUTE PIPELINE DEFINITIONS---------//

    ComputePipeline::ComputePipeline(RenderEngine& renderer, const ComputePipelineInfo& creationInfo)
        :Pipeline(renderer, creationInfo.descriptorSets, creationInfo.pcRanges)
    {
        const VkShaderModuleCreateInfo shaderModuleInfo = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .codeSize = creationInfo.shaderData.size(),
            .pCode = creationInfo.shaderData.data()
        };

        const VkComputePipelineCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .stage = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = &shaderModuleInfo,
                .flags = 0,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = VK_NULL_HANDLE,
                .pName = "main", //use main() function in shaders
                .pSpecializationInfo = NULL
            },
            .layout = pipelineLayout
        };
        
        VkResult result = vkCreateComputePipelines(renderer.getDevice().getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        if(result != VK_SUCCESS)
        {
            renderer.getLogger().recordLog({
                .type = CRITICAL_ERROR,
                .text = "Failed to create compute pipeline"
            });
        }
    }

    ComputePipeline::~ComputePipeline()
    {
    }

    //----------RASTER PIPELINE DEFINITIONS---------//

    RasterPipeline::RasterPipeline(RenderEngine& renderer, const RasterPipelineInfo& creationInfo)
        :Pipeline(renderer, creationInfo.descriptorSets, creationInfo.pcRanges),
        pipelineProperties(creationInfo.properties)
    {
        //pipeline info from here on
        const VkPipelineRenderingCreateInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .pNext = NULL,
            .viewMask = 0,
            .colorAttachmentCount = (uint32_t)pipelineProperties.colorAttachmentFormats.size(),
            .pColorAttachmentFormats = pipelineProperties.colorAttachmentFormats.data(),
            .depthAttachmentFormat = pipelineProperties.depthAttachmentFormat,
            .stencilAttachmentFormat = pipelineProperties.stencilAttachmentFormat
        };

        const VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .vertexBindingDescriptionCount = (uint32_t)pipelineProperties.vertexDescriptions.size(),
            .pVertexBindingDescriptions = pipelineProperties.vertexDescriptions.data(),
            .vertexAttributeDescriptionCount = (uint32_t)pipelineProperties.vertexAttributes.size(),
            .pVertexAttributeDescriptions = pipelineProperties.vertexAttributes.data()
        };

        const VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE
        };

        const VkPipelineViewportStateCreateInfo viewportInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .viewportCount = 0,
            .scissorCount = 0
        };

        const VkPipelineMultisampleStateCreateInfo MSAAInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_TRUE,
            .minSampleShading = 1.0f,
            .pSampleMask = NULL,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE
        };

        const VkPipelineColorBlendStateCreateInfo colorInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = (uint32_t)pipelineProperties.colorAttachments.size(),
            .pAttachments = pipelineProperties.colorAttachments.data(),
            .blendConstants = { 0.0f, 0.0f, 0.0f, 0.0f }
        };

        const std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
            VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
            VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT,
            VK_DYNAMIC_STATE_DEPTH_COMPARE_OP
        };
        
        const VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .dynamicStateCount = (uint32_t)dynamicStates.size(),
            .pDynamicStates = dynamicStates.data()
        };
        
        std::vector<VkShaderModuleCreateInfo> shaderModuleInfos;
        shaderModuleInfos.reserve(creationInfo.shaders.size());
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        shaderStages.reserve(creationInfo.shaders.size());
        for(const auto& [shaderStage, shaderData] : creationInfo.shaders)
        {
            shaderModuleInfos.push_back({
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
                .codeSize = shaderData.size(),
                .pCode = shaderData.data()
            });

            shaderStages.push_back({
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = &(*shaderModuleInfos.rbegin()),
                .flags = 0,
                .stage = shaderStage,
                .module = VK_NULL_HANDLE,
                .pName = "main",
                .pSpecializationInfo = NULL
            });
        }

        const VkGraphicsPipelineCreateInfo pipelineCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &renderingInfo,
            .flags = 0,
            .stageCount = (uint32_t)shaderStages.size(),
            .pStages = shaderStages.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssemblyInfo,
            .pTessellationState = &pipelineProperties.tessellationInfo,
            .pViewportState = &viewportInfo,
            .pRasterizationState = &pipelineProperties.rasterInfo,
            .pMultisampleState = &MSAAInfo,
            .pDepthStencilState = &pipelineProperties.depthStencilInfo,
            .pColorBlendState = &colorInfo,
            .pDynamicState = &dynamicStateInfo,
            .layout = pipelineLayout,
            .renderPass = NULL, //use dynamic rendering
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1
        };
        
        VkResult result = vkCreateGraphicsPipelines(renderer.getDevice().getDevice(), VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline);
        if(result != VK_SUCCESS)
        {
            renderer.getLogger().recordLog({
                .type = CRITICAL_ERROR,
                .text = "Failed to create a graphics pipeline"
            });
        }
    }

    RasterPipeline::~RasterPipeline()
    {
    }

    //----------RT PIPELINE DEFINITIONS----------//

    RTPipeline::RTPipeline(RenderEngine& renderer, const RTPipelineInfo& creationInfo)
        :Pipeline(renderer, creationInfo.descriptorSets, creationInfo.pcRanges),
        pipelineProperties(creationInfo.properties)
    {
        //SBT important alignments and sizes
        const uint32_t handleSize = renderer.getDevice().getGPUFeaturesAndProperties().rtPipelineProperties.shaderGroupHandleSize;
        const uint32_t handleAlignment = renderer.getDevice().getGPUFeaturesAndProperties().rtPipelineProperties.shaderGroupHandleAlignment;
        const uint32_t groupBaseAlignment = renderer.getDevice().getGPUFeaturesAndProperties().rtPipelineProperties.shaderGroupBaseAlignment;
        const uint32_t alignedGroupSize = renderer.getDevice().getAlignment(handleSize, handleAlignment);

        //clear old SBT data
        sbtRawData.clear();

        //shaders
        const VkDeviceSize shaderGroupCount = creationInfo.missShaders.size() + creationInfo.callableShaders.size() + creationInfo.materials.size() + 1;
        const VkDeviceSize maxNumShaders = creationInfo.missShaders.size() + creationInfo.callableShaders.size() + creationInfo.materials.size() + 1 + (creationInfo.materials.size() * 3);
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> rtShaderGroups;
        rtShaderGroups.reserve(shaderGroupCount);
        std::vector<VkShaderModuleCreateInfo> shaderModuleInfos;
        shaderModuleInfos.reserve(maxNumShaders); //reserve worst case
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        shaderStages.reserve(maxNumShaders); //reserve worst case

        //SBT offsets
        std::unordered_map<std::vector<uint32_t> const*, uint32_t> raygenGroupOffsets = {};
        std::unordered_map<std::vector<uint32_t> const*, uint32_t> missGroupOffsets = {};
        std::unordered_map<std::vector<uint32_t> const*, uint32_t> callableGroupOffsets = {};

        //enumerate raygen shader groups (there should only be one but whatever)
        enumerateShaders({ creationInfo.raygenShader }, raygenGroupOffsets, rtShaderGroups, shaderModuleInfos, shaderStages, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
        shaderBindingTableData.raygenShaderBindingTable.size = groupBaseAlignment; //edge case
        shaderBindingTableData.raygenShaderBindingTable.stride = groupBaseAlignment; //edge case

        //enumerate miss shader groups
        enumerateShaders(creationInfo.missShaders, missGroupOffsets, rtShaderGroups, shaderModuleInfos, shaderStages, VK_SHADER_STAGE_MISS_BIT_KHR);
        shaderBindingTableData.missShaderBindingTable.size = Device::getAlignment(creationInfo.missShaders.size() * alignedGroupSize, groupBaseAlignment);
        shaderBindingTableData.missShaderBindingTable.stride = handleAlignment;
        const uint32_t missOffset = 1;
        
        //enumerate callable shader groups
        enumerateShaders(creationInfo.callableShaders, callableGroupOffsets, rtShaderGroups, shaderModuleInfos, shaderStages, VK_SHADER_STAGE_CALLABLE_BIT_KHR);
        shaderBindingTableData.callableShaderBindingTable.size = Device::getAlignment(creationInfo.callableShaders.size() * alignedGroupSize, groupBaseAlignment);
        shaderBindingTableData.callableShaderBindingTable.stride = handleAlignment;
        const uint32_t callableOffset = missOffset + creationInfo.missShaders.size();

        //enumerate hit shader groups
        const uint32_t hitGroupsStartIndex = rtShaderGroups.size();
        std::vector<uint32_t> hitGroupCounts(creationInfo.materials.size());
        for(uint32_t i = 0; i < creationInfo.materials.size(); i++)
        {
            if(!creationInfo.materials[i])
            {
                continue;
            }

            //set offset
            shaderBindingTableData.materialShaderGroupOffsets.emplace(creationInfo.materials[i], i);

            //helper function
            struct MaterialShader
            {
                std::vector<uint32_t> const* shaderData;
                VkShaderStageFlagBits stage;
            };

            const auto getMaterialShaderGroup = [&](const std::vector<MaterialShader>& materialShaders)
            {
                //hit group
                VkRayTracingShaderGroupCreateInfoKHR shaderGroupInfo = {
                    .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
                    .pNext = NULL,
                    .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_MAX_ENUM_KHR,
                    .generalShader = VK_SHADER_UNUSED_KHR,
                    .closestHitShader  = VK_SHADER_UNUSED_KHR,
                    .anyHitShader = VK_SHADER_UNUSED_KHR,
                    .intersectionShader = VK_SHADER_UNUSED_KHR,
                    .pShaderGroupCaptureReplayHandle = NULL
                };

                //enumerate shaders
                for(const MaterialShader& shader : materialShaders)
                {
                    //skip if shader is empty
                    if(!shader.shaderData->size())
                    {
                        continue;
                    }

                    //fill shader group with corresponding shader stage
                    switch(shader.stage)
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

                    //shader module for pNext
                    shaderModuleInfos.push_back({
                        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                        .pNext = NULL,
                        .flags = 0,
                        .codeSize = shader.shaderData->size(),
                        .pCode = shader.shaderData->data()
                    });

                    //shader stage
                    shaderStages.push_back({
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .pNext = &(*shaderModuleInfos.rbegin()),
                        .flags = 0,
                        .stage = shader.stage,
                        .module = VK_NULL_HANDLE,
                        .pName = "main",
                        .pSpecializationInfo = NULL
                    });
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

                return shaderGroupInfo;
            };

            //push shader group
            rtShaderGroups.push_back(getMaterialShaderGroup({
                { &creationInfo.materials[i]->getShaderHitGroup().chitShaderData, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
                { &creationInfo.materials[i]->getShaderHitGroup().ahitShaderData, VK_SHADER_STAGE_ANY_HIT_BIT_KHR },
                { &creationInfo.materials[i]->getShaderHitGroup().intShaderData, VK_SHADER_STAGE_INTERSECTION_BIT_KHR }
            }));
        }
        shaderBindingTableData.hitShaderBindingTable.stride = handleAlignment;

        const VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
            .pNext = NULL,
            .flags = 0,
            .stageCount = (uint32_t)shaderStages.size(),
            .pStages = shaderStages.data(),
            .groupCount = (uint32_t)rtShaderGroups.size(),
            .pGroups = rtShaderGroups.data(),
            .maxPipelineRayRecursionDepth = pipelineProperties.maxRecursionDepth,
            .pLibraryInfo = NULL,
            .pLibraryInterface = NULL,
            .pDynamicState = NULL,
            .layout = pipelineLayout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1
        };

        VkResult result = vkCreateRayTracingPipelinesKHR(renderer.getDevice().getDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline);
        if(result != VK_SUCCESS)
        {
            renderer.getLogger().recordLog({
                .type = CRITICAL_ERROR,
                .text = "Failed to create a ray tracing pipeline"
            });
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
        const std::vector<std::vector<uint32_t> const*>& shaders,
        std::unordered_map<std::vector<uint32_t> const*, uint32_t>& offsets,
        std::vector<VkRayTracingShaderGroupCreateInfoKHR>& shaderGroups,
        std::vector<VkShaderModuleCreateInfo>& shaderModuleInfos,
        std::vector<VkPipelineShaderStageCreateInfo>& shaderStages,
        VkShaderStageFlagBits stage
    )
    {
        //general shader groups (easy because there's 1 shader per group)
        for(uint32_t i = 0; i < shaders.size(); i++)
        {
            //continue if shader data is empty
            if(!shaders[i]->size())
            {
                continue;
            }

            //set offset
            offsets[shaders[i]] = i;
            
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

            //shader module for pNext
            shaderModuleInfos.push_back({
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
                .codeSize = shaders[i]->size(),
                .pCode = shaders[i]->data()
            });

            //setup stage (1 to 1 with group)
            shaderStages.push_back({
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = &(*shaderModuleInfos.rbegin()),
                .flags = 0,
                .stage = stage,
                .module = VK_NULL_HANDLE,
                .pName = "main", //use main() function in shaders
                .pSpecializationInfo = NULL
            });
        }
    }

    uint32_t RTPipeline::insertGroupSBTData(std::vector<char>& toInsertData, uint32_t groupOffset, uint32_t handleCount) const
    {
        const uint32_t handleSize = renderer.getDevice().getGPUFeaturesAndProperties().rtPipelineProperties.shaderGroupHandleSize;
        const uint32_t handleAlignment = renderer.getDevice().getGPUFeaturesAndProperties().rtPipelineProperties.shaderGroupHandleAlignment;
        const uint32_t groupBaseAlignment = renderer.getDevice().getGPUFeaturesAndProperties().rtPipelineProperties.shaderGroupBaseAlignment;
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
        const BufferInfo sbtBufferInfo = {
            .size = sbtRawData.size(),
            .usageFlags = VK_BUFFER_USAGE_2_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR,
            .allocationFlags = 0
        };
        sbtBuffer = std::make_unique<Buffer>(renderer, sbtBufferInfo);

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
}