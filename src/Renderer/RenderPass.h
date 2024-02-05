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
        std::vector<std::list<std::shared_ptr<QueueReturn>>> preRenderFences;
        std::vector<std::list<std::shared_ptr<QueueReturn>>> renderFences;
        std::vector<std::shared_ptr<UniformBuffer>> globalUBOs;
        std::vector<GlobalDescriptor> uniformDatas;
        ImageAttachments renderTargets;
        uint32_t currentImage;
        bool recreateFlag = false;

        Swapchain* swapchainPtr;
        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;
        Pipeline const* pipeline = NULL; //changes per frame
        DescriptorAllocator* descriptorsPtr;
        Camera* cameraPtr = NULL;
        
        void checkSwapchain(VkResult imageResult);
        ImageAttachment createImageAttachment(VkFormat imageFormat);

    public:
        RenderPass(Swapchain* swapchain, Device* device, CmdBufferAllocator* commands, DescriptorAllocator* descriptors);
        ~RenderPass();

        //set camera
        void setCamera(Camera* camera) { this->cameraPtr = camera; }

        //start new frame
        VkCommandBuffer startNewFrame();

        void composeAttachments(const VkCommandBuffer& cmdBuffer);

        //end frame
        void incrementFrameCounter(const VkCommandBuffer& cmdBuffer);
        
        //index render triangles on previously bound pipeline with specific material parameters
        void drawIndexed(const DrawBufferObject& objectData, const VkCommandBuffer& cmdBuffer);
        
        //indirect indexed rendering
        void drawIndexedIndirect(const VkCommandBuffer& cmdBuffer, IndirectDrawBuffer* drawBuffer);

        //change the rendering pipeline
        void bindMaterial(Material const* material, const VkCommandBuffer& cmdBuffer);

        //change the material
        void bindMaterialInstance(MaterialInstance const* materialInstance, const VkCommandBuffer& cmdBuffer);
    };
}