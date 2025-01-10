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
        const ShaderDescription raygenShader,
        const std::vector<ShaderDescription> missShaders,
        const std::vector<ShaderDescription> callableShaders,
        const std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>>& descriptorSets,
        const RTPipelineProperties& pipelineProperties,
        const std::vector<VkPushConstantRange>& pcRanges
    )
        :pcRanges(pcRanges),
        descriptorSets(descriptorSets),
        pipelineProperties(pipelineProperties),
        raygenShader(raygenShader),
        missShaders(missShaders),
        callableShaders(callableShaders),
        renderer(renderer),
        tlas(accelerationStructure)
    {
    }

    RayTraceRender::~RayTraceRender()
    {
        pipeline.reset();
    }

    const Queue& RayTraceRender::render(RayTraceRenderInfo rtRenderInfo, SynchronizationInfo syncInfo)
    {
        //Timer
        Timer timer(renderer, "RayTraceRender Record", REGULAR);

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
                rtRenderInfo.image.getExtent().depth
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
        const Queue& queue = renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer });
        
        //assign ownership
        assignResourceOwner(queue);

        //return queue
        return  queue;
    }

    const Queue& RayTraceRender::updateTLAS(VkBuildAccelerationStructureModeKHR mode, VkBuildAccelerationStructureFlagsKHR flags, SynchronizationInfo syncInfo)
    {
        //update RT pipeline if needed (required to access SBT offsets for TLAS)
        if(queuePipelineBuild)
        {
            rebuildPipeline();
            queuePipelineBuild = false;
        }

        //update TLAS
        return tlas.updateTLAS(*this, mode, flags, syncInfo);
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

        //rebuild pipeline
        RTPipelineBuildInfo pipelineBuildInfo = {
            .materials = materials,
            .raygenShader = raygenShader,
            .missShaders = missShaders,
            .callableShaders = callableShaders,
            .descriptorSets = descriptorSets,
            .pcRanges = pcRanges,
            .properties = pipelineProperties
        };
        pipeline = renderer.getPipelineBuilder().buildRTPipeline(pipelineBuildInfo);
    }

    void RayTraceRender::assignResourceOwner(const Queue &queue)
    {
        pipeline->assignOwner(queue);
    }

    void RayTraceRender::addInstance(AccelerationStructureInstanceData instanceData, const RTMaterial& material)
    {
        //add reference
        instanceData.instancePtr->rtRenderSelfReferences[this] = {
            .material = &material,
            .selfIndex = (uint32_t)asInstances.size()
        };
        asInstances.push_back(instanceData);

        //increment material reference counter and rebuild pipeline if needed
        if(!materialReferences.count(&material))
        {
            queuePipelineBuild = true;
        }
        materialReferences[&material]++;
    }
    
    void RayTraceRender::removeInstance(ModelInstance& instance)
    {
        //shift AS instances locations
        if(instance.rtRenderSelfReferences.count(this))
        {
            if(asInstances.size() > 1)
            {
                const uint32_t selfIndex = instance.rtRenderSelfReferences[this].selfIndex;
                asInstances[selfIndex] = asInstances.back();
                asInstances[selfIndex].instancePtr->rtRenderSelfReferences[this].selfIndex = selfIndex;
                
                asInstances.pop_back();
            }
            else
            {
                asInstances.clear();
            }
        }

        //remove reference
        if(instance.rtRenderSelfReferences.count(this) && materialReferences.count(instance.rtRenderSelfReferences[this].material))
        {
            //decrement material reference and check size to see if material entry should be deleted
            materialReferences[instance.rtRenderSelfReferences[this].material]--;
            if(!materialReferences[instance.rtRenderSelfReferences[this].material])
            {
                materialReferences.erase(instance.rtRenderSelfReferences[this].material);
                queuePipelineBuild = true;
            }

            instance.rtRenderSelfReferences.erase(this);
        }
    }
}