#pragma once
#include "GLFW/glfw3.h"

#include "RHI/Device.h"
#include "RHI/Window.h"
#include "RHI/AccelerationStructure.h"
#include "RenderPass.h"

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

namespace Renderer
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
        CmdBufferAllocator commands;
        DescriptorAllocator descriptors;
        PipelineBuilder pipelineBuilder;
        AccelerationStructure rtAccelStructure;
        RenderPass rendering;

        //render tree stores all pipelines, their child materials, with their child RenderObject pointers
        LightingInformation lightingInfo;
        
        std::unordered_map<Material*, MaterialNode> renderTree;
        std::unordered_map<std::string, std::shared_ptr<Texture>> textures;
        std::shared_ptr<DefaultMaterial> defaultMaterial;

        std::string appName;
        bool rtEnabled = false;

        Image loadImage(std::string directory);
        void loadTextures(std::string texturesDir);
        void initRT();
        
    public:
        RenderEngine(RendererCreationStruct creationInfo);
        ~RenderEngine();

        //add/remove objects to render tree
        void addObject(ModelInstance& object, std::vector<std::unordered_map<uint32_t, DrawBufferObject>>& meshReferences, std::list<ModelInstance*>::iterator& objectReference);
        void removeObject(ModelInstance& object, std::vector<std::unordered_map<uint32_t, DrawBufferObject>>& meshReferences, std::list<ModelInstance*>::iterator& objectReference);

        //add point lights, returns list reference which is necessary for removal
        void addPointLight(PointLightObject& light);
        void setDirectLight(const DirectLight& light) { lightingInfo.directLight = &light; }
        void setAmbientLight(const AmbientLight& light) { lightingInfo.ambientLight = &light; }

        //remove point light using a previously aquired reference TODO make the light contain the reference
        void removePointLight(PointLightObject& light);

        //overwrite camera pointer used for rendering
        void setCamera(Camera* camera);

        //draw all the items in the render tree
        void drawAllReferences();

        bool getRTstatus() const { return rtEnabled; }
        void setRTstatus(bool newStatus) { this->rtEnabled = newStatus; }

        Texture const* getTextureByName(std::string name);

        GLFWwindow* getGLFWwindow() const { return window.getWindow(); }
        Device* getDevice() { return &device; }
        CmdBufferAllocator* getCommandsHandler() { return &commands; }
    };
}