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
        //takes in compiled IndirectDrawBuild.comp spirv data
        const std::vector<uint32_t>& rasterPreprocessSpirv;
        //takes in compiled TLASInstBuild.comp spirv data
        const std::vector<uint32_t>& rtPreprocessSpirv;
        const WindowState& windowState = {};
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

        struct DstCopy
        {
            const Buffer& dstBuffer;
            VkBufferCopy copyInfo;
        };
        std::vector<DstCopy> getDataTransfers();

        class RenderEngine& renderer;

    public:
        EngineStagingBuffer(RenderEngine& renderer);
        ~EngineStagingBuffer();
        
        void queueDataTransfers(const Buffer& dstBuffer, VkDeviceSize dstOffset, const std::vector<char>& data); //do not submit more than 1 transfer with the same destination! undefined behavior!
        void submitQueuedTransfers(SynchronizationInfo syncInfo); //Submits all queued transfers and clears the queue. Does not need to be explicitly synced with last transfer
        void submitQueuedTransfers(VkCommandBuffer cmdBuffer); //submits all queued transfers and clears queue, but takes command buffer as parameter
        TimelineSemaphorePair getTransferSemaphore() const { return { transferSemaphore, VK_PIPELINE_STAGE_2_TRANSFER_BIT, finalSemaphoreValue }; }
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

        RendererState rendererState = {};

        //frame rendering stuff
        std::vector<ModelInstance*> renderingModelInstances;
        std::deque<ModelInstance*> toUpdateModelInstances; //queued instance references that need to have their data in GPU buffers updated
        std::vector<Model*> renderingModels;
        std::deque<Model*> toUpdateModels; //queued model references that need to have their data in GPU buffers updated

        //render passes
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
        friend RayTraceRender;
        friend RasterPreprocessPipeline;
        friend TLAS;
        friend AccelerationStructureBuilder;
        friend TLASInstanceBuildPipeline;

        uint64_t frameNumber = 0;
    public:
        RenderEngine(const RendererCreationStruct& creationInfo);
        ~RenderEngine();
        RenderEngine(const RenderEngine&) = delete;

        //returns the image acquire semaphore from the swapchain
        const VkSemaphore& beginFrame(const SynchronizationInfo& transferSyncInfo, const SynchronizationInfo& asSyncInfo);
        void endFrame(const std::vector<VkSemaphore>& waitSemaphores); 

        uint32_t getBufferIndex() const { return frameNumber % 2; }
        uint64_t getFramesRenderedCount() const { return frameNumber; }
        Device& getDevice() { return device; }
        RasterPreprocessPipeline& getRasterPreprocessPipeline() { return rasterPreprocessPipeline; }
        DescriptorAllocator& getDescriptorAllocator() { return descriptors; }
        PipelineBuilder& getPipelineBuilder() { return pipelineBuilder; }
        RendererState& getRendererState() { return rendererState; }
        Swapchain& getSwapchain() { return swapchain; }
        EngineStagingBuffer& getEngineStagingBuffer() { return stagingBuffer; }
        const std::vector<Model*>& getModelReferences() const { return renderingModels; }
        const std::vector<ModelInstance*>& getModelInstanceReferences() const { return renderingModelInstances; }
        Buffer& getModelDataBuffer() const { return modelDataBuffer->getBuffer(); }
    };
}