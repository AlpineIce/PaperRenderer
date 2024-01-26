#pragma once
#include "GLFW/glfw3.h"

#include "RHI/Device.h"
#include "RHI/Window.h"
#include "RHI/Swapchain.h"
#include "RHI/Pipeline.h"
#include "RHI/AccelerationStructure.h"
#include "RenderPass.h"
#include "Material.h"
#include "Model.h"

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

namespace Renderer
{
    typedef std::unordered_map<uint32_t, DrawBufferObject> RenderObjectReference;
    
    struct RendererCreationStruct
    {
        std::string appName;
        unsigned int resX;
        unsigned int resY;
    };

    //struct to be used as an instance of a model, used as a reference for rendering, material index corresponds to material slot
    struct ModelInstance
    {
        RenderObjectReference objRefs;
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        Model const* modelPtr = NULL; //TODO default model
        std::vector<Material const*> materials; //TODO default material

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
        std::unordered_map<PipelineType, PipelineNode> renderTree;

        std::unordered_map<std::string, std::shared_ptr<Model>> models;
        std::unordered_map<std::string, std::shared_ptr<Material>> materials;
        std::unordered_map<std::string, std::shared_ptr<Texture>> textures;

        std::string appName;
        bool rtEnabled = true;

        Image loadImage(std::string directory);
        void loadPipelines();
        void loadModels(std::string modelsDir);
        void loadMaterials(std::string materialsDir);
        void createMaterial(std::string filePath);
        void loadTextures(std::string texturesDir);
        void initRT();
        
    public:
        RenderEngine(RendererCreationStruct creationInfo);
        ~RenderEngine();

        //Add an object to the render tree with its corresponding material and pipeline. Returns iterator to tree location which is also used for removing
        void addObject(ModelInstance& object);

        //remove an object from the render tree based on a ptr to a tree leaf
        void removeObject(ModelInstance& object);

        //overwrite camera pointer used for rendering
        void setCamera(Camera* camera);

        //draw all the items in the render tree
        void drawAllReferences();

        bool getRTstatus() const { return rtEnabled; }
        void setRTstatus(bool newStatus) { this->rtEnabled = newStatus; }

        Model const* getModelByName(std::string name);
        Material const* getMaterialByName(std::string name);
        Texture const* getTextureByName(std::string name);

        GLFWwindow* getGLFWwindow() const { return window.getWindow(); }
    };
}