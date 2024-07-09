#include "RayTrace.h"
#include "RHI/AccelerationStructure.h"
#include "PaperRenderer.h"
#include "Camera.h"
#include "Material.h"

namespace PaperRenderer
{
    RayTraceRender::RayTraceRender(RenderEngine* renderer, AccelerationStructure* accelerationStructure)
        :rendererPtr(renderer),
        accelerationStructurePtr(accelerationStructure)
    {
        rtDescriptorSets[0];
    }

    RayTraceRender::~RayTraceRender()
    {
    }

    void RayTraceRender::buildPipeline()
    {
        ShaderPair rgenShader = {
            .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            .directory = "resources/shaders/RTRayGen.spv"
        };
        ShaderPair missShader = {
            .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            .directory = "resources/shaders/RTMiss.spv"
        };

        //get materials TODO
        //std::vector<std::vector<ShaderPair>> shaderGroups;
        //shaderGroups

        RTPipelineBuildInfo pipelineBuildInfo = {
            .rgenShader = rgenShader,
            .missShader = missShader,
            .shaderGroups = shaderGroups,
            .descriptors = rtDescriptorSets
        };
        pipeline = rendererPtr->getPipelineBuilder()->buildRTPipeline(pipelineBuildInfo, pipelineProperties);
    }

    void RayTraceRender::render(const RayTraceRenderInfo& rtRenderInfo, const PaperMemory::SynchronizationInfo& syncInfo)
    {
        //command buffer
        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        VkCommandBuffer cmdBuffer = PaperMemory::Commands::getCommandBuffer(rendererPtr->getDevice()->getDevice(), PaperMemory::QueueType::COMPUTE);
        vkBeginCommandBuffer(cmdBuffer, &commandInfo);

        //bind RT pipeline
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->getPipeline());

        //write acceleration structure
        AccelerationStructureDescriptorWrites accelStructureWrites = {};
        accelStructureWrites.accelerationStructures = { accelerationStructurePtr };
        accelStructureWrites.binding = 0;
        rtDescriptorWrites.accelerationStructureWrites = { accelStructureWrites };

        //descriptor writes
        if(rtDescriptorWrites.bufferViewWrites.size() || rtDescriptorWrites.bufferWrites.size() || rtDescriptorWrites.imageWrites.size())
        {
            VkDescriptorSet rtDescriptorSet = rendererPtr->getDescriptorAllocator()->allocateDescriptorSet(pipeline->getDescriptorSetLayouts().at(0), *rendererPtr->getCurrentFramePtr());
            DescriptorAllocator::writeUniforms(rendererPtr->getDevice()->getDevice(), rtDescriptorSet, rtDescriptorWrites);

            DescriptorBind bindingInfo = {};
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
            bindingInfo.set = rtDescriptorSet;
            bindingInfo.descriptorScope = 0;
            bindingInfo.layout = pipeline->getLayout();
            
            DescriptorAllocator::bindSet(rendererPtr->getDevice()->getDevice(), cmdBuffer, bindingInfo);
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
        PaperMemory::Commands::submitToQueue(rendererPtr->getDevice()->getDevice(), syncInfo, { cmdBuffer });

        PaperMemory::CommandBuffer commandBuffer = { cmdBuffer, syncInfo.queueType };
        rendererPtr->recycleCommandBuffer(commandBuffer);
    }
}