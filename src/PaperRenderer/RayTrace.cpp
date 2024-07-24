#include "RayTrace.h"
#include "RHI/AccelerationStructure.h"
#include "PaperRenderer.h"
#include "Camera.h"
#include "Material.h"

namespace PaperRenderer
{
    RayTraceRender::RayTraceRender(RenderEngine* renderer, AccelerationStructure* accelerationStructure, const std::unordered_map<uint32_t, PaperRenderer::DescriptorSet>& descriptorSets)
        :rendererPtr(renderer),
        accelerationStructurePtr(accelerationStructure)
    {
        buildPipeline(descriptorSets);
    }

    RayTraceRender::~RayTraceRender()
    {
    }

    void RayTraceRender::buildPipeline(const std::unordered_map<uint32_t, PaperRenderer::DescriptorSet>& descriptorSets)
    {
        std::vector<std::vector<ShaderPair>> shaderGroups;

        //rgen shader
        ShaderPair rgenShader = {
            .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            .directory = "resources/shaders/RTRayGen.spv"
        };
        
        //environment map miss
        shaderGroups.push_back({ {
            .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
            .directory = "resources/shaders/RTMiss.spv"
        } });

        //shadow miss
        shaderGroups.push_back({ {
            .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
            .directory = "resources/shaders/RTShadow.spv"
        } });

        //get materials TODO
        shaderGroups.push_back({ { 
            .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            .directory = "resources/shaders/RTChit.spv"
        } });

        RTPipelineBuildInfo pipelineBuildInfo = {
            .rgenShader = rgenShader,
            .shaderGroups = shaderGroups,
            .descriptors = descriptorSets
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

        //descriptor writes
        if(rtRenderInfo.rtDescriptorWrites.bufferViewWrites.size() || rtRenderInfo.rtDescriptorWrites.bufferWrites.size() || 
            rtRenderInfo.rtDescriptorWrites.imageWrites.size() || rtRenderInfo.rtDescriptorWrites.accelerationStructureWrites.size())
        {
            VkDescriptorSet rtDescriptorSet = rendererPtr->getDescriptorAllocator()->allocateDescriptorSet(pipeline->getDescriptorSetLayouts().at(0), rendererPtr->getCurrentFrameIndex());
            DescriptorAllocator::writeUniforms(rendererPtr->getDevice()->getDevice(), rtDescriptorSet, rtRenderInfo.rtDescriptorWrites);

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