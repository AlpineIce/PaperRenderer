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
        //update RT pipeline if needed
        if(queuePipelineBuild)
        {
            rebuildPipeline();
            queuePipelineBuild = false;
        }

        //update TLAS instances
        tlas.queueInstanceTransfers(this);

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
        accelStructureWrites.accelerationStructures = { &tlas };
        accelStructureWrites.binding = 0;
        
        //write instance descriptions
        VkDescriptorBufferInfo instanceDescriptionWriteInfo = {};
        instanceDescriptionWriteInfo.buffer = tlas.getInstancesBuffer().getBuffer();
        instanceDescriptionWriteInfo.offset = tlas.getInstanceDescriptionsOffset();
        instanceDescriptionWriteInfo.range = tlas.getInstanceDescriptionsRange();

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
            bindingInfo.descriptorSetIndex = 0;
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
            .descriptorSets = descriptorSets,
            .pcRanges = pcRanges,
            .properties = pipelineProperties
        };
        pipeline = renderer.getPipelineBuilder().buildRTPipeline(pipelineBuildInfo);
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
}