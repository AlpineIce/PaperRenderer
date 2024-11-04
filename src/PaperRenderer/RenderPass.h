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

    class RasterPreprocessPipeline : public ComputeShader
    {
    private:
        std::string fileName = "IndirectDrawBuild.spv";
        std::unique_ptr<Buffer> uniformBuffer;

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

        class RenderEngine& renderer;

    public:
        RasterPreprocessPipeline(RenderEngine& renderer, std::string fileDir);
        ~RasterPreprocessPipeline() override;
        RasterPreprocessPipeline(const RasterPreprocessPipeline&) = delete;

        void submit(VkCommandBuffer cmdBuffer, const RenderPass& renderPass);
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
        VkDependencyInfo const* preRenderBarriers = NULL; //applied before data transfers, preprocess and render pass
        VkDependencyInfo const* postRenderBarriers = NULL; //applied after render pass
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
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
        std::deque<ModelInstance*> toUpdateInstances;

        //buffers
        std::unique_ptr<Buffer> instancesBuffer;
        std::unique_ptr<FragmentableBuffer> instancesDataBuffer;

        void rebuildInstancesBuffer();
        void rebuildMaterialDataBuffer();
        void handleMaterialDataCompaction(std::vector<CompactionResult> results);
        void handleCommonMeshGroupResize(std::vector<ModelInstance*> invalidInstances);
        void clearDrawCounts(VkCommandBuffer cmdBuffer);

        RenderEngine& renderer;
        Camera* cameraPtr;
        MaterialInstance* defaultMaterialInstancePtr;

        friend RasterPreprocessPipeline;
        
    public:
        RenderPass(RenderEngine& renderer, Camera* camera, MaterialInstance* defaultMaterialInstance);
        ~RenderPass();
        RenderPass(const RenderPass&) = delete;

        void queueInstanceTransfers();
        void render(VkCommandBuffer cmdBuffer, const RenderPassInfo& renderPassInfo);
        std::vector<uint32_t> readInstanceCounts(); //BRUTE FORCE SYNC DEBUG FUNCTION

        void addInstance(ModelInstance* instance, std::vector<std::unordered_map<uint32_t, MaterialInstance*>> materials);
        void removeInstance(ModelInstance* instance);
    };
}