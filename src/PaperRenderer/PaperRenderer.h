#pragma once
#include "GLFW/glfw3.h"

#include "Device.h"
#include "Descriptor.h"
#include "Pipeline.h"
#include "AccelerationStructure.h"
#include "RenderPass.h"
#include "RayTrace.h"
#include "Model.h"
#include "Camera.h"

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

    //Dynamically resizing staging buffer to be used for most staging data operations used by the renderer (render passes, acceleration structures, instances, etc)
    class EngineStagingBuffer
    {
    private:
        std::unique_ptr<Buffer> stagingBuffer;
        const float bufferOverhead = 1.5f;

        struct QueuedTransfer
        {
            const Buffer& dstBuffer;
            VkDeviceSize dstOffset;
            std::vector<char> data;
        };

        std::vector<QueuedTransfer> transferQueue;

        VkSemaphore transferSemaphore;
        VkDeviceSize lastSubmissionSize = 0;
        VkDeviceSize queueSize = 0;
        uint64_t finalSemaphoreValue = 0;

        struct TransferInfo
        {
            uint64_t semaphoreSignalValue;
            VkDeviceSize size;
            VkDeviceSize offset;
        };
        std::list<TransferInfo> previousTransfers;

        class RenderEngine* rendererPtr;

    public:
        EngineStagingBuffer(RenderEngine* renderer);
        ~EngineStagingBuffer();
        
        void queueDataTransfers(const Buffer& dstBuffer, VkDeviceSize dstOffset, const std::vector<char>& data);
        void submitQueuedTransfers(SynchronizationInfo syncInfo); //Submits all queued transfers and clears the queue. Does not need to be explicitly synced with last transfer
    };

    //Render engine object. Contains the entire state of the renderer and some important buffers
    class RenderEngine
    {
    private:
        Device device;
        Swapchain swapchain;
        DescriptorAllocator descriptors;
        PipelineBuilder pipelineBuilder;
        RasterPreprocessPipeline rasterPreprocessPipeline;
        TLASInstanceBuildPipeline tlasInstanceBuildPipeline;
        EngineStagingBuffer stagingBuffer;

        std::string shadersDir;
        RendererState rendererState = {};

        //frame rendering stuff
        std::vector<CommandBuffer> usedCmdBuffers;
        std::vector<ModelInstance*> renderingModelInstances;
        std::vector<Model*> renderingModels;

        //synchronization
        VkFence copyFence; //fence isn't really required, but buffer device address doesn't work properly with validation layer sync (or at least I believe so, it works perfectly fine without the fence)

        //----------BUFFERS----------//

        //host visible buffers
        const float instancesDataOverhead = 1.4f;
        std::unique_ptr<Buffer> hostInstancesDataBuffer;
        const float modelsDataOverhead = 1.2f;
        std::unique_ptr<FragmentableBuffer> hostModelDataBuffer;
        
        //device local buffers (mirror of host visible buffers)
        std::unique_ptr<Buffer> deviceInstancesDataBuffer;
        std::unique_ptr<Buffer> deviceModelDataBuffer;
        
        void rebuildInstancesbuffer();
        void rebuildModelDataBuffer();
        void handleModelDataCompaction(std::vector<CompactionResult> results);

        //----------END OF BUFFERS----------//

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

        uint32_t bufferIndex = 0;
        uint64_t frameNumber = 0;
    public:
        RenderEngine(RendererCreationStruct creationInfo);
        ~RenderEngine();

        //returns the image acquire semaphore from the swapchain
        const VkSemaphore& beginFrame(
            const std::vector<BinarySemaphorePair> &binaryBufferCopySignalSemaphores,
            const std::vector<TimelineSemaphorePair> &timelineBufferCopySignalSemaphores
        );
        void endFrame(const std::vector<VkSemaphore>& waitSemaphores); 

        void recycleCommandBuffer(CommandBuffer& commandBuffer);
        void recycleCommandBuffer(CommandBuffer&& commandBuffer);

        uint32_t getBufferIndex() const { return bufferIndex; }
        uint64_t getFramesRenderedCount() const { return frameNumber; }
        Device* getDevice() { return &device; }
        RasterPreprocessPipeline* getRasterPreprocessPipeline() { return &rasterPreprocessPipeline; }
        DescriptorAllocator* getDescriptorAllocator() { return &descriptors; }
        PipelineBuilder* getPipelineBuilder() { return &pipelineBuilder; }
        RendererState* getRendererState() { return &rendererState; }
        Swapchain* getSwapchain() { return &swapchain; }
        EngineStagingBuffer* getEngineStagingBuffer() { return &stagingBuffer; }
        const std::vector<Model*>& getModelReferences() const { return renderingModels; }
        const std::vector<ModelInstance*>& getModelInstanceReferences() const { return renderingModelInstances; }
        Buffer* getModelDataBuffer() const { return deviceModelDataBuffer.get(); }
    };
}