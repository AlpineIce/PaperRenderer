#include "RayTrace.h"
#include "PaperRenderer.h"
#include "Camera.h"
#include "Material.h"

#include <algorithm>

namespace PaperRenderer
{
    RayTraceRender::RayTraceRender(
        RenderEngine& renderer,
        const ShaderDescription& raygenShader,
        const std::vector<ShaderDescription>& missShaders,
        const std::vector<ShaderDescription>& callableShaders,
        const std::unordered_map<uint32_t, VkDescriptorSetLayout>& setLayouts,
        const RTPipelineProperties& pipelineProperties,
        const std::vector<VkPushConstantRange>& pcRanges
    )
        :pcRanges(pcRanges),
        setLayouts(setLayouts),
        pipelineProperties(pipelineProperties),
        raygenShader(raygenShader),
        missShaders(missShaders),
        callableShaders(callableShaders),
        renderer(renderer)
    {
    }

    RayTraceRender::~RayTraceRender()
    {
        pipeline.reset();
    }

    const Queue& RayTraceRender::render(const RayTraceRenderInfo& rtRenderInfo, const SynchronizationInfo& syncInfo)
    {
        //Timer
        Timer timer(renderer, "RayTraceRender Record", REGULAR);

        //command buffer
        const VkCommandBufferBeginInfo commandInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = NULL
        };

        VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(syncInfo.queueType);

        vkBeginCommandBuffer(cmdBuffer, &commandInfo);

        //pre-render barriers
        if(rtRenderInfo.preRenderBarriers)
        {
            vkCmdPipelineBarrier2(cmdBuffer, rtRenderInfo.preRenderBarriers);
        }

        //bind pipeline
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->getPipeline());

        //bind descriptors
        for(const SetBinding& setBinding : rtRenderInfo.descriptorBindings)
        {
            setBinding.set.bindDescriptorSet(cmdBuffer, setBinding.binding);
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

    const Queue& RayTraceRender::updateTLAS(TLAS& tlas, const VkBuildAccelerationStructureModeKHR mode, const VkBuildAccelerationStructureFlagsKHR flags, const SynchronizationInfo& syncInfo)
    {
        //update RT pipeline if needed (required to access SBT offsets for TLAS)
        if(queuePipelineBuild)
        {
            rebuildPipeline();
            queuePipelineBuild = false;
        }

        //sort instances; remove duplicates
        std::sort(tlasData[&tlas].toUpdateInstances.begin(), tlasData[&tlas].toUpdateInstances.end());
        auto sortedInstances = std::unique(tlasData[&tlas].toUpdateInstances.begin(), tlasData[&tlas].toUpdateInstances.end());
        tlasData[&tlas].toUpdateInstances.erase(sortedInstances, tlasData[&tlas].toUpdateInstances.end());

        //update TLAS
        const Queue& queue = tlas.updateTLAS(mode, flags, syncInfo);
        
        //clear toUpdateInstances
        tlasData[&tlas].toUpdateInstances.clear();

        //return queue used
        return queue;
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
        const RTPipelineInfo pipelineBuildInfo = {
            .materials = materials,
            .raygenShader = raygenShader,
            .missShaders = missShaders,
            .callableShaders = callableShaders,
            .descriptorSets = setLayouts,
            .pcRanges = pcRanges,
            .properties = pipelineProperties
        };
        pipeline = std::make_unique<RTPipeline>(renderer, pipelineBuildInfo);

        //invalidate all instances
        for(auto& [tlas, data] : tlasData)
        {
            data.toUpdateInstances.clear();
            data.toUpdateInstances.insert(data.toUpdateInstances.end(), data.instances.begin(), data.instances.end());
        }
    }

    void RayTraceRender::assignResourceOwner(const Queue &queue)
    {
        pipeline->assignOwner(queue);
    }

    std::unique_ptr<TLAS> RayTraceRender::addNewTLAS()
    {
        //create new TLAS and get its reference
        std::unique_ptr<TLAS> newTLAS = std::make_unique<TLAS>(renderer, *this);
        tlasData[newTLAS.get()] = {};

        //return new TLAS and its ownership
        return newTLAS;
    }

    void RayTraceRender::addInstance(AccelerationStructureInstanceData instanceData, const RTMaterial &material)
    {
        //log warning if no TLAS is specified
        if(!instanceData.owners.size())
        {
            renderer.getLogger().recordLog({
                .type = WARNING,
                .text = "Adding instance to RT render without any specified TLAS means this instance will do nothing"
            });
        }

        //add RT reference
        instanceData.instancePtr->rtRenderSelfReferences[this] = {
            .material = &material,
            .selfIndex = (uint32_t)asInstances.size()
        };
        asInstances.push_back(instanceData);

        //add TLAS references
        for(TLAS* tlas : instanceData.owners)
        {
            instanceData.instancePtr->tlasSelfReferences[tlas] = tlasData[tlas].instances.size();
            tlasData[tlas].instances.push_back(instanceData);

            tlasData[tlas].toUpdateInstances.push_front(*asInstances.rbegin());
        }

        //increment material reference counter and rebuild pipeline if needed
        if(!materialReferences.count(&material))
        {
            queuePipelineBuild = true;
        }
        materialReferences[&material]++;
    }
    
    void RayTraceRender::removeInstance(ModelInstance& instance)
    {
        if(instance.rtRenderSelfReferences.count(this))
        {
            //shift TLAS instances locations
            for(TLAS* tlas : asInstances[instance.rtRenderSelfReferences[this].selfIndex].owners)
            {
                if(instance.tlasSelfReferences.count(tlas))
                {
                    if(tlasData[tlas].instances.size() > 1)
                    {
                        const uint32_t selfIndex = instance.tlasSelfReferences[tlas];

                        tlasData[tlas].instances[selfIndex] = tlasData[tlas].instances.back();
                        tlasData[tlas].instances[selfIndex].instancePtr->tlasSelfReferences[tlas] = selfIndex;
                        
                        tlasData[tlas].instances.pop_back();

                        tlasData[tlas].toUpdateInstances.push_front(asInstances[instance.rtRenderSelfReferences[this].selfIndex]);
                    }
                    else
                    {
                        tlasData[tlas].instances.clear();
                    }

                    //null out any instances that may be queued
                    for(AccelerationStructureInstanceData& thisInstance : tlasData[tlas].toUpdateInstances)
                    {
                        if(thisInstance.instancePtr == &instance)
                        {
                            thisInstance.instancePtr = NULL;
                        }
                    }
                }
            }

            //shift AS instances locations
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

            //remove material reference
            if(materialReferences.count(instance.rtRenderSelfReferences[this].material))
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
}