#pragma once
#include "RHI/Swapchain.h"
#include "RHI/Pipeline.h"
#include "RHI/IndirectDrawBuffer.h"
#include "RHI/AccelerationStructure.h"
#include "Camera.h"
#include "Material.h"
#include "Model.h"

namespace PaperRenderer
{
    //leaf node including the mesh and any uniforms/push constants to be set for rendering. 
    //Ownership should be within actors, not the render tree, which includes a pointer instead

    //node for objects corresponding to one material
    struct MaterialInstanceNode
    {
        std::unique_ptr<IndirectDrawContainer> objectBuffer;
    };

    //node for materials corresponding to one pipeline
    struct MaterialNode
    {
        std::unordered_map<MaterialInstance const*, MaterialInstanceNode> instances;
    };

    struct ImageAttachments
    {
        //nothing for now
    };

    class RenderPass
    {
    private:
        //synchronization and commands
        std::vector<VkSemaphore> imageSemaphores;
        std::vector<VkSemaphore> bufferCopySemaphores;
        std::vector<VkSemaphore> BLASBuildSemaphores;
        std::vector<VkSemaphore> TLASBuildSemaphores;
        std::vector<VkSemaphore> rasterPreprocessSemaphores;
        std::vector<VkSemaphore> preprocessTLASSignalSemaphores;
        std::vector<VkSemaphore> renderSemaphores;
        std::vector<VkFence> RTFences;
        std::vector<VkFence> renderFences;
        std::vector<std::vector<PaperMemory::CommandBuffer>> usedCmdBuffers;
        
        //buffers and allocations
        std::vector<std::unique_ptr<PaperMemory::DeviceAllocation>> uniformBuffersAllocations;
        std::vector<std::unique_ptr<PaperMemory::Buffer>> preprocessUniformBuffers;
        std::vector<std::unique_ptr<PaperMemory::DeviceAllocation>> stagingAllocations; //took me forever to learn this needs to live more than the lifetime of a function body... i love buffer addresses
        std::vector<std::unique_ptr<PaperMemory::Buffer>> newDataStagingBuffers;

        //device local rendering buffer and misc data
        std::vector<IndirectRenderingData> renderingData; //includes its own device local allocation, but needs staging allocation for access
        
        std::unordered_map<Model*, std::vector<ModelInstance*>> renderingModels;

        std::unique_ptr<ComputePipeline> meshPreprocessPipeline;
        
        AccelerationStructure rtAccelStructure;
        uint32_t currentImage;
        bool recreateFlag = false;

        Swapchain* swapchainPtr;
        Device* devicePtr;
        DescriptorAllocator* descriptorsPtr;
        PipelineBuilder* pipelineBuilderPtr;
        Camera* cameraPtr = NULL;
        
        //helper functions
        CullingFrustum createCullingFrustum();
        glm::vec4 normalizePlane(glm::vec4 plane);
        void rebuildRenderDataAllocation(uint32_t currentFrame);
        
        //frame rendering functions
        void frameBegin(const std::unordered_map<Material *, MaterialNode> &renderTree);
        void raster(const std::unordered_map<Material*, MaterialNode>& renderTree);
        void setRasterStagingData(const std::unordered_map<Material*, MaterialNode>& renderTree);
        void setRTStagingData(const std::unordered_map<Material*, MaterialNode>& renderTree);
        void copyStagingData();
        void rasterPreProcess(const std::unordered_map<Material*, MaterialNode>& renderTree);
        void rayTracePreProcess(const std::unordered_map<Material*, MaterialNode>& renderTree);
        void frameEnd(VkSemaphore waitSemaphore);

    public:
        RenderPass(Swapchain* swapchain, Device* device, DescriptorAllocator* descriptors, PipelineBuilder* pipelineBuilder);
        ~RenderPass();

        //set camera
        void setCamera(Camera* camera) { this->cameraPtr = camera; }

        //draw new frame
        void rasterOrTrace(bool shouldRaster, const std::unordered_map<Material *, MaterialNode> &renderTree);

        void addModelInstance(ModelInstance* instance, uint64_t& selfIndex);
        void removeModelInstance(ModelInstance* instance, uint64_t& reference);
    };
}