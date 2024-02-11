#pragma once
#include "GLFW/glfw3.h"

#include "RHI/Device.h"
#include "RHI/Window.h"
#include "RHI/Swapchain.h"
#include "RHI/Pipeline.h"
#include "RHI/AccelerationStructure.h"
#include "Material/Material.h"
#include "RenderPass.h"
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
        glm::vec3 position = glm::vec3(0.0f);
        Model const* modelPtr = NULL; //TODO default model
        std::unordered_map<uint32_t, MaterialInstance const*> materials;
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

        std::unordered_map<std::string, std::shared_ptr<Model>> models;
        std::unordered_map<std::string, std::shared_ptr<Material>> materials;
        std::unordered_map<std::string, std::shared_ptr<Texture>> textures;
        std::shared_ptr<DefaultMaterial> defaultMaterial;

        std::string appName;
        bool rtEnabled = false;

        Image loadImage(std::string directory);
        void loadModels(std::string modelsDir);
        void loadTextures(std::string texturesDir);
        void initRT();
        
    public:
        RenderEngine(RendererCreationStruct creationInfo);
        ~RenderEngine();

        //add/remove objects to render tree
        void addObject(ModelInstance& object);
        void removeObject(ModelInstance& object);

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

        Model const* getModelByName(std::string name);
        Material const* getMaterialByName(std::string name);
        Texture const* getTextureByName(std::string name);

        GLFWwindow* getGLFWwindow() const { return window.getWindow(); }
    };
}