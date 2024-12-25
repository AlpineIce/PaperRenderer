#pragma once
#include "Swapchain.h"
#include "Pipeline.h"
#include "IndirectDraw.h"
#include "Camera.h"
#include "ComputeShader.h"
#include "Material.h"
#include "Model.h"

namespace PaperRenderer
{
    //----------PREPROCESS COMPUTE PIPELINE----------//

    class RasterPreprocessPipeline
    {
    private:
        const ComputeShader computeShader;

        class RenderEngine& renderer;

    public:
        RasterPreprocessPipeline(RenderEngine& renderer, const std::vector<uint32_t>& shaderData);
        ~RasterPreprocessPipeline();
        RasterPreprocessPipeline(const RasterPreprocessPipeline&) = delete;

        struct UBOInputData
        {
            glm::vec4 camPos = glm::vec4(0.0f);
            glm::mat4 projection = glm::mat4(1.0f);
            glm::mat4 view = glm::mat4(1.0f);
            VkDeviceAddress materialDataPtr = 0;
            VkDeviceAddress modelDataPtr = 0;
            uint32_t objectCount = 0;
            bool doCulling = true;
        };

        void submit(VkCommandBuffer cmdBuffer, const RenderPass& renderPass, const Camera& camera);
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
        std::unordered_map<ModelInstance*, std::vector<std::unordered_map<uint32_t, MaterialInstance*>>> sortedInstances; //unordered map is probably slow but how much translucency you need?

        float instancesOverhead = 1.5f;
        std::vector<ModelInstance*> renderPassInstances; //doesn't included sorted
        std::deque<ModelInstance*> toUpdateInstances; //doesn't included sorted

        //buffers
        Buffer preprocessUniformBuffer;
        std::unique_ptr<Buffer> instancesBuffer;
        std::unique_ptr<Buffer> sortedInstancesOutputBuffer;
        std::unique_ptr<FragmentableBuffer> instancesDataBuffer;

        void rebuildInstancesBuffer();
        void rebuildSortedInstancesBuffer();
        void rebuildMaterialDataBuffer();
        void handleMaterialDataCompaction(std::vector<CompactionResult> results);
        void handleCommonMeshGroupResize(std::vector<ModelInstance*> invalidInstances);
        void clearDrawCounts(VkCommandBuffer cmdBuffer);

        RenderEngine& renderer;
        MaterialInstance& defaultMaterialInstance;

        friend RasterPreprocessPipeline;
        
    public:
        RenderPass(RenderEngine& renderer, MaterialInstance& defaultMaterialInstance);
        ~RenderPass();
        RenderPass(const RenderPass&) = delete;

        void queueInstanceTransfers();
        std::vector<VkCommandBuffer> render(const RenderPassInfo& renderPassInfo); //returns list of GRAPHICS command buffers, in order, to submit

        void addInstance(ModelInstance& instance, std::vector<std::unordered_map<uint32_t, MaterialInstance*>> materials, bool sorted=false);
        void removeInstance(ModelInstance& instance);
    };
}