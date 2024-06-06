#pragma once
#include "RHI/Swapchain.h"
#include "RHI/Pipeline.h"
#include "RHI/IndirectDraw.h"
#include "RHI/AccelerationStructure.h"
#include "Camera.h"
#include "ComputeShader.h"
#include "Material.h"
#include "Model.h"

namespace PaperRenderer
{
    struct IndirectRenderingData
    {
        uint32_t objectCount;
        VkBufferCopy inputObjectsRegion; //not actually used for a copy, but is used for storing input objects size and location
        std::vector<char> stagingData;
        std::unique_ptr<PaperMemory::DeviceAllocation> bufferAllocation;
        std::unique_ptr<PaperMemory::Buffer> bufferData; //THE UBER-BUFFER
    };

    //----------PREPROCESS COMPUTE PIPELINES----------//

    class RasterPreprocessPipeline : public ComputeShader
    {
    private:
        std::string fileName = "IndirectDrawBuild.spv";
        std::vector<std::unique_ptr<PaperMemory::Buffer>> uniformBuffers;
        std::unique_ptr<PaperMemory::DeviceAllocation> uniformBuffersAllocation;

        struct UBOInputData
        {
            VkDeviceAddress bufferAddress; //used with offsets to make LOD selection possible in a compute shader
            uint64_t padding;
            glm::vec4 camPos;
            glm::mat4 projection;
            glm::mat4 view;
            CameraFrustum frustumData;
            uint32_t objectCount;
        };

    public:
        RasterPreprocessPipeline(std::string fileDir);
        ~RasterPreprocessPipeline() override;

        PaperMemory::CommandBuffer submit(Camera* camera, const IndirectRenderingData& renderingData, uint32_t currentImage, PaperMemory::SynchronizationInfo syncInfo);
    };
    
    //----------RENDER PASS----------//

    class OldRenderPass
    {
    private:
        //synchronization and commands
        std::vector<VkSemaphore> bufferCopySemaphores;
        std::vector<VkFence> bufferCopyFences;
        std::vector<VkSemaphore> rasterPreprocessSemaphores;
        std::vector<VkSemaphore> preprocessTLASSignalSemaphores;
        std::vector<VkSemaphore> renderSemaphores;
        std::vector<VkFence> renderFences;
        
        //buffers and allocations
        std::vector<std::unique_ptr<PaperMemory::DeviceAllocation>> stagingAllocations;
        std::vector<std::unique_ptr<PaperMemory::Buffer>> newDataStagingBuffers;

        //compute shaders
        std::unique_ptr<RasterPreprocessPipeline> rasterPreprocessPipeline;

        //device local rendering buffer and misc data
        std::vector<IndirectRenderingData> renderingData; //includes its own device local allocation, but needs staging allocation for access

        Swapchain* swapchainPtr;
        Device* devicePtr;
        DescriptorAllocator* descriptorsPtr;
        PipelineBuilder* pipelineBuilderPtr;
        
        //helper functions
        void rebuildRenderDataAllocation(uint32_t currentFrame);
        
        //frame rendering functions
        void raster(std::unordered_map<Material*, MaterialNode>& renderTree);
        void setRasterStagingData(const std::unordered_map<Material*, MaterialNode>& renderTree);
        void rasterPreProcess(const std::unordered_map<Material*, MaterialNode>& renderTree);
        void rayTracePreProcess(const std::unordered_map<Material*, MaterialNode>& renderTree);

    public:
        OldRenderPass(Swapchain* swapchain, Device* device, DescriptorAllocator* descriptors, PipelineBuilder* pipelineBuilder);
        ~OldRenderPass();

        void addModelInstance(ModelInstance* instance, uint64_t& selfIndex);
        void removeModelInstance(ModelInstance* instance, uint64_t& reference);
    };

    struct RenderPassInfo
    {
        
    };

    class RenderPass
    {
    protected:
        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        VkRenderingAttachmentInfo const* depthAttachment = NULL;
        VkRenderingAttachmentInfo const* stencilAttachment = NULL;
        std::vector<VkViewport> viewports = {};
        std::vector<VkRect2D> scissors = {};
        VkRect2D renderArea = {};
    public:
        RenderPass();
        virtual ~RenderPass();

        virtual void render(VkCommandBuffer cmdBuffer);
    };
}