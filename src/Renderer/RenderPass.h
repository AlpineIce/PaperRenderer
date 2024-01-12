#pragma once
#include "vulkan/vulkan.hpp"
#include "RHI/Swapchain.h"
#include "RHI/Pipeline.h"
#include "Camera.h"
#include "Material.h"

#include <list>
#include <unordered_map>

namespace Renderer
{
    //leaf node including the mesh and any uniforms/push constants to be set for rendering. 
    //Ownership should be within actors, not the render tree, which includes a pointer instead
    struct ObjectParameters
    {
        Mesh const* mesh;
        glm::mat4 const* modelMatrix; //should be calculated outside the renderer
    };

    //node for objects corresponding to one material
    struct MaterialNode
    {
        Material const* material;
        std::list<ObjectParameters> objects; //list because objects can be removed from any point at any time
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
        std::vector<VkSemaphore> renderSemaphores;
        std::vector<VkFence> renderingFences;
        UniformBuffer globalUBO;
        uint32_t currentImage;
        bool recreateFlag = false;

        Swapchain* swapchainPtr;
        Device* devicePtr;
        Commands* commandsPtr;
        Pipeline const* pipeline = NULL; //changes per frame
        DescriptorAllocator* descriptorsPtr;
        Camera* cameraPtr = NULL;
        
        void checkSwapchain(VkResult imageResult);

    public:
        RenderPass(Swapchain* swapchain, Device* device, Commands* commands, DescriptorAllocator* descriptors);
        ~RenderPass();

        //set camera
        void setCamera(Camera* camera) { this->cameraPtr = camera; }

        //start new frame
        void startNewFrame();

        //end frame
        void incrementFrameCounter();
        
        //index render triangles on previously bound pipeline with specific material parameters
        void drawIndexed(const ObjectParameters& objectData);

        //change the rendering pipeline
        void bindPipeline(Pipeline const* pipeline);

        //change the material
        void bindMaterial(Material const* material);
    };
}