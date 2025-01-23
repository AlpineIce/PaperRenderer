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
        //destroy descriptors
        for(const auto& [setIndex, set] : descriptorSets)
        {
            renderer.getDescriptorAllocator().freeDescriptorSet(set);
        }

        pipeline.reset();
    }

    void ComputeShader::dispatch(const VkCommandBuffer& cmdBuffer,
        const std::unordered_map<uint32_t, DescriptorWrites>& descriptorWrites,
        const glm::uvec3& workGroupSizes
    )
    {
        //bind
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->getPipeline());

        //write descriptors
        for(const auto& [setIndex, writes] : descriptorWrites)
        {
            //make sure descriptor set exists
            if(!descriptorSets.count(setIndex))
            {
                descriptorSets[setIndex] = renderer.getDescriptorAllocator().getDescriptorSet(pipeline->getDescriptorSetLayouts().at(setIndex));
            }

            //update descriptor set
            renderer.getDescriptorAllocator().updateDescriptorSet(descriptorSets[setIndex], writes);

            const DescriptorBind bindingInfo = {
                .bindingPoint = VK_PIPELINE_BIND_POINT_COMPUTE,
                .layout = pipeline->getLayout(),
                .descriptorSetIndex = setIndex,
                .set = descriptorSets[setIndex]
            };
            
            renderer.getDescriptorAllocator().bindSet(cmdBuffer, bindingInfo);
        }

        //dispatch
        vkCmdDispatch(cmdBuffer, workGroupSizes.x, workGroupSizes.y, workGroupSizes.z);
    }
}
