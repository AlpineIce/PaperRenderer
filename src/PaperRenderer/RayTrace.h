#pragma once
#include "RHI/AccelerationStructure.h"
#include "ComputeShader.h"

namespace PaperRenderer
{
    class RTPreprocessPipeline : public ComputeShader
    {
    private:
        std::string fileName = "RTObjectBuild.spv";
        std::vector<std::unique_ptr<PaperMemory::Buffer>> uniformBuffers;
        std::unique_ptr<PaperMemory::DeviceAllocation> uniformBuffersAllocation;

        struct UBOInputData
        {
            VkDeviceAddress tlasInstancesAddress;
            uint32_t objectCount;
        };

    public:
        RTPreprocessPipeline(std::string fileDir);
        ~RTPreprocessPipeline() override;

        PaperMemory::CommandBuffer submit();
    };
}