#include "RayTrace.h"
#include "AccelerationStructure.h"
#include "PaperRenderer.h"
#include "Camera.h"
#include "Material.h"

namespace PaperRenderer
{
    RayTraceRender::RayTraceRender(
        RenderEngine* renderer,
        TLAS* accelerationStructure,
        const std::unordered_map<uint32_t, PaperRenderer::DescriptorSet>& descriptorSets,
        const std::vector<VkPushConstantRange>& pcRanges
    )
        :descriptorSets(descriptorSets),
        pcRanges(pcRanges),
        rendererPtr(renderer),
        tlas(accelerationStructure)
    {
    }

    RayTraceRender::~RayTraceRender()
    {
        pipeline.reset();
    }

    void RayTraceRender::render(const RayTraceRenderInfo& rtRenderInfo, SynchronizationInfo syncInfo)
    {
        //command buffer
        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        VkCommandBuffer cmdBuffer = rendererPtr->getDevice()->getCommandsPtr()->getCommandBuffer(QueueType::TRANSFER);

        //update TLAS instances
        vkBeginCommandBuffer(cmdBuffer, &commandInfo);
        tlas->queueInstanceTransfers(cmdBuffer, this);
        vkEndCommandBuffer(cmdBuffer);

        //submit and recycle (transfer timeline semaphore is implicitly signaled)
        SynchronizationInfo transferSyncInfo = {};
        transferSyncInfo.queueType = TRANSFER;
        transferSyncInfo.binaryWaitPairs = syncInfo.binaryWaitPairs;
        transferSyncInfo.timelineWaitPairs = syncInfo.timelineWaitPairs;

        rendererPtr->getDevice()->getCommandsPtr()->submitToQueue(transferSyncInfo, { cmdBuffer });
        rendererPtr->recycleCommandBuffer({ cmdBuffer, syncInfo.queueType });

        //build TLAS (build timeline semaphore is implicitly signaled)
        rendererPtr->asBuilder.queueAs({
            .accelerationStructure = tlas,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            .mode = rtRenderInfo.tlasBuildMode,
            .flags = rtRenderInfo.tlasBuildFlags
        });
        rendererPtr->asBuilder.setBuildData();

        SynchronizationInfo tlasBuildSyncInfo = {};
        tlasBuildSyncInfo.queueType = COMPUTE;
        tlasBuildSyncInfo.timelineWaitPairs = { rendererPtr->getEngineStagingBuffer()->getTransferSemaphore() };
        rendererPtr->asBuilder.submitQueuedOps(tlasBuildSyncInfo, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);

        //new command buffer for ray tracing
        cmdBuffer = rendererPtr->getDevice()->getCommandsPtr()->getCommandBuffer(syncInfo.queueType);
        vkBeginCommandBuffer(cmdBuffer, &commandInfo);

        //bind RT pipeline
        if(queuePipelineBuild)
        {
            rebuildPipeline();
        }
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->getPipeline());

        //descriptor writes
        if(rtRenderInfo.rtDescriptorWrites.bufferViewWrites.size() || rtRenderInfo.rtDescriptorWrites.bufferWrites.size() || 
            rtRenderInfo.rtDescriptorWrites.imageWrites.size() || rtRenderInfo.rtDescriptorWrites.accelerationStructureWrites.size())
        {
            VkDescriptorSet rtDescriptorSet = rendererPtr->getDescriptorAllocator()->allocateDescriptorSet(pipeline->getDescriptorSetLayouts().at(0));
            DescriptorAllocator::writeUniforms(rendererPtr, rtDescriptorSet, rtRenderInfo.rtDescriptorWrites);

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
        syncInfo.timelineWaitPairs.push_back(rendererPtr->asBuilder.getBuildSemaphore());
        rendererPtr->getDevice()->getCommandsPtr()->submitToQueue(syncInfo, { cmdBuffer });

        CommandBuffer commandBuffer = { cmdBuffer, syncInfo.queueType };
        rendererPtr->recycleCommandBuffer(commandBuffer);
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
        std::vector<ShaderDescription> generalShaders{std::begin(generalShaders), std::end(generalShaders)};
        
        //rebuild pipeline
        RTPipelineBuildInfo pipelineBuildInfo = {
            .materials = materials,
            .generalShaders = generalShaders,
            .descriptors = descriptorSets,
            .pcRanges = pcRanges
        };
        pipeline = rendererPtr->getPipelineBuilder()->buildRTPipeline(pipelineBuildInfo, pipelineProperties);
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