#pragma once
#include "RHI/Pipeline.h"

namespace PaperRenderer
{
    class ComputeShader
    {
    private:
        ComputePipelineBuildInfo pipelineBuildInfo;
        std::unique_ptr<ComputePipeline> pipeline;
        
    protected:
        ShaderPair shader;
        std::unordered_map<uint32_t, DescriptorWrites> descriptorWrites;
        std::unordered_map<uint32_t, DescriptorSet> descriptorSets;
        glm::uvec3 workGroupSizes = glm::uvec3(1, 1, 1);

        void buildPipeline();

    public:
        ComputeShader();
        virtual ~ComputeShader();

        void bind(VkCommandBuffer cmdBuffer);
        void writeDescriptorSet(VkCommandBuffer cmdBuffer, uint32_t currentImage, uint32_t setNumber);
        virtual void dispatch(VkCommandBuffer cmdBuffer);
    };
}