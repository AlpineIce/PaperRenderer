#include "PaperRenderer.h"

#include <fstream>

namespace PaperRenderer
{
    //----------SHADER DEFINITIONS----------//

    Shader::Shader(Device *device, std::string location)
        :devicePtr(device)
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
            throw std::runtime_error("Couldn't open pipeline shader file " + location);
        }

        return std::vector<uint32_t>();
    }

    //----------PIPELINE DEFINITIONS---------//

    Pipeline::Pipeline(const PipelineCreationInfo& creationInfo)
        :rendererPtr(creationInfo.renderer),
        pipelineLayout(creationInfo.pipelineLayout),
        setLayouts(creationInfo.setLayouts)
    {
    }

    Pipeline::~Pipeline()
    {
        for(auto& [setNum, set] : setLayouts)
        {
            vkDestroyDescriptorSetLayout(rendererPtr->getDevice()->getDevice(), set, nullptr);
        }
        vkDestroyPipeline(rendererPtr->getDevice()->getDevice(), pipeline, nullptr);
        vkDestroyPipelineLayout(rendererPtr->getDevice()->getDevice(), pipelineLayout, nullptr);
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
        
        VkResult result = vkCreateComputePipelines(rendererPtr->getDevice()->getDevice(), creationInfo.cache, 1, &pipelineInfo, nullptr, &pipeline);
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
        
        VkResult result = vkCreateGraphicsPipelines(rendererPtr->getDevice()->getDevice(), creationInfo.cache, 1, &pipelineCreateInfo, nullptr, &pipeline);
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create a graphics pipeline");
        }
    }

    RasterPipeline::~RasterPipeline()
    {
    }

    //----------RT PIPELINE DEFINITIONS----------//

    std::list<RTPipeline*> RTPipeline::rtPipelines;
    std::unique_ptr<DeviceAllocation> RTPipeline::sbtAllocation;
    std::unique_ptr<Buffer> RTPipeline::sbtBuffer;

    RTPipeline::RTPipeline(const RTPipelineCreationInfo& creationInfo, const RTPipelineProperties& pipelineProperties)
        :Pipeline(creationInfo),
        pipelineProperties(pipelineProperties)
    {
        //shaders
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> rtShaderGroups;
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

        //raygen
        const uint32_t raygenCount = 1;
        VkRayTracingShaderGroupCreateInfoKHR rgenShaderGroup = {};
        rgenShaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        rgenShaderGroup.pNext = NULL;
        rgenShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        rgenShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        rgenShaderGroup.closestHitShader  = VK_SHADER_UNUSED_KHR;
        rgenShaderGroup.generalShader = shaderStages.size();
        rgenShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
        rgenShaderGroup.pShaderGroupCaptureReplayHandle = NULL;
        rtShaderGroups.push_back(rgenShaderGroup);

        VkPipelineShaderStageCreateInfo rgenStageInfo = {};
        rgenStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        rgenStageInfo.pNext = NULL;
        rgenStageInfo.flags = 0;
        rgenStageInfo.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        rgenStageInfo.module = creationInfo.rgenShader->getModule();
        rgenStageInfo.pName = "main"; //use main() function in shaders
        rgenStageInfo.pSpecializationInfo = NULL;
        shaderStages.push_back(rgenStageInfo);

        //shader groups
        uint32_t hitCount = 0;
        uint32_t callableCount = 0;
        uint32_t missCount = 0;
        for(const auto& shaderGroup : creationInfo.shaderGroups)
        {
            VkRayTracingShaderGroupCreateInfoKHR shaderGroupInfo = {};
            shaderGroupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shaderGroupInfo.pNext = NULL;
            shaderGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            shaderGroupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroupInfo.closestHitShader  = VK_SHADER_UNUSED_KHR;
            shaderGroupInfo.generalShader = VK_SHADER_UNUSED_KHR;
            shaderGroupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
            shaderGroupInfo.pShaderGroupCaptureReplayHandle = NULL;

            //individual shaders
            for(auto& [shaderStage, shader] : shaderGroup)
            {
                VkPipelineShaderStageCreateInfo shaderStageInfo = {};
                shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shaderStageInfo.pNext = NULL;
                shaderStageInfo.flags = 0;
                shaderStageInfo.stage = shaderStage;
                shaderStageInfo.module = shader->getModule();
                shaderStageInfo.pName = "main"; //use main() function in shaders
                shaderStageInfo.pSpecializationInfo = NULL;

                switch(shaderStage)
                {
                    case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:
                        shaderGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
                        shaderGroupInfo.anyHitShader = shaderStages.size();
                        hitCount++;
                        break;
                    case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
                        shaderGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
                        shaderGroupInfo.closestHitShader = shaderStages.size();
                        hitCount++;
                        break;
                    case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
                        shaderGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                        shaderGroupInfo.generalShader = shaderStages.size();
                        callableCount++;
                        break;
                    case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:
                        shaderGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
                        shaderGroupInfo.intersectionShader = shaderStages.size();
                        callableCount++;
                        break;
                    case VK_SHADER_STAGE_MISS_BIT_KHR:
                        shaderGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                        shaderGroupInfo.generalShader = shaderStages.size();
                        missCount++;
                }
                shaderStages.push_back(shaderStageInfo);
            }
            rtShaderGroups.push_back(shaderGroupInfo);
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
        pipelineCreateInfo.pDynamicState = NULL;
        pipelineCreateInfo.layout = pipelineLayout;
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex = -1;

        vkCreateDeferredOperationKHR(rendererPtr->getDevice()->getDevice(), nullptr, &deferredOperation);
        VkResult result = vkCreateRayTracingPipelinesKHR(rendererPtr->getDevice()->getDevice(), deferredOperation, creationInfo.cache, 1, &pipelineCreateInfo, nullptr, &pipeline);
        if(result != VK_SUCCESS && result != VK_OPERATION_DEFERRED_KHR && result != VK_OPERATION_NOT_DEFERRED_KHR)
        {
            throw std::runtime_error("Failed to create a ray tracing pipeline");
        }

        //add reference
        rtPipelines.push_back(this);

        //wait for deferred operation 
        while(!isBuilt()) {}

        //setup shader binding table
        auto getAlignment = [&](VkDeviceSize size, VkDeviceSize alignment){ return (size + (alignment - 1)) & ~(alignment - 1); }; //from NVIDIA

        const uint32_t handleCount = rtShaderGroups.size();
        const uint32_t handleSize  = rendererPtr->getDevice()->getRTproperties().shaderGroupHandleSize;
        const uint32_t handleAlignment = rendererPtr->getDevice()->getRTproperties().shaderGroupBaseAlignment;
        const uint32_t alignedSize = getAlignment(handleSize, handleAlignment);
        
        shaderBindingTableData.raygenShaderBindingTable.stride = alignedSize;
        shaderBindingTableData.raygenShaderBindingTable.size   = getAlignment(raygenCount * alignedSize, handleAlignment);
        shaderBindingTableData.missShaderBindingTable.stride = alignedSize;
        shaderBindingTableData.missShaderBindingTable.size   = getAlignment(missCount * alignedSize, handleAlignment);
        shaderBindingTableData.hitShaderBindingTable.stride  = alignedSize;
        shaderBindingTableData.hitShaderBindingTable.size    = getAlignment(hitCount * alignedSize, handleAlignment);
        shaderBindingTableData.callableShaderBindingTable.stride  = alignedSize;
        shaderBindingTableData.callableShaderBindingTable.size    = getAlignment(callableCount * alignedSize, handleAlignment);

        //get shader handles
        std::vector<char> handleData(handleSize * handleCount);
        VkResult result2 = vkGetRayTracingShaderGroupHandlesKHR(rendererPtr->getDevice()->getDevice(), pipeline, 0, handleCount, handleData.size(), handleData.data());
        if(result2 != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to get RT shader group handles");
        }

        VkDeviceSize currentHandleIndex = 0;
        sbtRawData.resize(alignedSize * handleCount);

        //raygen (always at 0)
        memcpy(sbtRawData.data(), handleData.data(), handleSize);
        currentHandleIndex++;

        //miss
        for(uint32_t i = 0; i < missCount; i++)
        {
            memcpy(sbtRawData.data() + (alignedSize * currentHandleIndex), handleData.data() + (handleSize * currentHandleIndex), handleSize);
            currentHandleIndex++;
        }
        
        //hit
        for(uint32_t i = 0; i < hitCount; i++)
        {
            memcpy(sbtRawData.data() + (alignedSize * currentHandleIndex), handleData.data() + (handleSize * currentHandleIndex), handleSize);
            currentHandleIndex++;
        }
        
        //callable
        for(uint32_t i = 0; i < callableCount; i++)
        {
            memcpy(sbtRawData.data() + (alignedSize * currentHandleIndex), handleData.data() + (handleSize * currentHandleIndex), handleSize);
            currentHandleIndex++;
        }

        //set SBT data
        rebuildSBTBufferAndAllocation(rendererPtr);
    }

    RTPipeline::~RTPipeline()
    {
        rtPipelines.remove(this);
        if(rtPipelines.size())
        {
            rebuildSBTBufferAndAllocation(rendererPtr);
        }
        else
        {
            sbtBuffer.reset();
            sbtAllocation.reset();
        }
       
    }

    void RTPipeline::rebuildSBTBufferAndAllocation(RenderEngine* renderer)
    {
        //get size and data
        VkDeviceSize newSize = 0;
        std::vector<char> allRawData;
        for(RTPipeline* pipeline : rtPipelines)
        {
            newSize += pipeline->sbtRawData.size();
            allRawData.insert(allRawData.end(), pipeline->sbtRawData.begin(), pipeline->sbtRawData.end());
        }

        //create buffers
        BufferInfo deviceBufferInfo = {};
        deviceBufferInfo.size = newSize;
        deviceBufferInfo.usageFlags = VK_BUFFER_USAGE_2_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR;
        sbtBuffer = std::make_unique<Buffer>(renderer, deviceBufferInfo);
        
        BufferInfo stagingBufferInfo = {};
        stagingBufferInfo.size = newSize;
        stagingBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        Buffer stagingBuffer(renderer, stagingBufferInfo);

        //create allocations
        DeviceAllocationInfo deviceAllocationInfo = {};
        deviceAllocationInfo.allocationSize = sbtBuffer->getMemoryRequirements().size;
        deviceAllocationInfo.allocFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        deviceAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        sbtAllocation = std::make_unique<DeviceAllocation>(renderer->getDevice()->getDevice(), renderer->getDevice()->getGPU(), deviceAllocationInfo);
        sbtBuffer->assignAllocation(sbtAllocation.get());

        DeviceAllocationInfo stagingAllocationInfo = {};
        stagingAllocationInfo.allocationSize = stagingBuffer.getMemoryRequirements().size;
        stagingAllocationInfo.allocFlags = 0;
        stagingAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        DeviceAllocation stagingAllocation(renderer->getDevice()->getDevice(), renderer->getDevice()->getGPU(), stagingAllocationInfo);
        stagingBuffer.assignAllocation(&stagingAllocation);

        //copy data
        BufferWrite writeInfo = {};
        writeInfo.data = allRawData.data();
        writeInfo.size = allRawData.size();
        writeInfo.offset = 0;
        stagingBuffer.writeToBuffer({ writeInfo });

        VkBufferCopy copyRegion = {};
        copyRegion.dstOffset = 0;
        copyRegion.srcOffset = 0;
        copyRegion.size = allRawData.size();

        SynchronizationInfo syncInfo = {};
        syncInfo.queueType = QueueType::TRANSFER;
        syncInfo.fence = Commands::getUnsignaledFence(renderer);

        renderer->recycleCommandBuffer(sbtBuffer->copyFromBufferRanges(stagingBuffer, { copyRegion }, syncInfo));

        //set SBT addresses
        VkDeviceAddress dynamicOffset = sbtBuffer->getBufferDeviceAddress();
        for(RTPipeline* pipeline : rtPipelines)
        {
            pipeline->shaderBindingTableData.raygenShaderBindingTable.deviceAddress = dynamicOffset;
            dynamicOffset += pipeline->shaderBindingTableData.raygenShaderBindingTable.size;
            pipeline->shaderBindingTableData.missShaderBindingTable.deviceAddress = dynamicOffset;
            dynamicOffset += pipeline->shaderBindingTableData.missShaderBindingTable.size;
            pipeline->shaderBindingTableData.hitShaderBindingTable.deviceAddress = dynamicOffset;
            dynamicOffset += pipeline->shaderBindingTableData.hitShaderBindingTable.size;
            pipeline->shaderBindingTableData.callableShaderBindingTable.deviceAddress = dynamicOffset;
            dynamicOffset += pipeline->shaderBindingTableData.callableShaderBindingTable.size;
        }

        vkWaitForFences(renderer->getDevice()->getDevice(), 1, &syncInfo.fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(renderer->getDevice()->getDevice(), syncInfo.fence, nullptr);
    }

    bool RTPipeline::isBuilt()
    {
        VkResult result = vkDeferredOperationJoinKHR(rendererPtr->getDevice()->getDevice(), deferredOperation);
        if(result == VK_SUCCESS || result == VK_THREAD_DONE_KHR)
        {
            VkResult result2 = vkGetDeferredOperationResultKHR(rendererPtr->getDevice()->getDevice(), deferredOperation);
            if(result2 != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create a ray tracing pipeline");
            }
            vkDestroyDeferredOperationKHR(rendererPtr->getDevice()->getDevice(), deferredOperation, nullptr);

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

    PipelineBuilder::PipelineBuilder(RenderEngine* renderer)
        :rendererPtr(renderer)
    {
        VkPipelineCacheCreateInfo creationInfo;
        creationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        creationInfo.pNext = NULL;
        creationInfo.flags = 0;
        creationInfo.initialDataSize = 0;
        creationInfo.pInitialData = NULL;
        vkCreatePipelineCache(rendererPtr->getDevice()->getDevice(), &creationInfo, nullptr, &cache);
    }

    PipelineBuilder::~PipelineBuilder()
    {
        vkDestroyPipelineCache(rendererPtr->getDevice()->getDevice(), cache, nullptr);
    }

    std::shared_ptr<Shader> PipelineBuilder::createShader(const ShaderPair& pair) const
    {
        std::string shaderFile = pair.directory;

        return std::make_shared<Shader>(rendererPtr->getDevice(), shaderFile);
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
            VkResult result = vkCreateDescriptorSetLayout(rendererPtr->getDevice()->getDevice(), &descriptorLayoutInfo, nullptr, &setLayout);
            if(result != VK_SUCCESS) throw std::runtime_error("Failed to create descriptor set layout");

            setLayouts[setNum] = setLayout;
        }

        return setLayouts;
    }

    VkPipelineLayout PipelineBuilder::createPipelineLayout(const std::unordered_map<uint32_t, VkDescriptorSetLayout>& setLayouts, std::vector<VkPushConstantRange> pcRanges) const
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
        layoutInfo.pushConstantRangeCount = pcRanges.size();
        layoutInfo.pPushConstantRanges = pcRanges.data();

        VkPipelineLayout returnLayout;
        VkResult result = vkCreatePipelineLayout(rendererPtr->getDevice()->getDevice(), &layoutInfo, nullptr, &returnLayout);
        if(result != VK_SUCCESS) throw std::runtime_error("Pipeline layout creation failed");

        return returnLayout;
    }

    std::unique_ptr<ComputePipeline> PipelineBuilder::buildComputePipeline(const ComputePipelineBuildInfo& info) const
    {
        ComputePipelineCreationInfo pipelineInfo = {};
        pipelineInfo.renderer = rendererPtr;
        pipelineInfo.cache = cache;
        pipelineInfo.setLayouts = createDescriptorLayouts(info.descriptors);
        pipelineInfo.pipelineLayout = createPipelineLayout(pipelineInfo.setLayouts, info.pcRanges);
        pipelineInfo.shader = createShader(info.shaderInfo);

        return std::make_unique<ComputePipeline>(pipelineInfo);
    }

    std::unique_ptr<RasterPipeline> PipelineBuilder::buildRasterPipeline(const RasterPipelineBuildInfo& info, const RasterPipelineProperties& pipelineProperties) const
    {
        RasterPipelineCreationInfo pipelineInfo = {};
        pipelineInfo.renderer = rendererPtr;
        pipelineInfo.cache = cache;
        pipelineInfo.setLayouts = createDescriptorLayouts(info.descriptors);
        pipelineInfo.pipelineLayout = createPipelineLayout(pipelineInfo.setLayouts, info.pcRanges);
        pipelineInfo.pcRanges = info.pcRanges;
        
        //set shaders
        for(const ShaderPair& pair : info.shaderInfo)
        {
            pipelineInfo.shaders[pair.stage] = createShader(pair);
        }

        return std::make_unique<RasterPipeline>(pipelineInfo, pipelineProperties);
    }

    std::unique_ptr<RTPipeline> PipelineBuilder::buildRTPipeline(const RTPipelineBuildInfo& info, const RTPipelineProperties& pipelineProperties) const
    {
        RTPipelineCreationInfo pipelineInfo = {};
        pipelineInfo.renderer = rendererPtr;
        pipelineInfo.cache = cache;
        pipelineInfo.setLayouts = createDescriptorLayouts(info.descriptors);
        pipelineInfo.pipelineLayout = createPipelineLayout(pipelineInfo.setLayouts, info.pcRanges);
        
        //get all shaders needed
        pipelineInfo.shaderGroups.resize(info.shaderGroups.size());
        uint32_t groupIndex = 0;
        for(const std::vector<ShaderPair>& shaderPairs : info.shaderGroups)
        {
            for(const ShaderPair& shaderPair : shaderPairs)
            {
                pipelineInfo.shaderGroups.at(groupIndex)[shaderPair.stage] = createShader(shaderPair);
            }
            groupIndex++;
        }
        pipelineInfo.rgenShader = createShader(info.rgenShader);

        return std::make_unique<RTPipeline>(pipelineInfo, pipelineProperties);
    }
}