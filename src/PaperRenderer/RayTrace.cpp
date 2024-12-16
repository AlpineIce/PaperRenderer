#include "RayTrace.h"
#include "AccelerationStructure.h"
#include "PaperRenderer.h"
#include "Camera.h"
#include "Material.h"

namespace PaperRenderer
{
    RayTraceRender::RayTraceRender(
        RenderEngine& renderer,
        TLAS& accelerationStructure,
        const std::vector<ShaderDescription>& generalShaders,
        const std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>>& descriptorSets,
        const std::vector<VkPushConstantRange>& pcRanges
    )
        :pcRanges(pcRanges),
        descriptorSets(descriptorSets),
        generalShaders(generalShaders),
        renderer(renderer),
        tlas(accelerationStructure)
    {
    }

    RayTraceRender::~RayTraceRender()
    {
        pipeline.reset();
    }

    void RayTraceRender::render(RayTraceRenderInfo rtRenderInfo, SynchronizationInfo syncInfo)
    {
        //command buffer
        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(syncInfo.queueType);

        vkBeginCommandBuffer(cmdBuffer, &commandInfo);

        //pre-render barriers
        if(rtRenderInfo.preRenderBarriers)
        {
            vkCmdPipelineBarrier2(cmdBuffer, rtRenderInfo.preRenderBarriers);
        }

        //only trace rays if acceleration structure is valid
        if(tlas.getAccelerationStructure())
        {
            //bind pipeline
            vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->getPipeline());

            //descriptor writes
            if(rtRenderInfo.rtDescriptorWrites.bufferViewWrites.size() || rtRenderInfo.rtDescriptorWrites.bufferWrites.size() || 
                rtRenderInfo.rtDescriptorWrites.imageWrites.size() || rtRenderInfo.rtDescriptorWrites.accelerationStructureWrites.size())
            {
                VkDescriptorSet rtDescriptorSet = renderer.getDescriptorAllocator().allocateDescriptorSet(pipeline->getDescriptorSetLayouts().at(0));
                renderer.getDescriptorAllocator().writeUniforms(rtDescriptorSet, rtRenderInfo.rtDescriptorWrites);

                DescriptorBind bindingInfo = {};
                bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
                bindingInfo.set = rtDescriptorSet;
                bindingInfo.descriptorSetIndex = 0;
                bindingInfo.layout = pipeline->getLayout();
                
                renderer.getDescriptorAllocator().bindSet(cmdBuffer, bindingInfo);
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
        }

        //post-render barriers
        if(rtRenderInfo.postRenderBarriers)
        {
            vkCmdPipelineBarrier2(cmdBuffer, rtRenderInfo.postRenderBarriers);
        }
        
        //end
        vkEndCommandBuffer(cmdBuffer);

        renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);
        
        //submit
        syncInfo.timelineWaitPairs.push_back(renderer.asBuilder.getBuildSemaphore());
        renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer });
    }

    void RayTraceRender::updateTLAS(VkBuildAccelerationStructureModeKHR mode, VkBuildAccelerationStructureFlagsKHR flags, SynchronizationInfo syncInfo)
    {
        //update RT pipeline if needed (required to access SBT offsets for TLAS)
        if(queuePipelineBuild)
        {
            rebuildPipeline();
            queuePipelineBuild = false;
        }

        //update TLAS instances (signals transfer semaphore in staging buffer)
        tlas.queueInstanceTransfers(*this);

        //build TLAS
        renderer.asBuilder.queueAs({
            .accelerationStructure = tlas,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            .mode = mode,
            .flags = flags
        });

        syncInfo.timelineWaitPairs.push_back({ renderer.getStagingBuffer().getTransferSemaphore() });
        renderer.asBuilder.submitQueuedOps(syncInfo, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);
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
            .descriptorSets = descriptorSets,
            .pcRanges = pcRanges,
            .properties = pipelineProperties
        };
        pipeline = renderer.getPipelineBuilder().buildRTPipeline(pipelineBuildInfo);
    }

    void RayTraceRender::addInstance(ModelInstance& instance, const RTMaterial& material)
    {
        //add reference
        instance.rtRenderSelfReferences[this] = &material;

        //increment material reference counter and rebuild pipeline if needed
        if(!materialReferences.count(instance.rtRenderSelfReferences.at(this)))
        {
            queuePipelineBuild = true;
        }
        materialReferences[instance.rtRenderSelfReferences.at(this)]++;
    }
    
    void RayTraceRender::removeInstance(ModelInstance& instance)
    {
        if(instance.rtRenderSelfReferences.count(this) && materialReferences.count(instance.rtRenderSelfReferences.at(this)))
        {
            //decrement material reference and check size to see if material entry should be deleted
            materialReferences.at(instance.rtRenderSelfReferences.at(this))--;
            if(!materialReferences.at(instance.rtRenderSelfReferences.at(this)))
            {
                materialReferences.erase(instance.rtRenderSelfReferences.at(this));
                queuePipelineBuild = true;
            }

            instance.rtRenderSelfReferences.erase(this);
        }
    }
}