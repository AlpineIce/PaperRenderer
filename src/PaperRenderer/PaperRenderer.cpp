#include "PaperRenderer.h"

#include <iostream>
#include <filesystem>
#include <sstream>
#include <fstream>

namespace PaperRenderer
{
    RenderEngine::RenderEngine(RendererCreationStruct creationInfo)
        :appName(creationInfo.appName),
        device(creationInfo.appName),
        window(WindowInformation(creationInfo.resX, creationInfo.resY, false), creationInfo.appName, &device),
        swapchain(&device, &window, false),
        descriptors(&device),
        pipelineBuilder(&device, &descriptors, &swapchain),
        rendering(&swapchain, &device, &descriptors, &pipelineBuilder)
    {
        defaultMaterial = std::make_unique<DefaultMaterial>("resources/shaders/");
        defaultMaterialInstance = std::make_unique<DefaultMaterialInstance>(defaultMaterial.get());

        if(rtEnabled) initRT();

        vkDeviceWaitIdle(device.getDevice());
        std::cout << "Renderer initialization complete" << std::endl;
    }

    RenderEngine::~RenderEngine()
    {
        vkDeviceWaitIdle(device.getDevice());
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

    void RenderEngine::addObject(ModelInstance& object, std::vector<std::unordered_map<uint32_t, DrawBufferObject>>& meshReferences, uint64_t& selfIndex)
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
                            materialInstance = lod.materials.at(matSlot);
                        }
                        else //use default material if one isnt selected
                        {
                            materialInstance = defaultMaterialInstance.get();
                            lod.materials[matSlot] = materialInstance;
                        }

                        //pointers to object data
                        meshReferences.at(lodIndex)[meshIndex] = {
                            .parentMesh = &mesh,
                            .parentLOD = &lod.shaderLOD,
                            .parentModel = object.getModelPtr(),
                            .objectTransform = &object.getTransformation(),
                            .isVisible = &object.getVisibility()
                        };

                        //check if drawing class thing has been created
                        if(!renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances[materialInstance].objectBuffer)
                        {
                            renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances[materialInstance].objectBuffer = 
                                std::make_unique<IndirectDrawContainer>(&device, &descriptors, materialInstance->getBaseMaterialPtr()->getRasterPipeline());
                        }

                        //add reference
                        renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances[materialInstance].objectBuffer->addElement(meshReferences.at(lodIndex).at(meshIndex));

                        meshIndex++;
                    }
                }
                lodIndex++;
            }
            rendering.addModelInstance(&object, selfIndex);
        }
    }

    void RenderEngine::removeObject(ModelInstance& object, std::vector<std::unordered_map<uint32_t, DrawBufferObject>>& meshReferences, uint64_t& selfIndex)
    {
        uint32_t lodIndex = 0;
        for(const LOD& lod : object.getModelPtr()->getLODs()) //iterate LODs
        {
            for(const auto [matSlot, meshes] : lod.meshes) //iterate materials in LOD
            {
                uint32_t meshIndex = 0;
                for(const auto [matSlot, meshes] : lod.meshes) //iterate materials in LOD
                {
                    if(meshReferences.at(lodIndex).count(meshIndex))
                    {
                        MaterialInstance const* materialInstance = lod.materials.at(matSlot);
                        renderTree.at((Material*)materialInstance->getBaseMaterialPtr()).instances.at(materialInstance).objectBuffer->removeElement(meshReferences.at(lodIndex).at(meshIndex));
                    }
                    meshIndex++;
                }
            }
            lodIndex++;
        }
        rendering.removeModelInstance(&object, selfIndex);
    }

    void RenderEngine::setCamera(Camera *camera)
    {
        this->rendering.setCamera(camera);
    }

    void RenderEngine::drawAllReferences()
    {
        //start command buffer and bind pipeline
        rendering.rasterOrTrace(!rtEnabled, renderTree);

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
}
