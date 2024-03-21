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
        rendering(&swapchain, &device, &commands, &descriptors, &pipelineBuilder)
    {
        Material::initRendererInfo(&device, &commands, &descriptors, &pipelineBuilder);
        loadTextures("resources/textures");

        defaultMaterial = std::make_shared<DefaultMaterial>("resources/materials/Default_vert.spv", "resources/materials/Default_frag.spv");

        if(rtEnabled) initRT();

        vkDeviceWaitIdle(device.getDevice());
        std::cout << "Renderer initialization complete" << std::endl;
    }

    RenderEngine::~RenderEngine()
    {
        vkDeviceWaitIdle(device.getDevice());
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
        /*BottomAccelerationStructureData bottomData;
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
        rtAccelStructure.createBottomLevel(bottomData);*/
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

    void RenderEngine::addObject(ModelInstance& object, std::vector<std::unordered_map<uint32_t, DrawBufferObject>>& meshReferences, std::list<ModelInstance*>::iterator& objectReference)
    {
        if(object.getModelPtr() != NULL)
        {
            uint32_t lodIndex = 0;
            for(LOD& lod : object.getModelPtr()->getLODs()) //iterate LODs
            {
                for(auto& [matSlot, meshes] : lod.meshes) //iterate materials in LOD
                {
                    uint32_t meshIndex = 0;
                    for(LODMesh& mesh : meshes) //iterate meshes with associated material
                    {
                        MaterialInstance const* materialInstance;

                        if(lod.materials.count(matSlot))
                        {
                            materialInstance = (MaterialInstance const*)lod.materials.at(matSlot);
                        }
                        else //use default material if one isnt selected
                        {
                            materialInstance = &(defaultMaterial->getDefaultInstance());
                        }

                        //pointers to object data
                        meshReferences.at(lodIndex)[meshIndex] = {
                            .parentMesh = &mesh,
                            .parentLOD = &lod.shaderLOD,
                            .parentModel = object.getModelPtr(),
                            .objectTransform = &object.getTransformation(),
                            .isVisible = &object.getVisibility(),
                            .sphericalBounds = &object.getModelPtr()->getSphericalBounds()
                        };

                        //check if drawing class thing has been created
                        if(!renderTree[(Material*)materialInstance->parentMaterial].instances[materialInstance].objectBuffer)
                        {
                            renderTree[(Material*)materialInstance->parentMaterial].instances[materialInstance].objectBuffer = 
                                std::make_shared<IndirectDrawContainer>(&device, &commands, &descriptors, materialInstance->parentMaterial->getRasterPipeline());
                        }

                        //add reference
                        renderTree[(Material*)materialInstance->parentMaterial].instances[materialInstance].objectBuffer->addElement(meshReferences.at(lodIndex).at(meshIndex));

                        meshIndex++;
                    }
                }
                lodIndex++;
            }
            rendering.addModelInstance(&object);
        }
    }

    void RenderEngine::removeObject(ModelInstance& object, std::vector<std::unordered_map<uint32_t, DrawBufferObject>>& meshReferences, std::list<ModelInstance*>::iterator& objectReference)
    {
        uint32_t lodIndex = 0;
        for(const Renderer::LOD& lod : object.getModelPtr()->getLODs()) //iterate LODs
        {
            for(const auto [matSlot, meshes] : lod.meshes) //iterate materials in LOD
            {
                uint32_t meshIndex = 0;
                for(const auto [matSlot, meshes] : lod.meshes) //iterate materials in LOD
                {
                    if(meshReferences.at(lodIndex).count(meshIndex))
                    {
                        MaterialInstance const* materialInstance = (MaterialInstance const*)lod.materials.at(matSlot);
                        renderTree.at((Material*)materialInstance->parentMaterial).instances.at(materialInstance).objectBuffer->removeElement(meshReferences.at(lodIndex).at(meshIndex));
                    }
                    meshIndex++;
                }
            }
            lodIndex++;
        }
        rendering.removeModelInstance(objectReference);
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
        rendering.drawAll(renderTree, lightingInfo);

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

    Texture const* RenderEngine::getTextureByName(std::string name)
    {
        if(this->textures.count(name))
        {
            return this->textures.at(name).get();
        }
        return NULL; //default texture already exists in texture class
    }
}
