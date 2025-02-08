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
#include "StagingBuffer.h"

#include <string>
#include <memory>
#include <vector>
#include <deque>
#include <queue>
#include <array>

namespace PaperRenderer
{
    //Default descriptors
    enum DefaultDescriptors
    {
        INDIRECT_DRAW_MATRICES = 0,
        CAMERA_MATRICES = 1,
        TLAS_INSTANCE_DESCRIPTIONS = 2,
        INSTANCES = 3
    };

    //struct for RenderEngine
    struct PaperRendererInfo
    {
        std::function<void(RenderEngine&, const LogEvent&)> logEventCallbackFunction = NULL;
        std::function<void(RenderEngine&, VkExtent2D newExtent)> swapchainRebuildCallbackFunction = NULL;
        std::vector<uint32_t> rasterPreprocessSpirv {}; //takes in compiled IndirectDrawBuild.comp spirv data
        std::vector<uint32_t> rtPreprocessSpirv {}; //takes in compiled TLASInstBuild.comp spirv data
        DeviceInstanceInfo deviceInstanceInfo = {};
        WindowState windowState = {};
    };
    
    //main renderer class
    class RenderEngine
    {
    private:
        Logger logger;
        StatisticsTracker statisticsTracker;
        Device device;
        Swapchain swapchain;
        DescriptorAllocator descriptors;
        PipelineBuilder pipelineBuilder;
        std::array<VkDescriptorSetLayout, 4> defaultDescriptorLayouts;
        RasterPreprocessPipeline rasterPreprocessPipeline;
        TLASInstanceBuildPipeline tlasInstanceBuildPipeline;
        AccelerationStructureBuilder asBuilder;
        std::array<std::unique_ptr<RendererStagingBuffer>, 2> stagingBuffer; //double buffered

        //renderer descriptors
        ResourceDescriptor instancesBufferDescriptor;

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
        RenderEngine(const PaperRendererInfo& creationInfo);
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
        TLASInstanceBuildPipeline& getTLASPreprocessPipeline() { return tlasInstanceBuildPipeline; }
        DescriptorAllocator& getDescriptorAllocator() { return descriptors; }
        PipelineBuilder& getPipelineBuilder() { return pipelineBuilder; }
        Swapchain& getSwapchain() { return swapchain; }
        RendererStagingBuffer& getStagingBuffer() { return *stagingBuffer[getBufferIndex()]; }
        AccelerationStructureBuilder& getAsBuilder() { return asBuilder; }
        const std::vector<Model*>& getModelReferences() const { return renderingModels; }
        const std::vector<ModelInstance*>& getModelInstanceReferences() const { return renderingModelInstances; }
        Buffer& getModelDataBuffer() const { return modelDataBuffer->getBuffer(); }
        const VkDescriptorSetLayout& getDefaultDescriptorSetLayout(const DefaultDescriptors descriptor) const { return defaultDescriptorLayouts[descriptor]; }
        const ResourceDescriptor& getInstancesBufferDescriptor() const { return instancesBufferDescriptor; }
    };
}