#include "ComputeShader.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    ComputeShader::ComputeShader(RenderEngine& renderer)
        :renderer(renderer),
        pipelineBuildInfo({
            .shaderInfo = shader,
            .descriptors = descriptorSets,
            .pcRanges = pcRanges
        })
    {
    }

    ComputeShader::~ComputeShader()
    {
        pipeline.reset();
    }

    void ComputeShader::buildPipeline()
    {
        pipeline = renderer.getPipelineBuilder().buildComputePipeline(pipelineBuildInfo);
    }

    void ComputeShader::bind(VkCommandBuffer cmdBuffer)
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->getPipeline());
    }

    void ComputeShader::writeDescriptorSet(VkCommandBuffer cmdBuffer, uint32_t setNumber)
    {
        if(descriptorWrites.count(setNumber))
        {
            if(descriptorWrites.at(setNumber).bufferViewWrites.size() || descriptorWrites.at(setNumber).bufferWrites.size() || descriptorWrites.at(setNumber).imageWrites.size())
            {
                VkDescriptorSet descriptorSet = renderer.getDescriptorAllocator().allocateDescriptorSet(pipeline->getDescriptorSetLayouts().at(setNumber));
                DescriptorAllocator::writeUniforms(renderer, descriptorSet, descriptorWrites.at(setNumber));

                DescriptorBind bindingInfo = {};
                bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
                bindingInfo.set = descriptorSet;
                bindingInfo.descriptorScope = setNumber;
                bindingInfo.layout = pipeline->getLayout();
                
                DescriptorAllocator::bindSet(cmdBuffer, bindingInfo);
            }
        }
    }

    void ComputeShader::dispatch(VkCommandBuffer cmdBuffer)
    {
        vkCmdDispatch(cmdBuffer, workGroupSizes.x, workGroupSizes.y, workGroupSizes.z);
    }
}
