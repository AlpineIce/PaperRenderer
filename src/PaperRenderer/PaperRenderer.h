#pragma once
#include "GLFW/glfw3.h"

#include "RHI/Device.h"
#include "RHI/Memory/VulkanResources.h"
#include "RHI/Window.h"
#include "RenderPass.h"

#include <string>
#include <memory>
#include <vector>

namespace PaperRenderer
{
    struct RendererCreationStruct
    {
        std::string appName;
        unsigned int resX;
        unsigned int resY;
    };

    class RenderEngine
    {
    private:
        Device device;
        Window window;
        Swapchain swapchain;
        DescriptorAllocator descriptors;
        PipelineBuilder pipelineBuilder;
        RenderPass rendering;
        Camera* cameraPtr = NULL;

        std::string appName;
        bool rtEnabled = false;

        //frame rendering stuff
        std::vector<VkFence> bufferCopyFences;
        std::vector<VkSemaphore> imageSemaphores;
        std::vector<std::vector<PaperMemory::CommandBuffer>> usedCmdBuffers;

        //----------RENDER TREE----------//

        //node for objects corresponding to one material instance
        struct MaterialInstanceNode
        {
            std::unique_ptr<CommonMeshGroup> meshGroups;
        };

        //node for materials corresponding to one material
        struct MaterialNode
        {
            std::unordered_map<MaterialInstance*, MaterialInstanceNode> instances;
        };

        std::unordered_map<Material*, MaterialNode> renderTree;
        std::vector<ModelInstance*> renderingModels;

        DefaultMaterial defaultMaterial;
        DefaultMaterialInstance defaultMaterialInstance;

        //host visible version; acts like a staging buffer; can theoretically be modified throughout rendering process with a read/write race hazard
        std::unique_ptr<PaperMemory::DeviceAllocation> hostInstancesDataAllocation;
        std::unique_ptr<PaperMemory::Buffer> hostInstancesDataBuffer;

        //device local version; updated once per frame from host visible version
        std::unique_ptr<PaperMemory::DeviceAllocation> deviceInstancesDataAllocation;
        std::unique_ptr<PaperMemory::Buffer> deviceInstancesDataBuffer;

        void rebuildInstancesbuffers();

        uint32_t currentImage = 0;
    public:
        RenderEngine(RendererCreationStruct creationInfo);
        ~RenderEngine();

        //add/remove objects to render tree
        void addObject(ModelInstance& object, std::unordered_map<LODMesh const*, CommonMeshGroup*>& meshReferences, uint64_t& selfIndex);
        void removeObject(ModelInstance& object, std::unordered_map<LODMesh const*, CommonMeshGroup*>& meshReferences, uint64_t& selfIndex);

        //overwrite camera pointer used for rendering
        void setCamera(Camera* camera) { this->cameraPtr = camera; }

        //draw all the items in the render tree
        const VkSemaphore& beginFrame(std::vector<VkFence>& waitFences); //returns reference to signaled semaphore for the aquired frame, takes in fence(s) to wait on (usually from the rendering of the last frame) and resets them
        void endFrame(const std::vector<VkSemaphore>& waitSemaphores); //takes in pre-signaled semaphore(s) that presentation will wait on

        bool getRTstatus() const { return rtEnabled; }
        void setRTstatus(bool newStatus) { this->rtEnabled = newStatus; }

        PaperMemory::Buffer const* getHostInstancesBufferPtr() const { return hostInstancesDataBuffer.get(); }
        GLFWwindow* getGLFWwindow() const { return window.getWindow(); }
        Device* getDevice() { return &device; }
    };
}