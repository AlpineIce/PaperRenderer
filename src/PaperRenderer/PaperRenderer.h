#pragma once
#include "GLFW/glfw3.h"

#include "RHI/Device.h"
#include "RHI/Memory/VulkanResources.h"
#include "RenderPass.h"

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
        bool rtEnabled = false;
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

        std::string shadersDir;
        RendererState rendererState = {};

        //frame rendering stuff
        std::vector<std::vector<PaperMemory::CommandBuffer>> usedCmdBuffers;
        std::vector<ModelInstance*> renderingModelInstances;
        std::vector<Model*> renderingModels;

        //synchronization
        std::list<VkFence> preprocessFences;
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

        uint32_t currentImage = 0;
    public:
        RenderEngine(RendererCreationStruct creationInfo);
        ~RenderEngine();

        //returns 0 if no swapchain rebuild occured; returns 1 if swapchain was rebuilt; returns 2 if the entire frame should be skipped due to recreation failure on frame index that isn't 0
        int beginFrame(const std::vector<VkFence>& waitFences, VkSemaphore imageAquireSignalSemaphore, const std::vector<PaperMemory::SemaphorePair>& bufferCopySignalSemaphores);
        //returns 0 if no swapchain rebuild occured; returns 1 otherwise
        int endFrame(const std::vector<VkSemaphore>& waitSemaphores); 

        void recycleCommandBuffer(PaperMemory::CommandBuffer& commandBuffer);
        void recycleCommandBuffer(PaperMemory::CommandBuffer&& commandBuffer);

        uint32_t const* getCurrentFramePtr() const { return &currentImage; }
        Device* getDevice() { return &device; }
        RasterPreprocessPipeline* getRasterPreprocessPipeline() { return &rasterPreprocessPipeline; }
        DescriptorAllocator* getDescriptorAllocator() { return &descriptors; }
        RendererState* getRendererState() { return &rendererState; }
        Swapchain* getSwapchain() { return &swapchain; }
        const std::vector<Model*>& getModelReferences() const { return renderingModels; }
        const std::vector<ModelInstance*>& getModelInstanceReferences() const { return renderingModelInstances; }
    };
}