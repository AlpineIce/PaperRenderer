#pragma once
#include "RHI/Swapchain.h"
#include "RHI/Pipeline.h"
#include "RHI/IndirectDrawBuffer.h"
#include "RHI/AccelerationStructure.h"
#include "Camera.h"
#include "Renderer/Material/Material.h"
#include "Model.h"

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
        std::vector<VkSemaphore> bufferCopySemaphores;
        std::vector<VkSemaphore> tlasBuildSemaphores;
        std::vector<VkSemaphore> preprocessSemaphores;
        std::vector<VkSemaphore> preprocessTLASSignalSemaphores;
        std::vector<VkSemaphore> renderSemaphores;
        std::vector<VkFence> BLASFences;
        std::vector<VkFence> renderFences;
        std::vector<std::vector<CommandBuffer>> fenceCmdBuffers;
        std::vector<std::shared_ptr<UniformBuffer>> lightingInfoBuffers;
        std::vector<std::shared_ptr<IndirectRenderingData>> renderingData;
        std::vector<std::shared_ptr<UniformBuffer>> preprocessUniformBuffers;
        std::unordered_map<Model*, std::list<ModelInstance*>> renderingModels;

        std::shared_ptr<ComputePipeline> meshPreprocessPipeline;
        
        AccelerationStructure rtAccelStructure;
        ImageAttachments renderTargets;
        uint32_t currentImage;
        bool recreateFlag = false;

        Swapchain* swapchainPtr;
        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;
        DescriptorAllocator* descriptorsPtr;
        PipelineBuilder* pipelineBuilderPtr;
        Camera* cameraPtr = NULL;
        
        bool preProcessing(const std::unordered_map<Material*, MaterialNode>& renderTree, const LightingInformation& lightingInfo);
        void raster(const std::unordered_map<Material*, MaterialNode>& renderTree);
        void checkSwapchain(VkResult imageResult);
        void setStagingData(const std::unordered_map<Material*, MaterialNode>& renderTree, const LightingInformation& lightingInfo);
        void traceRays();
        CommandBuffer submitPreprocess();
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

        //draw new frame
        void drawAll(const  std::unordered_map<Material*, MaterialNode>& renderTree, const LightingInformation& lightingInfo);

        std::list<ModelInstance*>::iterator addModelInstance(ModelInstance* instance);
        void removeModelInstance(std::list<ModelInstance*>::iterator reference);
    };
}