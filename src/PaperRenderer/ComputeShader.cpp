#include "ComputeShader.h"
#include "RHI/Device.h"

namespace PaperRenderer
{
    ComputeShader::ComputeShader()
    {
        pipelineBuildInfo.shaderInfo = &shader;
        pipelineBuildInfo.descriptors = &descriptorSets;
    }

    ComputeShader::~ComputeShader()
    {
    }

    void ComputeShader::buildPipeline()
    {
        pipeline = PipelineBuilder::getRendererInfo().pipelineBuilderPtr->buildComputePipeline(pipelineBuildInfo);
    }

    void ComputeShader::bind(VkCommandBuffer cmdBuffer)
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->getPipeline());
    }

    void ComputeShader::writeDescriptorSet(VkCommandBuffer cmdBuffer, uint32_t currentImage, uint32_t setNumber)
    {
        if(descriptorWrites.count(setNumber))
        {
            if(descriptorWrites.at(setNumber).bufferViewWrites.size() || descriptorWrites.at(setNumber).bufferWrites.size() || descriptorWrites.at(setNumber).imageWrites.size())
            {
                VkDescriptorSet descriptorSet = PipelineBuilder::getRendererInfo().descriptorsPtr->allocateDescriptorSet(pipeline->getDescriptorSetLayouts().at(setNumber), currentImage);
                DescriptorAllocator::writeUniforms(PipelineBuilder::getRendererInfo().devicePtr->getDevice(), descriptorSet, descriptorWrites.at(setNumber));

                DescriptorBind bindingInfo = {};
                bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
                bindingInfo.set = descriptorSet;
                bindingInfo.descriptorScope = setNumber;
                bindingInfo.layout = pipeline->getLayout();
                
                DescriptorAllocator::bindSet(PipelineBuilder::getRendererInfo().devicePtr->getDevice(), cmdBuffer, bindingInfo);
            }
        }
    }

    void ComputeShader::dispatch(VkCommandBuffer cmdBuffer)
    {
        vkCmdDispatch(cmdBuffer, workGroupSizes.x, workGroupSizes.y, workGroupSizes.z);
    }
}
