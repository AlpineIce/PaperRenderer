#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "Renderer.h"

#include <iostream>
#include <filesystem>
#include <sstream>
#include <fstream>

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
            
            PipelineType type = PipelineType::UNDEFINED;
            if(shadersPath.stem().string().find("TexturelessPBR") != std::string::npos)
            {
                type = PipelineType::PBR;
            }
            else if(shadersPath.stem().string().find("PBR") != std::string::npos)
            {
                type = PipelineType::TexturelessPBR;
            }

            if(type != UNDEFINED)
            {
                renderTree[type] = {std::make_shared<RasterPipeline>(
                        &device,
                        shaderFiles,
                        &descriptors,
                        type,
                        &swapchain), 
                    std::unordered_map<std::string, MaterialNode>()};
            }
        }
    }

    void RenderEngine::loadModels(std::string modelsDir)
    {
        const std::filesystem::path models(modelsDir);
        for(const auto& model : std::filesystem::directory_iterator(models)) //iterate models
        {
            if(model.path().filename().string().find(".fbx") != std::string::npos ) //must be a valid .fbx file
            {
                std::cout << "loading model: " << model.path().stem().string() << std::endl;
                this->models.insert(std::make_pair(
                    model.path().stem().string(),
                    std::make_shared<Model>(&device, &commands, model.path().string())));
            }
        }
    }

    void RenderEngine::loadMaterials(std::string materialsDir)
    {
        //iterate materials path
        const std::filesystem::path materials(materialsDir);
        for(const auto& material : std::filesystem::directory_iterator(materials)) //iterate models
        {
            if(material.path().filename().string().find(".mat") != std::string::npos ) //must be a valid .fbx file
            {
                std::cout << "loading material: " << material.path().stem().string() << std::endl;
                createMaterial(material.path().string());
            }
        }
    }

    void RenderEngine::createMaterial(std::string filePath)
    {
        std::ifstream file(filePath);
        if(file.is_open())
        {
            std::string matName = "UNDEFINED";
            PipelineType type = UNDEFINED;
            std::vector<std::string> textureNames(TEXTURE_ARRAY_SIZE);
            std::vector<glm::vec4> vec4vars(TEXTURE_ARRAY_SIZE);
            
            for(std::string line; std::getline(file, line); )
            {
                if(line.find("#") != std::string::npos) //material name
                {
                    matName = line.substr(line.find("#") + 1, line.length() - line.find("#"));
                }

                if(line.find("pipeline") != std::string::npos && line.find("=") != std::string::npos) //pipeline equals
                {
                    if(line.find("\"PBR\"") != std::string::npos)
                    {
                        type = PipelineType::PBR;
                    }
                    else if(line.find("\"TexturelessPBR\"") != std::string::npos)
                    {
                        type = PipelineType::TexturelessPBR;
                    }
                    else
                    {
                        throw std::runtime_error("Unsupported pipeline specified at material: " + filePath);
                    }
                }
                
                if(line.find("Tex_") != std::string::npos && line.find("=") != std::string::npos)
                {
                    if(line.find("diffuse") != std::string::npos)
                    {
                        std::string name = line.substr(line.find("=") + 1, line.length() - line.find("="));
                        textureNames.at(0) = name.substr(name.find("\"") + 1, line.rfind("\"") - line.find("\"") - 1);
                    }
                    else if(line.find("metalness") != std::string::npos)
                    {
                        std::string name = line.substr(line.find("=") + 1, line.length() - line.find("="));
                        textureNames.at(1) = name.substr(name.find("\"") + 1, line.rfind("\"") - line.find("\"") - 1);
                    }
                    else if(line.find("normal") != std::string::npos)
                    {
                        std::string name = line.substr(line.find("=") + 1, line.length() - line.find("="));
                        textureNames.at(2) = name.substr(name.find("\"") + 1, line.rfind("\"") - line.find("\"") - 1);
                    }
                    else if(line.find("roughness") != std::string::npos)
                    {
                        std::string name = line.substr(line.find("=") + 1, line.length() - line.find("="));
                        textureNames.at(3) = name.substr(name.find("\"") + 1, line.rfind("\"") - line.find("\"") - 1);
                    }
                }
                else if(line.find("Color_") != std::string::npos && line.find("=") != std::string::npos)
                {
                    if(line.find("diffuse") != std::string::npos)
                    {
                        float r, g, b, a;
                        std::string content = line.substr(line.find("\"(") + 2, line.length() - line.find("\"("));
                        content = content.substr(0, content.find(")\""));
                        
                        std::stringstream ss(content);
                        ss >> r >> g >> b >> a;

                        vec4vars.at(0) = (glm::vec4(r, g, b, a));
                    }
                    else if(line.find("metalness") != std::string::npos)
                    {
                        float r, g, b, a;
                        std::string content = line.substr(line.find("\"(") + 2, line.length() - line.find("\"("));
                        content = content.substr(0, content.find(")\""));
                        
                        std::stringstream ss(content);
                        ss >> r >> g >> b >> a;

                        vec4vars.at(1) = (glm::vec4(r, g, b, a));
                    }
                    else if(line.find("normal") != std::string::npos)
                    {
                        float r, g, b, a;
                        std::string content = line.substr(line.find("\"(") + 2, line.length() - line.find("\"("));
                        content = content.substr(0, content.find(")\""));
                        
                        std::stringstream ss(content);
                        ss >> r >> g >> b >> a;

                        vec4vars.at(2) = (glm::vec4(r, g, b, a));
                    }
                    else if(line.find("roughness") != std::string::npos)
                    {
                        float r, g, b, a;
                        std::string content = line.substr(line.find("\"(") + 2, line.length() - line.find("\"("));
                        content = content.substr(0, content.find(")\""));
                        
                        std::stringstream ss(content);
                        ss >> r >> g >> b >> a;

                        vec4vars.at(3) = (glm::vec4(r, g, b, a));
                    }
                }
            }
            if(matName == "UNDEFINED" || type == UNDEFINED) throw std::runtime_error("Not all material parameters specified at " + filePath);
            
            std::vector<Texture const*> textures;
            for(std::string name : textureNames)
            {
                textures.push_back(getTextureByName(name));
            }
            textures.resize(TEXTURE_ARRAY_SIZE);

            materials.insert(std::make_pair(
                matName,
                std::make_shared<Material>(&device, type, matName, textures, vec4vars)));

            MaterialNode matParams;
            matParams.material = materials.at(matName).get();
            
            renderTree.at(type).materials.insert(std::make_pair(matName, matParams));
        }
        else throw std::runtime_error("Error opening material file at " + filePath);

    }

    void RenderEngine::loadTextures(std::string texturesDir)
    {
        const std::filesystem::path textures(texturesDir);
        for(const auto& texture : std::filesystem::recursive_directory_iterator(textures)) //iterate models RECURSIVE BECAUSE TEXTURES CAN BE IN FOLDERS
        {
            if(texture.path().filename().string().find(".png") != std::string::npos ||
                texture.path().filename().string().find(".jpeg") != std::string::npos) //must be a valid .png/jpg file
            {
                std::cout << "loading texture: " << texture.path().stem().string() << std::endl;
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
        if(object.modelPtr != NULL)
        {
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
                }
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
    }

    Material const* RenderEngine::getMaterialByName(std::string name)
    {
        if(this->materials.count(name))
        {
            return this->materials.at(name).get();
        }
        return NULL;
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
