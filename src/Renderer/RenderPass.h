#pragma once
#include "RHI/Swapchain.h"
#include "RHI/Pipeline.h"
#include "RHI/IndirectDrawBuffer.h"
#include "Camera.h"
#include "Renderer/Material/Material.h"

#include <list>
#include <unordered_map>

namespace Renderer
{
    //leaf node including the mesh and any uniforms/push constants to be set for rendering. 
    //Ownership should be within actors, not the render tree, which includes a pointer instead

    //node for objects corresponding to one material
    struct MaterialInstanceNode
    {
        std::shared_ptr<IndirectDrawContainer> objectBuffer;
    };

    //node for materials corresponding to one pipeline
    struct MaterialNode
    {
        std::unordered_map<MaterialInstance const*, MaterialInstanceNode> instances;
    };

    struct LightingInformation
    {
        std::list<PointLight const*> pointLights;
        DirectLight const* directLight = NULL; //this could be easily expaned to support multiple direct lights, but isnt needed
        AmbientLight const* ambientLight = NULL;
    };

    struct ImageAttachment
    {
        VkImage image;
        VkImageView view;
        VmaAllocation allocation;
    };

    struct ImageAttachments
    {
        //nothing for now
    };

    class RenderPass
    {
    private:
        std::vector<VkSemaphore> imageSemaphores;
        std::vector<VkSemaphore> stagingCopySemaphores;
        std::vector<VkFence> bufferCopyFences;
        std::vector<VkFence> stagingCopyFences;
        std::vector<VkSemaphore> cullingSemaphores;
        std::vector<VkSemaphore> bufferCopySemaphores;
        std::vector<VkSemaphore> renderSemaphores;
        std::vector<VkFence> renderFences;
        std::vector<std::vector<CommandBuffer>> commandBuffers;
        std::vector<std::shared_ptr<UniformBuffer>> lightingInfoBuffers;
        std::vector<std::shared_ptr<IndirectRenderingData>> renderingData;
        std::vector<std::shared_ptr<StorageBuffer>> dedicatedStagingData;

        std::shared_ptr<ComputePipeline> meshPreprocessPipeline;

        ImageAttachments renderTargets;
        uint32_t currentImage;
        bool recreateFlag = false;
        const uint32_t MAX_POINT_LIGHTS = 1024; //id rather not do a new allocation for every addition like for objects

        Swapchain* swapchainPtr;
        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;
        DescriptorAllocator* descriptorsPtr;
        PipelineBuilder* pipelineBuilderPtr;
        Camera* cameraPtr = NULL;
        
        void checkSwapchain(VkResult imageResult);
        void drawCallCull(const VkCommandBuffer& cmdBuffer, const CullingFrustum& cullingFrustum, StorageBuffer const* newBufferData, IndirectDrawContainer* drawBuffer);
        CommandBuffer submitCulling(const VkCommandBuffer& cmdBuffer);
        void composeAttachments(const VkCommandBuffer& cmdBuffer);
        void bindMaterial(Material const* material, const VkCommandBuffer& cmdBuffer);
        void bindMaterialInstance(MaterialInstance const* materialInstance, const VkCommandBuffer& cmdBuffer);
        void drawIndexedIndirect(const VkCommandBuffer& cmdBuffer, IndirectDrawContainer* drawBuffer);
        void incrementFrameCounter(const VkCommandBuffer& cmdBuffer);
        CullingFrustum createCullingFrustum();
        glm::vec4 normalizePlane(glm::vec4 plane);
        ImageAttachment createImageAttachment(VkFormat imageFormat);

    public:
        RenderPass(Swapchain* swapchain, Device* device, CmdBufferAllocator* commands, DescriptorAllocator* descriptors, PipelineBuilder* pipelineBuilder);
        ~RenderPass();

        //set camera
        void setCamera(Camera* camera) { this->cameraPtr = camera; }

        //start new frame
        void preProcessing(const std::unordered_map<Material*, MaterialNode>& renderTree, const LightingInformation& lightingInfo);

        //begin rendering
        void raster(const std::unordered_map<Material*, MaterialNode>& renderTree);
    };
}