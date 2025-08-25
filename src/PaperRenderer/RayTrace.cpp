#include "RayTrace.h"
#include "PaperRenderer.h"
#include "Camera.h"
#include "Material.h"

#include <algorithm>

namespace PaperRenderer
{
    RayTraceRender::RayTraceRender(
        RenderEngine& renderer,
        const std::vector<uint32_t>& raygenShader,
        const std::vector<std::vector<uint32_t>>& missShaders,
        const std::vector<std::vector<uint32_t>>& callableShaders,
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

    Queue& RayTraceRender::render(const RayTraceRenderInfo& rtRenderInfo, const SynchronizationInfo& syncInfo)
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

        CommandBuffer cmdBuffer(renderer.getDevice().getCommands(), COMPUTE);

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
        
        //submit
        Queue& queue = renderer.getDevice().getCommands().submitToQueue(COMPUTE, syncInfo, { cmdBuffer });
        
        //assign ownership
        assignResourceOwner(queue);

        //return queue
        return  queue;
    }

    Queue& RayTraceRender::updateTLAS(TLAS& tlas, const VkBuildAccelerationStructureModeKHR mode, const VkBuildAccelerationStructureFlagsKHR flags, const SynchronizationInfo& syncInfo)
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
        Queue& queue = tlas.updateTLAS(mode, flags, syncInfo);
        
        //clear toUpdateInstances
        tlasData[&tlas].toUpdateInstances.clear();

        //return queue used
        return queue;
    }

    void RayTraceRender::rebuildPipeline()
    {
        //lock mutex
        std::lock_guard guard(rtRenderMutex);

        //there still may be a condition here if another thread was waiting for the mutex, so early return if no longer needed
        if(!queuePipelineBuild) return;

        //add up materials
        std::vector<ShaderHitGroup*> materials;
        materials.reserve(materialReferences.size());
        for(const auto& [material, count] : materialReferences)
        {
            materials.push_back((ShaderHitGroup*)material);
        }

        //rebuild pipeline
        const RTPipelineInfo pipelineBuildInfo = {
            .materials = materials,
            .raygenShader = &raygenShader,
            .missShaders = &missShaders,
            .callableShaders = &callableShaders,
            .descriptorSets = setLayouts,
            .pcRanges = pcRanges,
            .properties = pipelineProperties
        };
        pipeline = std::make_unique<RTPipeline>(renderer, pipelineBuildInfo);

        //invalidate all instances
        for(auto& [tlas, data] : tlasData)
        {
            data.toUpdateInstances.clear();
            data.toUpdateInstances.insert(data.toUpdateInstances.end(), data.instanceDatas.begin(), data.instanceDatas.end());
        }

        //set pipeline build flag
        queuePipelineBuild = false;
    }

    void RayTraceRender::assignResourceOwner(Queue &queue)
    {
        pipeline->assignOwner(queue);
    }

    std::unique_ptr<TLAS> RayTraceRender::addNewTLAS()
    {
        //lock mutex
        std::lock_guard guard(rtRenderMutex);
        
        //create new TLAS and get its reference
        std::unique_ptr<TLAS> newTLAS = std::make_unique<TLAS>(renderer, *this);
        tlasData[newTLAS.get()] = {};

        //return new TLAS and its ownership
        return newTLAS;
    }

    void RayTraceRender::addInstance(const std::unordered_map<TLAS*, AccelerationStructureInstanceData>& asDatas)
    {
        //lock mutex
        std::lock_guard guard(rtRenderMutex);

        //iterate TLAS'
        for(auto& [tlas, instanceData] : asDatas)
        {
            //add TLAS references and queue update
            instanceData.instancePtr->rtRenderSelfReferences[this][tlas] = {
                .material = instanceData.hitGroup,
                .selfIndex = (uint32_t)tlasData[tlas].instanceDatas.size()
            };
            tlasData[tlas].instanceDatas.push_back(instanceData);
            tlasData[tlas].toUpdateInstances.push_front(instanceData);

            //increment material reference counter and rebuild pipeline if needed
            if(!materialReferences.count(instanceData.hitGroup))
            {
                queuePipelineBuild = true;
            }
            materialReferences[instanceData.hitGroup]++;
        }
    }
    
    void RayTraceRender::removeInstance(ModelInstance& instance)
    {
        //lock mutex
        std::lock_guard guard(rtRenderMutex);

        //iterate TLAS'
        if(instance.rtRenderSelfReferences.count(this))
        {
            for(auto& [tlas, data] : instance.rtRenderSelfReferences[this])
            {
                if(tlasData[tlas].instanceDatas.size() > 1)
                {
                    tlasData[tlas].instanceDatas[data.selfIndex] = tlasData[tlas].instanceDatas.back();
                    tlasData[tlas].instanceDatas[data.selfIndex].instancePtr->rtRenderSelfReferences[this][tlas] = data;

                    tlasData[tlas].toUpdateInstances.push_front(tlasData[tlas].instanceDatas[data.selfIndex]);

                    tlasData[tlas].instanceDatas.pop_back();
                }
                else
                {
                    tlasData[tlas].instanceDatas.clear();
                }

                //null out any instances that may be queued
                for(AccelerationStructureInstanceData& thisInstance : tlasData[tlas].toUpdateInstances)
                {
                    if(thisInstance.instancePtr == &instance)
                    {
                        thisInstance.instancePtr = NULL;
                    }
                }

                //remove material reference
                if(materialReferences.count(data.material))
                {
                    //decrement material reference and check size to see if material entry should be deleted
                    materialReferences[data.material]--;
                    if(!materialReferences[data.material])
                    {
                        materialReferences.erase(data.material);
                        queuePipelineBuild = true;
                    }
                }
            }

            // Erase this reference
            instance.rtRenderSelfReferences.erase(this);
        }
    }
}