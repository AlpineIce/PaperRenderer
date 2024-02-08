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
        std::shared_ptr<IndirectDrawBuffer> objectBuffer;
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
        std::vector<VkSemaphore> lightBufferCopySemaphores;
        std::vector<VkSemaphore> cullingSemaphores;
        std::vector<VkSemaphore> imageSemaphores;
        std::vector<std::list<std::shared_ptr<QueueReturn>>> cullingFences;
        std::vector<std::list<std::shared_ptr<QueueReturn>>> renderFences;
        std::vector<std::shared_ptr<UniformBuffer>> globalInfoBuffers;
        std::vector<std::shared_ptr<StorageBuffer>> pointLightsBuffers;
        std::vector<std::shared_ptr<UniformBuffer>> lightingInfoBuffers;
        std::vector<CameraData> globalBufferDatas;
        GlobalUniforms globalUniformData; //only need one per frame

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
        glm::vec4 normalizePlane(glm::vec4 plane);
        ImageAttachment createImageAttachment(VkFormat imageFormat);

    public:
        RenderPass(Swapchain* swapchain, Device* device, CmdBufferAllocator* commands, DescriptorAllocator* descriptors, PipelineBuilder* pipelineBuilder);
        ~RenderPass();

        //set camera
        void setCamera(Camera* camera) { this->cameraPtr = camera; }

        //start new frame
        VkCommandBuffer preProcessing(const LightingInformation& lightingInfo);

        //creates a frustum from current view matrix
        CullingFrustum createCullingFrustum();

        //cull draw calls
        void drawCallCull(const VkCommandBuffer& cmdBuffer, const CullingFrustum& frustumData, IndirectDrawBuffer* drawBuffer);

        //submit culling commands
        void submitCulling(const VkCommandBuffer& cmdBuffer);

        //begin rendering
        VkCommandBuffer beginRendering();

        void composeAttachments(const VkCommandBuffer& cmdBuffer);

        //end frame
        void incrementFrameCounter(const VkCommandBuffer& cmdBuffer);
        
        //indirect indexed rendering
        void drawIndexedIndirect(const VkCommandBuffer& cmdBuffer, IndirectDrawBuffer* drawBuffer);

        //change the rendering pipeline
        void bindMaterial(Material const* material, const VkCommandBuffer& cmdBuffer);

        //change the material
        void bindMaterialInstance(MaterialInstance const* materialInstance, const VkCommandBuffer& cmdBuffer);
    };
}