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

        //render tree stores all pipelines, their child materials, with their child RenderObject pointers
        std::unordered_map<Material*, MaterialNode> renderTree;
        std::unique_ptr<DefaultMaterial> defaultMaterial;
        std::unique_ptr<DefaultMaterialInstance> defaultMaterialInstance;

        std::string appName;
        bool rtEnabled = false;

        void initRT();
        
    public:
        RenderEngine(RendererCreationStruct creationInfo);
        ~RenderEngine();

        //add/remove objects to render tree
        void addObject(ModelInstance& object, std::unordered_map<LODMesh const*, CommonMeshGroup*>& meshReferences, uint64_t& selfIndex);
        void removeObject(ModelInstance& object, std::unordered_map<LODMesh const*, CommonMeshGroup*>& meshReferences, uint64_t& selfIndex);

        //overwrite camera pointer used for rendering
        void setCamera(Camera* camera);

        //draw all the items in the render tree
        void drawAllReferences();

        bool getRTstatus() const { return rtEnabled; }
        void setRTstatus(bool newStatus) { this->rtEnabled = newStatus; }

        GLFWwindow* getGLFWwindow() const { return window.getWindow(); }
        Device* getDevice() { return &device; }
    };
}