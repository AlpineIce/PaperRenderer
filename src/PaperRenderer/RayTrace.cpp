#include "RayTrace.h"
#include "AccelerationStructure.h"
#include "PaperRenderer.h"
#include "Camera.h"
#include "Material.h"

namespace PaperRenderer
{
    RayTraceRender::RayTraceRender(
        RenderEngine& renderer,
        TLAS* accelerationStructure,
        const std::unordered_map<uint32_t, PaperRenderer::DescriptorSet>& descriptorSets,
        const std::vector<VkPushConstantRange>& pcRanges
    )
        :descriptorSets(descriptorSets),
        pcRanges(pcRanges),
        renderer(renderer),
        tlas(accelerationStructure)
    {
        if(descriptorSets.count(0) && (descriptorSets.at(0).descriptorBindings.count(0) || descriptorSets.at(0).descriptorBindings.count(1)))
        {
            throw std::runtime_error("Descriptor set 0 bindings 0 and 1 are reserved and cannot be set for RT shaders");
        }

        VkShaderStageFlags stages = 
            VK_SHADER_STAGE_RAYGEN_BIT_KHR |
            VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
            VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
            VK_SHADER_STAGE_MISS_BIT_KHR |
            VK_SHADER_STAGE_CALLABLE_BIT_KHR |
            VK_SHADER_STAGE_INTERSECTION_BIT_KHR;

        this->descriptorSets[0].descriptorBindings[0] = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            .descriptorCount = 1,
            .stageFlags = stages
        };
        this->descriptorSets[0].descriptorBindings[1] = {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = stages
        };
    }

    RayTraceRender::~RayTraceRender()
    {
        pipeline.reset();
    }

    void RayTraceRender::render(RayTraceRenderInfo rtRenderInfo, SynchronizationInfo syncInfo)
    {
        //update RT pipeline if needed
        if(queuePipelineBuild)
        {
            rebuildPipeline();
            queuePipelineBuild = false;
        }

        //update TLAS instances
        tlas->queueInstanceTransfers(this);

        //build TLAS (build timeline semaphore is implicitly signaled)
        renderer.asBuilder.queueAs({
            .accelerationStructure = tlas,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            .mode = rtRenderInfo.tlasBuildMode,
            .flags = rtRenderInfo.tlasBuildFlags
        });
        renderer.asBuilder.setBuildData();

        SynchronizationInfo tlasBuildSyncInfo = {};
        tlasBuildSyncInfo.queueType = COMPUTE;
        tlasBuildSyncInfo.binaryWaitPairs = syncInfo.binaryWaitPairs;
        tlasBuildSyncInfo.timelineWaitPairs = syncInfo.timelineWaitPairs;
        tlasBuildSyncInfo.timelineWaitPairs.push_back({ renderer.getEngineStagingBuffer().getTransferSemaphore() });
        renderer.asBuilder.submitQueuedOps(tlasBuildSyncInfo, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);

        //command buffer
        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(syncInfo.queueType);

        vkBeginCommandBuffer(cmdBuffer, &commandInfo);

        //bind pipeline
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->getPipeline());

        //write acceleration structure
        PaperRenderer::AccelerationStructureDescriptorWrites accelStructureWrites = {};
        accelStructureWrites.accelerationStructures = { tlas };
        accelStructureWrites.binding = 0;
        
        //write instance descriptions
        VkDescriptorBufferInfo instanceDescriptionWriteInfo = {};
        instanceDescriptionWriteInfo.buffer = tlas->getInstancesBuffer()->getBuffer();
        instanceDescriptionWriteInfo.offset = tlas->getInstanceDescriptionsOffset();
        instanceDescriptionWriteInfo.range = tlas->getInstanceDescriptionsRange();

        PaperRenderer::BuffersDescriptorWrites instanceDescriptionWrite = {};
        instanceDescriptionWrite.binding = 1;
        instanceDescriptionWrite.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        instanceDescriptionWrite.infos = { instanceDescriptionWriteInfo };

        //append
        rtRenderInfo.rtDescriptorWrites.accelerationStructureWrites.push_back(accelStructureWrites);
        rtRenderInfo.rtDescriptorWrites.bufferWrites.push_back(instanceDescriptionWrite);

        if(rtRenderInfo.rtDescriptorWrites.bufferViewWrites.size() || rtRenderInfo.rtDescriptorWrites.bufferWrites.size() || 
            rtRenderInfo.rtDescriptorWrites.imageWrites.size() || rtRenderInfo.rtDescriptorWrites.accelerationStructureWrites.size())
        {
            VkDescriptorSet rtDescriptorSet = renderer.getDescriptorAllocator().allocateDescriptorSet(pipeline->getDescriptorSetLayouts().at(0));
            DescriptorAllocator::writeUniforms(renderer, rtDescriptorSet, rtRenderInfo.rtDescriptorWrites);

            DescriptorBind bindingInfo = {};
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
            bindingInfo.set = rtDescriptorSet;
            bindingInfo.descriptorScope = 0;
            bindingInfo.layout = pipeline->getLayout();
            
            DescriptorAllocator::bindSet(cmdBuffer, bindingInfo);
        }

        //pre-pipeline barrier
        if(rtRenderInfo.preRenderBarriers)
        {
            vkCmdPipelineBarrier2(cmdBuffer, rtRenderInfo.preRenderBarriers);
        }

        //trace rays
        vkCmdTraceRaysKHR(
            cmdBuffer,
            &pipeline->getShaderBindingTableData().raygenShaderBindingTable,
            &pipeline->getShaderBindingTableData().missShaderBindingTable,
            &pipeline->getShaderBindingTableData().hitShaderBindingTable,
            &pipeline->getShaderBindingTableData().callableShaderBindingTable,
            rtRenderInfo.image.getExtent().width,
            rtRenderInfo.image.getExtent().height,
            1
        );

        //post-pipeline barrier
        if(rtRenderInfo.postRenderBarriers)
        {
            vkCmdPipelineBarrier2(cmdBuffer, rtRenderInfo.postRenderBarriers);
        }
        
        //end
        vkEndCommandBuffer(cmdBuffer);
        
        //submit
        syncInfo.timelineWaitPairs.push_back(renderer.asBuilder.getBuildSemaphore());
        renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer });
    }

    void RayTraceRender::rebuildPipeline()
    {
        //add up materials
        std::vector<RTMaterial*> materials;
        materials.reserve(materialReferences.size());
        for(const auto& [material, count] : materialReferences)
        {
            materials.push_back((RTMaterial*)material);
        }

        //add up general shaders
        std::vector<ShaderDescription> generalShaders{std::begin(this->generalShaders), std::end(this->generalShaders)};
        
        //rebuild pipeline
        RTPipelineBuildInfo pipelineBuildInfo = {
            .materials = materials,
            .generalShaders = generalShaders,
            .descriptors = descriptorSets,
            .pcRanges = pcRanges
        };
        pipeline = renderer.getPipelineBuilder().buildRTPipeline(pipelineBuildInfo, pipelineProperties);
    }

    void RayTraceRender::addInstance(ModelInstance *instance, RTMaterial const* material)
    {
        //add reference
        instance->rtRenderSelfReferences[this] = material;

        //increment material reference counter and rebuild pipeline if needed
        if(!materialReferences.count(instance->rtRenderSelfReferences.at(this)))
        {
            queuePipelineBuild = true;
        }
        materialReferences[instance->rtRenderSelfReferences.at(this)]++;
    }
    
    void RayTraceRender::removeInstance(ModelInstance *instance)
    {
        if(instance->rtRenderSelfReferences.count(this))
        {
            //decrement material reference and check size to see if material entry should be deleted
            materialReferences.at(instance->rtRenderSelfReferences.at(this))--;
            if(!materialReferences.at(instance->rtRenderSelfReferences.at(this)))
            {
                materialReferences.erase(instance->rtRenderSelfReferences.at(this));
                queuePipelineBuild = true;
            }

            instance->rtRenderSelfReferences.erase(this);
        }
    }

    void RayTraceRender::addGeneralShaders(const std::vector<ShaderDescription>& shaders)
    {
        generalShaders.insert(generalShaders.end(), shaders.begin(), shaders.end());
        queuePipelineBuild = true;
    }

    void RayTraceRender::removeGeneralShader(const ShaderDescription &shader)
    {
        auto compareOp = [&](const ShaderDescription& listShader) { return listShader.shader == shader.shader ? false : true; };
        generalShaders.remove_if(compareOp);
        queuePipelineBuild = true;
    }
}