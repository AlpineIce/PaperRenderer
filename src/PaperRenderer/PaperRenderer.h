#pragma once
#include "GLFW/glfw3.h"

#include "RHI/Device.h"
#include "RHI/Memory/VulkanResources.h"
#include "RHI/Window.h"
#include "RenderPass.h"

#include <string>
#include <memory>
#include <vector>

namespace PaperRenderer
{
    struct RendererCreationStruct
    {
        std::string appName;
        std::string shadersDir;
        unsigned int resX;
        unsigned int resY;
    };

    class RenderEngine
    {
    private:
        Device device;
        Window window;
        Swapchain swapchain;
        DescriptorAllocator descriptors;
        PipelineBuilder pipelineBuilder;
        RasterPreprocessPipeline rasterPreprocessPipeline;

        std::string appName;
        std::string shadersDir;
        bool rtEnabled = false;

        //frame rendering stuff
        std::vector<VkFence> bufferCopyFences;
        std::vector<std::vector<PaperMemory::CommandBuffer>> usedCmdBuffers;
        std::vector<ModelInstance*> renderingModelInstances;
        std::vector<Model*> renderingModels;

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

        void addModelData(Model& model, uint64_t& selfIndex);
        void removeModelData(Model& model, uint64_t& selfIndex);
        void addObject(ModelInstance& object, uint64_t& selfIndex);
        void removeObject(ModelInstance& object, uint64_t& selfIndex);

        friend Model;
        friend ModelInstance;

        uint32_t currentImage = 0;
    public:
        RenderEngine(RendererCreationStruct creationInfo);
        ~RenderEngine();

        //draw all the items in the render tree
        int beginFrame(const std::vector<VkFence>& waitFences, VkSemaphore& imageAquireSignalSemaphore); //returns 0 if no swapchain rebuild occured; returns 1 otherwise
        int endFrame(const std::vector<VkSemaphore>& waitSemaphores); //returns 0 if no swapchain rebuild occured; returns 1 otherwise

        bool getRTstatus() const { return rtEnabled; }
        void setRTstatus(bool newStatus) { this->rtEnabled = newStatus; }

        void recycleCommandBuffer(PaperMemory::CommandBuffer& commandBuffer);

        uint32_t const* getCurrentFramePtr() const { return &currentImage; }
        RasterPreprocessPipeline* getRasterPreprocessPipeline() { return &rasterPreprocessPipeline; }
        PaperMemory::Buffer const* getHostInstancesBufferPtr() const { return hostInstancesDataBuffer.get(); }
        PaperMemory::Buffer const* getDeviceInstancesBufferPtr() const { return deviceInstancesDataBuffer.get(); }
        GLFWwindow* getGLFWwindow() const { return window.getWindow(); }
        Device* getDevice() { return &device; }
        const VkExtent2D& getResolution() const { return swapchain.getExtent(); }
        const std::vector<ModelInstance*>& getModelInstanceReferences() const { return renderingModelInstances; }
        std::string getShadersDir() const { return shadersDir; }
    };
}