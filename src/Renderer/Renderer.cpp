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
        swapchain(&device, &window, false),
        descriptors(&device, &commands),
        pipelineBuilder(&device, &descriptors, &swapchain),
        rtAccelStructure(&device, &commands),
        rendering(&swapchain, &device, &commands, &descriptors, &pipelineBuilder)
    {
        Material::initRendererInfo(&device, &commands, &descriptors, &pipelineBuilder);
        loadModels("resources/models");
        loadTextures("resources/textures");

        defaultMaterial = std::make_shared<DefaultMaterial>("resources/materials/Default_vert.spv", "resources/materials/Default_frag.spv");

        if(rtEnabled) initRT();

        vkDeviceWaitIdle(device.getDevice());
    }

    RenderEngine::~RenderEngine()
    {
        vkDeviceWaitIdle(device.getDevice());
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

    void RenderEngine::initRT()
    {
        BottomAccelerationStructureData bottomData;
        for(auto& [name, model] : models)
        {
            AccelerationStructureModelReference modelRef;
            modelRef.modelPointer = model.get();
            for(const ModelMesh& mesh : model->getModelMeshes())
            {
                modelRef.meshes.push_back(&mesh);
            }
            bottomData.models.push_back(modelRef);
        }
        rtAccelStructure.createBottomLevel(bottomData);
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
        if(object.modelPtr != NULL)
        {
            for(uint32_t i = 0; i < object.modelPtr->getModelMeshes().size(); i++) //iterate meshes
            {
                MaterialInstance const* materialInstance;
                uint32_t materialIndex = object.modelPtr->getModelMeshes().at(i).materialIndex;

                if(object.materials.count(materialIndex))
                {
                    materialInstance = object.materials.at(materialIndex);
                }
                else //use default material if one isnt selected
                {
                    materialInstance = &(defaultMaterial->getDefaultInstance());
                    object.materials[materialIndex] = materialInstance;
                }

                object.objRefs[i] = {
                    .modelMatrix = &object.modelMatrix,
                    .position = &object.position,
                    .mesh = object.modelPtr->getModelMeshes().at(i).mesh.get()
                };

                if(!renderTree[(Material*)materialInstance->parentMaterial].instances[materialInstance].objectBuffer)
                {
                    renderTree[(Material*)materialInstance->parentMaterial].instances[materialInstance].objectBuffer = 
                        std::make_shared<IndirectDrawContainer>(&device, &commands, &descriptors, materialInstance->parentMaterial->getRasterPipeline());
                }
                renderTree[(Material*)materialInstance->parentMaterial].instances[materialInstance].objectBuffer->addElement(object.objRefs.at(i));
            }
        }
    }

    void RenderEngine::removeObject(ModelInstance& object)
    {
        for(uint32_t i = 0; i < object.modelPtr->getModelMeshes().size(); i++)
        {
            if(object.objRefs.count(i))
            {
                MaterialInstance const* materialInstance;
                materialInstance = object.materials.at(object.modelPtr->getModelMeshes().at(i).materialIndex);
                renderTree.at((Material*)materialInstance->parentMaterial).instances.at(materialInstance).objectBuffer->removeElement(object.objRefs.at(i));
            }
        }
    }

    void RenderEngine::addPointLight(PointLightObject& light)
    {
        lightingInfo.pointLights.push_back(&light.light);
        light.lightReference = lightingInfo.pointLights.end()--;
    }

    void RenderEngine::removePointLight(PointLightObject& light)
    {
        lightingInfo.pointLights.erase(light.lightReference);
    }

    void RenderEngine::setCamera(Camera *camera)
    {
        this->rendering.setCamera(camera);
    }

    void RenderEngine::drawAllReferences()
    {
        //start command buffer and bind pipeline
        rendering.preProcessing(renderTree, lightingInfo);
        rendering.raster(renderTree);

        //RT pass
        /*if(rtEnabled)
        {
            TopAccelerationData accelData = {};
            for(const auto& [pipelineType, pipelineNode] : renderTree) //pipeline
            {
                for(const auto& [materialName, materialNode] : pipelineNode.materials) //material
                {
                    for(const auto& [mesh, node] : materialNode.objectBuffer->getDrawCallTree()) //similar objects
                    {
                        for(auto object = node.objects.begin(); object != node.objects.end(); object++)
                        {
                            Model const* modelPtr;
                            (*object)->;
                            VkTransformMatrixKHR matrix;
                            matrix.matrix[0][0] = (*((*object)->modelMatrix))[0][0];
                            matrix.matrix[0][1] = (*((*object)->modelMatrix))[0][1];
                            matrix.matrix[0][2] = (*((*object)->modelMatrix))[0][2];
                            matrix.matrix[0][3] = (*((*object)->modelMatrix))[0][3];

                            matrix.matrix[1][0] = (*((*object)->modelMatrix))[1][0];
                            matrix.matrix[1][1] = (*((*object)->modelMatrix))[1][1];
                            matrix.matrix[1][2] = (*((*object)->modelMatrix))[1][2];
                            matrix.matrix[1][3] = (*((*object)->modelMatrix))[1][3];

                            matrix.matrix[2][0] = (*((*object)->modelMatrix))[2][0];
                            matrix.matrix[2][1] = (*((*object)->modelMatrix))[2][1];
                            matrix.matrix[2][2] = (*((*object)->modelMatrix))[2][2];
                            matrix.matrix[2][3] = (*((*object)->modelMatrix))[2][3];
                        }
                    }
                }
            }
            rtAccelStructure.createTopLevel(accelData);
        }*/
        glfwPollEvents();
    }

    //----------GETTER/SETTER FUNCTIONS----------//

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

    Texture const* RenderEngine::getTextureByName(std::string name)
    {
        if(this->textures.count(name))
        {
            return this->textures.at(name).get();
        }
        return NULL; //default texture already exists in texture class
    }
}
