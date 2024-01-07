#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "Renderer.h"

#include <filesystem>

namespace Renderer
{
    RenderEngine::RenderEngine(RendererCreationStruct creationInfo)
        :appName(creationInfo.appName),
        device(creationInfo.appName),
        window(WindowInformation(creationInfo.resX, creationInfo.resY, false), creationInfo.appName, &device),
        commands(&device),
        swapchain(&device, &window, false, false),
        descriptors(&device, &commands),
        rendering(&swapchain, &device, &commands, &descriptors)
    {
        Pipeline::createCache(&device);

        loadPipelines("resources/pipelines");
        loadModels("resources/models"); //this function isnt very memory efficient as it just loads everything in the folder
        loadTextures("resources/textures"); //same deal as the funciton call above too, needs some optimization
        loadMaterials("resources/materials"); //arguably ok if all materials get loaded
    }

    RenderEngine::~RenderEngine()
    {
        vkDeviceWaitIdle(device.getDevice());
        Pipeline::destroyCache(&device);
    }

    void RenderEngine::loadPipelines(std::string shadersDir)
    {
        const std::filesystem::path pipelines(shadersDir);
        for(const auto& pipeline : std::filesystem::directory_iterator(pipelines)) //iterate pipelines
        {
            //create pipeline
            std::vector<std::string> shaderFiles;
            const std::filesystem::path shadersPath(pipeline.path());
            for(const auto& pipelineShader : std::filesystem::directory_iterator(shadersPath)) //iterate shaders
            {
                shaderFiles.push_back(pipelineShader.path().string());
            }
            
            renderTree.emplace(std::make_pair<PipelineType, PipelineNode>(PipelineType::PBR, {
                std::make_shared<RasterPipeline>(
                    &device,
                    shaderFiles,
                    &descriptors,
                    PipelineType::PBR,
                    &swapchain), 
                std::unordered_map<std::string, MaterialNode>()}));

            //create materials associated with pipeline
        }
    }

    void RenderEngine::loadModels(std::string modelsDir)
    {
        const std::filesystem::path models(modelsDir);
        for(const auto& model : std::filesystem::directory_iterator(models)) //iterate models
        {
            if(model.path().filename().string().find(".fbx") != std::string::npos ) //must be a valid .fbx file
            {
                this->models.insert(std::make_pair(
                    model.path().stem().string(),
                    std::make_shared<Model>(&device, &commands, model.path().string())));
            }
        }
    }

    void RenderEngine::loadMaterials(std::string materialsDir)
    {
        //default material
        std::vector<std::string> defaultMaterialTextures;
        createMaterial(PipelineType::PBR, defaultMaterialTextures, "default");

        //base material
        std::vector<std::string> baseTextures = {
            "MS_Cup_low_Bone_BaseColor"
        };
        createMaterial(PipelineType::PBR, baseTextures, "base");

        //tooth
        std::vector<std::string> toothTextures = {
            "MS_Cup_low_Bone_Tooth_BaseColor"
        };
        createMaterial(PipelineType::PBR, toothTextures, "tooth");

        //head
        std::vector<std::string> headTextures = {
            "MS_Cup_low_Metalic_BaseColor"
        };
        createMaterial(PipelineType::PBR, headTextures, "head");

        //gem
        std::vector<std::string> gemTextures = {
            "MS_Cup_low_Gem_BaseColor"
        };
        createMaterial(PipelineType::PBR, gemTextures, "gem");

        //TODO iterate folder with material files... and create a whole material system... (bruh)
    }

    void RenderEngine::createMaterial(PipelineType type, const std::vector<std::string>& textureNames, std::string materialName)
    {
        //fill in textures
        std::vector<Texture const*> textures;
        for(std::string name : textureNames)
        {
            textures.push_back(getTextureByName(name));
        }
        textures.resize(TEXTURE_ARRAY_SIZE);

        materials.insert(std::make_pair(
            std::string(materialName),
            std::make_shared<Material>(&device, PipelineType::PBR, materialName, textures)));
        
        //add material node to render tree
        MaterialNode defaultParams;
        defaultParams.material = materials.at(materialName).get();
        
        renderTree.at(type).materials.insert(std::make_pair(materialName, defaultParams));
    }

    void RenderEngine::loadTextures(std::string texturesDir)
    {
        const std::filesystem::path textures(texturesDir);
        for(const auto& texture : std::filesystem::recursive_directory_iterator(textures)) //iterate models RECURSIVE BECAUSE TEXTURES CAN BE IN FOLDERS
        {
            if(texture.path().filename().string().find(".png") != std::string::npos ||
                texture.path().filename().string().find(".jpeg") != std::string::npos) //must be a valid .png/jpg file
            {
                Image imageData = loadImage(texture.path().string());

                this->textures.insert(std::make_pair(
                    texture.path().stem().string(),
                    std::make_shared<Texture>(&device, &commands, &imageData)));
                
                stbi_image_free(imageData.data);
            }
        }
    }

    Image RenderEngine::loadImage(std::string directory)
    {
        Image returnImg;
        returnImg.data = stbi_load(directory.c_str(), &returnImg.width, &returnImg.height, &returnImg.channels, STBI_rgb_alpha);
        returnImg.size = returnImg.width * returnImg.height * 4;

        if(!returnImg.data) //image is NULL by default
        {
            throw std::runtime_error("failed to load texture image!");
        }

        return returnImg;
    }

    void RenderEngine::addObject(ModelInstance& object)
    {
        RenderObjectReference returnReferences;
        for(uint32_t i = 0; i < object.modelPtr->getModelMeshes().size(); i++)
        {
            if(object.materials.at(object.modelPtr->getModelMeshes().at(i).materialIndex)) //make sure a valid material is set
            {
                PipelineType pipelineType = object.materials.at(object.modelPtr->getModelMeshes().at(i).materialIndex)->getPipelineType();
                std::string matName = object.materials.at(object.modelPtr->getModelMeshes().at(i).materialIndex)->getMatName();
                
                ObjectParameters treeObject = {
                    .mesh = object.modelPtr->getModelMeshes().at(i).mesh.get(),
                    .modelMatrix = &object.modelMatrix
                };

                renderTree.at(pipelineType).materials.at(matName).objects.push_back(treeObject);
                object.objReference.insert(std::make_pair(i, renderTree.at(pipelineType).materials.at(matName).objects.end()));
                object.objReference.at(i)--;
                
                auto b = 3;
            }
        }
    }

    void RenderEngine::removeObject(ModelInstance& object)
    {
        for(uint32_t i = 0; i < object.modelPtr->getModelMeshes().size(); i++)
        {
            if(object.objReference.count(i))
            {
                PipelineType pipelineType = object.materials.at(object.modelPtr->getModelMeshes().at(i).materialIndex)->getPipelineType();
                std::string matName = object.materials.at(object.modelPtr->getModelMeshes().at(i).materialIndex)->getMatName();

                renderTree.at(pipelineType).materials.at(matName).objects.erase(object.objReference.at(i));
            }
        }
    }

    void RenderEngine::drawAllReferences()
    {
        rendering.startNewFrame();

        for(const auto& [pipelineType, pipelineNode] : renderTree) //pipeline
        {
            rendering.bindPipeline(pipelineNode.pipeline.get());
            for(const auto& [materialName, materialNode] : pipelineNode.materials) //material
            {
                rendering.bindMaterial(materialNode.material);
                for(auto object = materialNode.objects.begin(); object != materialNode.objects.end(); object++) //object
                {
                    rendering.drawIndexed(*object);
                }
            }
            rendering.submit();
        }

        rendering.incrementFrameCounter();
        glfwPollEvents();
    }

    //----------GETTER FUNCTIONS----------//

    Model const* RenderEngine::getModelByName(std::string name)
    {
        if(this->models.count(name))
        {
            return this->models.at(name).get();
        }
        return NULL;
        //return this->models.at("Default").get();
    }

    Material const* RenderEngine::getMaterialByName(std::string name)
    {
        if(this->materials.count(name))
        {
            return this->materials.at(name).get();
        }
        return this->materials.at("default").get();
    }

    void RenderEngine::setCamera(Camera* camera)
    {
        this->rendering.setCamera(camera);
    }

    Texture const* RenderEngine::getTextureByName(std::string name)
    {
        if(this->textures.count(name))
        {
            return this->textures.at(name).get();
        }
        return NULL; //default texture already exists in texture class
    }
}
