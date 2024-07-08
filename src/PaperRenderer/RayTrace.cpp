#include "RayTrace.h"
#include "RHI/Pipeline.h"
#include "RHI/AccelerationStructure.h"
#include "PaperRenderer.h"
#include "Camera.h"

namespace PaperRenderer
{
    RayTraceRender::RayTraceRender(RenderEngine* renderer, AccelerationStructure* accelerationStructure, Camera* camera, const struct RTPipelineProperties& pipelineProperties, const RayTraceRenderInfo& rtRenderInfo)
        :rendererPtr(renderer),
        accelerationStructurePtr(accelerationStructure),
        cameraPtr(camera)
    {
        buildPipeline();
    }

    RayTraceRender::~RayTraceRender()
    {
    }

    void RayTraceRender::buildPipeline()
    {
    }

    void RayTraceRender::render(const PaperMemory::SynchronizationInfo &syncInfo)
    {
        VkCommandBuffer cmdBuffer = PaperMemory::Commands::getCommandBuffer(rendererPtr->getDevice()->getDevice(), PaperMemory::QueueType::COMPUTE);

        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->getPipeline());

        //write acceleration structure
        AccelerationStructureDescriptorWrites accelStructureWrites = {};
        accelStructureWrites.accelerationStructures = { accelerationStructurePtr };
        accelStructureWrites.binding = 0;
        rtDescriptorWrites.accelerationStructureWrites = { accelStructureWrites };

        //shader writes
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
    }
}