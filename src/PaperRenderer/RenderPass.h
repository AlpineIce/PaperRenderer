#pragma once
#include "Swapchain.h"
#include "Pipeline.h"
#include "IndirectDraw.h"
#include "Camera.h"
#include "ComputeShader.h"
#include "Material.h"
#include "Model.h"

#include <variant>

namespace PaperRenderer
{
    //----------PREPROCESS COMPUTE PIPELINE----------//

    class RasterPreprocessPipeline
    {
    private:
        VkDescriptorSetLayout uboSetLayout;
        VkDescriptorSetLayout ioSetLayout;
        Shader shader;
        ComputeShader computeShader;

        class RenderEngine& renderer;

    public:
        RasterPreprocessPipeline(RenderEngine& renderer, const std::vector<uint32_t>& shaderData);
        ~RasterPreprocessPipeline();
        RasterPreprocessPipeline(const RasterPreprocessPipeline&) = delete;

        struct UBOInputData
        {
            VkDeviceAddress materialDataPtr = 0;
            VkDeviceAddress modelDataPtr = 0;
            uint32_t objectCount = 0;
            bool doCulling = true;
            float padding[9];
        };

        void submit(VkCommandBuffer cmdBuffer, const RenderPass& renderPass, const Camera& camera);

        const VkDescriptorSetLayout& getUboDescriptorLayout() const { return uboSetLayout; }
        const VkDescriptorSetLayout& getIODescriptorLayout() const { return ioSetLayout; }
    };
    
    //----------RENDER PASS----------//

    enum RenderPassSortMode
    {
        DONT_CARE,
        FRONT_FIRST,
        BACK_FIRST
    };
    
    struct RenderPassInfo
    {
        const Camera& camera;
        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        VkRenderingAttachmentInfo const* depthAttachment = NULL;
        VkRenderingAttachmentInfo const* stencilAttachment = NULL;
        std::vector<VkViewport> viewports;
        std::vector<VkRect2D> scissors;
        VkRect2D renderArea = {};
        VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
        VkDependencyInfo const* preRenderBarriers = NULL; //applied before data transfers, preprocess and render pass
        VkDependencyInfo const* postRenderBarriers = NULL; //applied after render pass
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
        RenderPassSortMode sortMode = BACK_FIRST; //rendering order for instances that were added with the sort set to true
    };

    class RenderPass
    {
    private:
        //render tree and sorted instances
        std::unordered_map<Material*, std::unordered_map<MaterialInstance*, CommonMeshGroup>> renderTree; //render tree
        struct SortedInstance
        {
            ModelInstance* instance;
            std::vector<std::unordered_map<uint32_t, MaterialInstance*>> materials;
        };
        std::vector<SortedInstance> renderPassSortedInstances;

        float instancesOverhead = 1.5f;
        std::vector<ModelInstance*> renderPassInstances; //doesn't included sorted
        std::deque<ModelInstance*> toUpdateInstances; //doesn't included sorted

        //buffers
        Buffer preprocessUniformBuffer;
        std::unique_ptr<Buffer> instancesBuffer;
        std::unique_ptr<Buffer> sortedInstancesOutputBuffer;
        std::unique_ptr<FragmentableBuffer> instancesDataBuffer;

        //sync
        uint64_t transferSemaphoreValue = 0;
        VkSemaphore transferSemaphore;

        //descriptors
        enum RenderPassDescriptorIndices
        {
            UBO = 0,
            INSTANCES = 1,
            IO = 2,
            CAMERA = 3,
            SORTED_MATRICES = 4
        };
        ResourceDescriptor uboDescriptor;
        ResourceDescriptor ioDescriptor;
        ResourceDescriptor sortedMatricesDescriptor;

        //functions
        void rebuildInstancesBuffer();
        void rebuildSortedInstancesBuffer();
        void rebuildMaterialDataBuffer();
        void queueInstanceTransfers();
        void handleMaterialDataCompaction(const std::vector<CompactionResult>&);
        void handleCommonMeshGroupResize(std::vector<ModelInstance*> invalidInstances);
        void clearDrawCounts(VkCommandBuffer cmdBuffer);
        void assignResourceOwner(const Queue& queue);

        RenderEngine& renderer;
        MaterialInstance& defaultMaterialInstance;

        friend RasterPreprocessPipeline;
        
    public:
        RenderPass(RenderEngine& renderer, MaterialInstance& defaultMaterialInstance);
        ~RenderPass();
        RenderPass(const RenderPass&) = delete;

        const Queue& render(const RenderPassInfo& renderPassInfo, SynchronizationInfo syncInfo);

        void addInstance(ModelInstance& instance, std::vector<std::unordered_map<uint32_t, MaterialInstance*>> materials, bool sorted=false);
        void removeInstance(ModelInstance& instance);
    };
}