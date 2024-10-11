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
        const ShaderPair& raygenShaderPair,
        const std::vector<std::vector<ShaderPair>>& shaderGroups,
        std::vector<VkPushConstantRange> pcRanges
    )
        :rendererPtr(renderer),
        accelerationStructurePtr(accelerationStructure)
    {
        RTPipelineBuildInfo pipelineBuildInfo = {
            .rgenShader = raygenShaderPair,
            .shaderGroups = shaderGroups,
            .descriptors = descriptorSets,
            .pcRanges = pcRanges
        };
        pipeline = rendererPtr->getPipelineBuilder()->buildRTPipeline(pipelineBuildInfo, pipelineProperties);
    }

    RayTraceRender::~RayTraceRender()
    {
        pipeline.reset();
    }

    void RayTraceRender::render(const RayTraceRenderInfo& rtRenderInfo, const SynchronizationInfo& syncInfo)
    {
        //command buffer
        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        VkCommandBuffer cmdBuffer = rendererPtr->getDevice()->getCommandsPtr()->getCommandBuffer(QueueType::COMPUTE);
        vkBeginCommandBuffer(cmdBuffer, &commandInfo);

        //bind RT pipeline
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
        rendererPtr->getDevice()->getCommandsPtr()->submitToQueue(syncInfo, { cmdBuffer });

        CommandBuffer commandBuffer = { cmdBuffer, syncInfo.queueType };
        rendererPtr->recycleCommandBuffer(commandBuffer);
    }
}