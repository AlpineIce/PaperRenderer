#pragma once
#include "RHI/Swapchain.h"
#include "RHI/Pipeline.h"
#include "RHI/IndirectDrawBuffer.h"
#include "Camera.h"
#include "Material.h"

#include <list>
#include <unordered_map>

namespace Renderer
{
    //leaf node including the mesh and any uniforms/push constants to be set for rendering. 
    //Ownership should be within actors, not the render tree, which includes a pointer instead

    //node for objects corresponding to one material
    struct MaterialNode
    {
        Material const* material;
        std::shared_ptr<IndirectDrawBuffer> objectBuffer;
    };

    //node for materials corresponding to one pipeline
    struct PipelineNode
    {
        std::shared_ptr<RasterPipeline> pipeline;
        std::unordered_map<std::string, MaterialNode> materials; //string value for material name
    };

    class RenderPass
    {
    private:
        std::vector<VkSemaphore> imageSemaphores;
        std::vector<std::list<std::shared_ptr<QueueReturn>>> preRenderFences;
        std::vector<std::list<std::shared_ptr<QueueReturn>>> renderFences;
        std::vector<std::shared_ptr<UniformBuffer>> globalUBOs;
        std::vector<GlobalDescriptor> uniformDatas;
        uint32_t currentImage;
        bool recreateFlag = false;

        Swapchain* swapchainPtr;
        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;
        Pipeline const* pipeline = NULL; //changes per frame
        DescriptorAllocator* descriptorsPtr;
        Camera* cameraPtr = NULL;
        
        void checkSwapchain(VkResult imageResult);

    public:
        RenderPass(Swapchain* swapchain, Device* device, CmdBufferAllocator* commands, DescriptorAllocator* descriptors);
        ~RenderPass();

        //set camera
        void setCamera(Camera* camera) { this->cameraPtr = camera; }

        //start new frame
        VkCommandBuffer startNewFrame();

        //end frame
        void incrementFrameCounter(const VkCommandBuffer& cmdBuffer);
        
        //index render triangles on previously bound pipeline with specific material parameters
        void drawIndexed(const DrawBufferObject& objectData, const VkCommandBuffer& cmdBuffer);
        
        //indirect indexed rendering
        void drawIndexedIndirect(const VkCommandBuffer& cmdBuffer, IndirectDrawBuffer* drawBuffer);

        //change the rendering pipeline
        void bindPipeline(Pipeline const* pipeline, const VkCommandBuffer& cmdBuffer);

        //change the material
        void bindMaterial(Material const* material, const VkCommandBuffer& cmdBuffer);
    };
}