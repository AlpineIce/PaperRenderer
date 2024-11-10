#include "ComputeShader.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    ComputeShader::ComputeShader(RenderEngine& renderer, const ComputePipelineBuildInfo& pipelineInfo)
        :pipeline(renderer.getPipelineBuilder().buildComputePipeline(pipelineInfo)),
        renderer(renderer)
    {
    }

    ComputeShader::~ComputeShader()
    {
        pipeline.reset();
    }

    void ComputeShader::dispatch(const VkCommandBuffer& cmdBuffer,
        const std::unordered_map<uint32_t, DescriptorWrites>& descriptorWrites,
        const glm::uvec3& workGroupSizes
    ) const
    {
        //bind
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->getPipeline());

        //write descriptors
        for(const auto& [setNumber, writes] : descriptorWrites)
        {
            if(writes.bufferViewWrites.size() || writes.bufferWrites.size() || writes.imageWrites.size())
            {
                VkDescriptorSet descriptorSet = renderer.getDescriptorAllocator().allocateDescriptorSet(pipeline->getDescriptorSetLayouts().at(setNumber));
                DescriptorAllocator::writeUniforms(renderer, descriptorSet, descriptorWrites.at(setNumber));

                DescriptorBind bindingInfo = {};
                bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
                bindingInfo.set = descriptorSet;
                bindingInfo.descriptorSetIndex = setNumber;
                bindingInfo.layout = pipeline->getLayout();
                
                DescriptorAllocator::bindSet(cmdBuffer, bindingInfo);
            }
        }

        //dispatch
        vkCmdDispatch(cmdBuffer, workGroupSizes.x, workGroupSizes.y, workGroupSizes.z);
    }
}
