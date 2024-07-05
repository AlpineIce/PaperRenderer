#pragma once
#include "RHI/Swapchain.h"
#include "RHI/Pipeline.h"
#include "RHI/IndirectDraw.h"
#include "Camera.h"
#include "ComputeShader.h"
#include "Material.h"
#include "Model.h"

namespace PaperRenderer
{
    //----------PREPROCESS COMPUTE PIPELINE----------//

    class RasterPreprocessPipeline : public ComputeShader
    {
    private:
        std::string fileName = "IndirectDrawBuild.spv";
        std::vector<std::unique_ptr<PaperMemory::Buffer>> uniformBuffers;
        std::unique_ptr<PaperMemory::DeviceAllocation> uniformBuffersAllocation;

        struct UBOInputData
        {
            glm::vec4 camPos;
            glm::mat4 projection;
            glm::mat4 view;
            VkDeviceAddress materialDataPtr;
            VkDeviceAddress modelDataPtr;
            uint32_t objectCount;
            uint32_t frameIndex;
        };

        class RenderEngine* rendererPtr;

    public:
        RasterPreprocessPipeline(RenderEngine* renderer, std::string fileDir);
        ~RasterPreprocessPipeline() override;

        void submit(const PaperMemory::SynchronizationInfo& syncInfo, const RenderPass& renderPass);
    };
    
    //----------RENDER PASS----------//

    struct RenderPassInfo
    {
        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        VkRenderingAttachmentInfo const* depthAttachment = NULL;
        VkRenderingAttachmentInfo const* stencilAttachment = NULL;
        std::vector<VkViewport> viewports;
        std::vector<VkRect2D> scissors;
        VkRect2D renderArea = {};
        VkDependencyInfo const* preRenderBarriers = NULL;
        VkDependencyInfo const* postRenderBarriers = NULL;
    };

    struct RenderPassSynchronizationInfo
    {
        std::vector<PaperMemory::SemaphorePair> preprocessWaitPairs;
        std::vector<PaperMemory::SemaphorePair> renderWaitPairs;
        std::vector<PaperMemory::SemaphorePair> renderSignalPairs;
        VkFence renderSignalFence;
    };

    class RenderPass
    {
    private:
        //node for materials corresponding to one material
        struct MaterialNode
        {
            //objects corresponding to one material instance
            std::unordered_map<MaterialInstance*, std::unique_ptr<CommonMeshGroup>> instances;
        };
        std::unordered_map<Material*, MaterialNode> renderTree; //render tree

        float instancesOverhead = 1.5f;
        std::vector<ModelInstance*> renderPassInstances;
        
        //allocations
        static std::unique_ptr<PaperMemory::DeviceAllocation> hostInstancesAllocation;
        static std::unique_ptr<PaperMemory::DeviceAllocation> deviceInstancesAllocation;
        static std::list<RenderPass*> renderPasses;

        //buffers
        std::unique_ptr<PaperMemory::Buffer> hostInstancesBuffer;
        std::unique_ptr<PaperMemory::Buffer> deviceInstancesBuffer;

        std::unique_ptr<PaperMemory::FragmentableBuffer> hostInstancesDataBuffer;
        std::unique_ptr<PaperMemory::Buffer> deviceInstancesDataBuffer;

        static void rebuildAllocationsAndBuffers(RenderEngine* renderer);
        void rebuildBuffers(VkDeviceSize newMaterialDataBufferSize);
        void handleMaterialDataCompaction(std::vector<PaperMemory::CompactionResult> results);
        void handleCommonMeshGroupResize(std::vector<ModelInstance*> invalidInstances);
        void clearDrawCounts();

        //synchronization
        VkFence preprocessFence;
        std::vector<VkSemaphore> drawCountsClearSemaphores;
        std::vector<VkSemaphore> preprocessSignalSemaphores;
        std::vector<VkSemaphore> instancesBufferCopySemaphores;
        std::vector<VkSemaphore> materialDataBufferCopySemaphores;

        //default materials
        Material* defaultMaterial = NULL;
        MaterialInstance* defaultMaterialInstance = NULL;

        RenderEngine* rendererPtr;
        Camera* cameraPtr;
        Material* defaultMaterialPtr;
        MaterialInstance* defaultMaterialInstancePtr;
        RenderPassInfo const* renderPassInfoPtr;

        friend RasterPreprocessPipeline;
        
    public:
        RenderPass(RenderEngine* renderer, Camera* camera, Material* defaultMaterial, MaterialInstance* defaultMaterialInstance, RenderPassInfo const* renderPassInfo);
        ~RenderPass();

        void render(const RenderPassSynchronizationInfo& syncInfo);

        void addInstance(ModelInstance* instance, std::vector<std::unordered_map<uint32_t, MaterialInstance*>> materials);
        void removeInstance(ModelInstance* instance);

        void setDefaultMaterial(Material* material) { this->defaultMaterial = material; }
        void setDefaultMaterialInstance(MaterialInstance* materialInstance) { this->defaultMaterialInstance = materialInstance; }
    };
}