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
#include <deque>
#include <queue>

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
            VkDeviceSize dstOffset;
            std::vector<char> data;
        };

        std::unordered_map<Buffer const*, std::deque<QueuedTransfer>> transferQueues;

        VkSemaphore transferSemaphore;
        VkDeviceSize queueSize = 0;
        uint64_t finalSemaphoreValue = 0;

        class RenderEngine* rendererPtr;

    public:
        EngineStagingBuffer(RenderEngine* renderer);
        ~EngineStagingBuffer();
        
        void queueDataTransfers(const Buffer& dstBuffer, VkDeviceSize dstOffset, const std::vector<char>& data); //do not submit more than 1 transfer with the same destination! undefined behavior!
        void submitQueuedTransfers(SynchronizationInfo syncInfo); //Submits all queued transfers and clears the queue. Does not need to be explicitly synced with last transfer
    };

    struct FrameBeginSyncInfo
    {
        VkSemaphore imageSemaphore;
        TimelineSemaphorePair asSignaledSemaphore; //value corresponds to the signaled value, which is the same value that should be waited on
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
        AccelerationStructureBuilder asBuilder;
        EngineStagingBuffer stagingBuffer;

        std::string shadersDir;
        RendererState rendererState = {};

        //frame rendering stuff
        std::vector<CommandBuffer> usedCmdBuffers;
        std::vector<ModelInstance*> renderingModelInstances;
        std::deque<ModelInstance*> toUpdateModelInstances; //queued instance references that need to have their data in GPU buffers updated
        std::vector<Model*> renderingModels;
        std::deque<Model*> toUpdateModels; //queued model references that need to have their data in GPU buffers updated
        VkSemaphore asWaitSemaphore;
        VkSemaphore asSignalSemaphore;
        uint64_t asSemaphoreValue = 0;

        //render passes and acceleration structures
        std::list<TLAS*> tlAccelerationStructures;
        std::list<RenderPass*> renderPasses;

        //----------BUFFERS----------//

        const float instancesDataOverhead = 1.4f;
        const float modelsDataOverhead = 1.2f;
        std::unique_ptr<Buffer> instancesDataBuffer;
        std::unique_ptr<FragmentableBuffer> modelDataBuffer;
        
        void rebuildInstancesbuffer();
        void rebuildModelDataBuffer();
        void handleModelDataCompaction(std::vector<CompactionResult> results);

        //----------MODEL AND INSTANCE FUNCTIONS----------//

        void addModelData(Model* model);
        void removeModelData(Model* model);
        void addObject(ModelInstance* object);
        void removeObject(ModelInstance* object);
        void queueModelsAndInstancesTransfers();

        //----------MISC----------//

        friend Model;
        friend ModelInstance;
        friend RenderPass;
        friend RasterPreprocessPipeline;
        friend TLAS;
        friend AccelerationStructureBuilder;
        friend TLASInstanceBuildPipeline;

        uint32_t bufferIndex = 0;
        uint64_t frameNumber = 0;
    public:
        RenderEngine(RendererCreationStruct creationInfo);
        ~RenderEngine();
        RenderEngine(const RenderEngine&) = delete;

        //returns the image acquire semaphore from the swapchain
        FrameBeginSyncInfo beginFrame(SynchronizationInfo syncInfo);
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
        Buffer* getModelDataBuffer() const { return modelDataBuffer->getBuffer(); }
    };
}