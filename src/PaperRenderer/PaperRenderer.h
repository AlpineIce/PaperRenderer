#pragma once
#include "GLFW/glfw3.h"

#include "RHI/Device.h"
#include "RHI/Descriptor.h"
#include "RHI/Pipeline.h"
#include "RHI/AccelerationStructure.h"
#include "RenderPass.h"
#include "RayTrace.h"
#include "Model.h"

#include <string>
#include <memory>
#include <vector>

namespace PaperRenderer
{
    struct RendererCreationStruct
    {
        std::string shadersDir;
        WindowState windowState = {};
    };

    struct RendererState
    {
        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    };

    class RenderEngine
    {
    private:
        Device device;
        Swapchain swapchain;
        DescriptorAllocator descriptors;
        PipelineBuilder pipelineBuilder;
        RasterPreprocessPipeline rasterPreprocessPipeline;
        TLASInstanceBuildPipeline tlasInstanceBuildPipeline;

        std::string shadersDir;
        RendererState rendererState = {};

        //frame rendering stuff
        std::vector<std::vector<PaperMemory::CommandBuffer>> usedCmdBuffers;
        std::vector<ModelInstance*> renderingModelInstances;
        std::vector<Model*> renderingModels;

        //synchronization
        std::list<VkFence> preprocessFences;
        std::list<VkFence> accelerationStructureFences;
        VkFence copyFence; //fence isn't really required, but buffer device address doesn't work properly with validation layer sync (or at least I believe so, it works perfectly fine without the fence)

        //----------BUFFERS AND MEMORY----------//

        //allocations
        std::unique_ptr<PaperMemory::DeviceAllocation> hostDataAllocation;
        std::unique_ptr<PaperMemory::DeviceAllocation> deviceDataAllocation;

        //host visible buffers
        const float instancesDataOverhead = 1.4f;
        std::unique_ptr<PaperMemory::Buffer> hostInstancesDataBuffer;
        const float modelsDataOverhead = 1.2f;
        std::unique_ptr<PaperMemory::FragmentableBuffer> hostModelDataBuffer;
        
        //device local buffers (mirror of host visible buffers)
        std::unique_ptr<PaperMemory::Buffer> deviceInstancesDataBuffer;
        std::unique_ptr<PaperMemory::Buffer> deviceModelDataBuffer;
        
        void rebuildBuffersAndAllocations();
        void rebuildInstancesbuffers();
        void rebuildModelDataBuffers(VkDeviceSize rebuildSize);
        void handleModelDataCompaction(std::vector<PaperMemory::CompactionResult> results);

        //----------END OF BUFFERS AND MEMORY----------//

        void addModelData(Model* model);
        void removeModelData(Model* model);
        void addObject(ModelInstance* object);
        void removeObject(ModelInstance* object);

        friend Model;
        friend ModelInstance;
        friend RenderPass;
        friend RasterPreprocessPipeline;
        friend AccelerationStructure;
        friend TLASInstanceBuildPipeline;

        uint32_t currentImage = 0;
    public:
        RenderEngine(RendererCreationStruct creationInfo);
        ~RenderEngine();

        //returns 0 if no swapchain rebuild occured; returns 1 if the entire frame should be skipped due to recreation failure on frame index that isn't 0
        int beginFrame(const std::vector<VkFence>& waitFences, VkSemaphore imageAquireSignalSemaphore, const std::vector<PaperMemory::SemaphorePair>& bufferCopySignalSemaphores);
        void endFrame(const std::vector<VkSemaphore>& waitSemaphores); 

        void recycleCommandBuffer(PaperMemory::CommandBuffer& commandBuffer);
        void recycleCommandBuffer(PaperMemory::CommandBuffer&& commandBuffer);

        uint32_t const* getCurrentFramePtr() const { return &currentImage; }
        Device* getDevice() { return &device; }
        RasterPreprocessPipeline* getRasterPreprocessPipeline() { return &rasterPreprocessPipeline; }
        DescriptorAllocator* getDescriptorAllocator() { return &descriptors; }
        PipelineBuilder* getPipelineBuilder() { return &pipelineBuilder; }
        RendererState* getRendererState() { return &rendererState; }
        Swapchain* getSwapchain() { return &swapchain; }
        const std::vector<Model*>& getModelReferences() const { return renderingModels; }
        const std::vector<ModelInstance*>& getModelInstanceReferences() const { return renderingModelInstances; }
        PaperMemory::Buffer* getModelDataBuffer() const { return deviceModelDataBuffer.get(); }
    };
}