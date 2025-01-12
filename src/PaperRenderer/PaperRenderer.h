#pragma once

#include "Statistics.h"
#include "Device.h"
#include "Swapchain.h"
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
#include <array>

namespace PaperRenderer
{
    struct RendererCreationStruct
    {
        const std::function<void(RenderEngine&, const LogEvent&)>& logEventCallbackFunction = NULL;
        const std::function<void(RenderEngine&, VkExtent2D newExtent)>& swapchainRebuildCallbackFunction = NULL;
        const std::vector<uint32_t>& rasterPreprocessSpirv; //takes in compiled IndirectDrawBuild.comp spirv data
        const std::vector<uint32_t>& rtPreprocessSpirv; //takes in compiled TLASInstBuild.comp spirv data
        const DeviceInstanceInfo& deviceInstanceInfo = {};
        const WindowState& windowState = {};
    };

    //Dynamically resizing staging buffer to be used for most staging data operations used by the renderer (render passes, acceleration structures, instances, etc)
    class RendererStagingBuffer
    {
    private:
        std::recursive_mutex stagingBufferMutex;
        std::unique_ptr<Buffer> stagingBuffer;
        const float bufferOverhead = 1.5f;

        struct QueuedTransfer
        {
            VkDeviceSize dstOffset;
            std::vector<char> data;
        };

        std::unordered_map<Buffer const*, std::deque<QueuedTransfer>> transferQueues;
        VkDeviceSize queueSize = 0;
        VkDeviceSize stackLocation = 0;

        struct DstCopy
        {
            const Buffer& dstBuffer;
            VkBufferCopy copyInfo;
        };
        std::vector<DstCopy> getDataTransfers();

        class RenderEngine& renderer;

    public:
        RendererStagingBuffer(RenderEngine& renderer);
        ~RendererStagingBuffer();
        
        void idleBuffer();
        void queueDataTransfers(const Buffer& dstBuffer, VkDeviceSize dstOffset, const std::vector<char>& data); //do not submit more than 1 transfer with the same destination! undefined behavior!
        void submitQueuedTransfers(VkCommandBuffer cmdBuffer); //records all queued transfers and clears the queue
        const Queue& submitQueuedTransfers(SynchronizationInfo syncInfo); //Submits all queued transfers and clears the queue
        void addOwner(const Queue& queue) { if(stagingBuffer) stagingBuffer->addOwner(queue); }
    };

    //Render engine object. Contains the entire state of the renderer and some important buffers
    class RenderEngine
    {
    private:
        Logger logger;
        StatisticsTracker statisticsTracker;
        Device device;
        Swapchain swapchain;
        DescriptorAllocator descriptors;
        PipelineBuilder pipelineBuilder;
        RasterPreprocessPipeline rasterPreprocessPipeline;
        TLASInstanceBuildPipeline tlasInstanceBuildPipeline;
        AccelerationStructureBuilder asBuilder;
        std::array<std::unique_ptr<RendererStagingBuffer>, 2> stagingBuffer; //double buffered

        //frame rendering stuff
        std::vector<ModelInstance*> renderingModelInstances;
        std::deque<ModelInstance*> toUpdateModelInstances; //queued instance references that need to have their data in GPU buffers updated
        std::vector<Model*> renderingModels;
        std::deque<Model*> toUpdateModels; //queued model references that need to have their data in GPU buffers updated

        //----------BUFFERS----------//

        const float instancesDataOverhead = 1.4f;
        const float modelsDataOverhead = 1.2f;
        std::unique_ptr<Buffer> instancesDataBuffer;
        std::unique_ptr<FragmentableBuffer> modelDataBuffer;
        
        void rebuildInstancesbuffer();
        void rebuildModelDataBuffer();
        void handleModelDataCompaction(const std::vector<CompactionResult>& results);

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
        std::chrono::time_point<std::chrono::high_resolution_clock> lastFrameTimePoint;
        float deltaTime = 0.0000000000001f;
    public:
        RenderEngine(const RendererCreationStruct& creationInfo);
        ~RenderEngine();
        RenderEngine(const RenderEngine&) = delete;

        //returns the image acquire semaphore from the swapchain
        const VkSemaphore& beginFrame();
        void endFrame(const std::vector<VkSemaphore>& waitSemaphores); 

        uint32_t getBufferIndex() const { return frameNumber % 2; }
        uint64_t getFramesRenderedCount() const { return frameNumber; }
        float getDeltaTime() const { return deltaTime; } //returns in seconds
        Logger& getLogger() { return logger; }
        StatisticsTracker& getStatisticsTracker() { return statisticsTracker; }
        Device& getDevice() { return device; }
        RasterPreprocessPipeline& getRasterPreprocessPipeline() { return rasterPreprocessPipeline; }
        DescriptorAllocator& getDescriptorAllocator() { return descriptors; }
        PipelineBuilder& getPipelineBuilder() { return pipelineBuilder; }
        Swapchain& getSwapchain() { return swapchain; }
        RendererStagingBuffer& getStagingBuffer() { return *stagingBuffer[getBufferIndex()]; }
        AccelerationStructureBuilder& getAsBuilder() { return asBuilder; }
        const std::vector<Model*>& getModelReferences() const { return renderingModels; }
        const std::vector<ModelInstance*>& getModelInstanceReferences() const { return renderingModelInstances; }
        Buffer& getModelDataBuffer() const { return modelDataBuffer->getBuffer(); }
    };
}