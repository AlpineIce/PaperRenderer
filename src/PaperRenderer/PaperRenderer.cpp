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

    void RenderEngine::addObject(ModelInstance& object, std::unordered_map<LODMesh const*, CommonMeshGroup*>& meshReferences, uint64_t& selfIndex)
    {
        if(object.getParentModelPtr() != NULL)
        {
            for(uint32_t lodIndex = 0; lodIndex < object.getParentModelPtr()->getLODs().size(); lodIndex++)
            {

                for(uint32_t matIndex = 0; matIndex < object.getParentModelPtr()->getLODs().at(lodIndex).meshMaterialData.size(); matIndex++) //iterate materials in LOD
                {
                    //get material instance
                    MaterialInstance* materialInstance;
                    if(object.getMaterialInstances().at(lodIndex).at(matIndex))
                    {
                        materialInstance = object.getMaterialInstances().at(lodIndex).at(matIndex);
                    }
                    else //use default material if one isn't selected
                    {
                        materialInstance = defaultMaterialInstance.get();
                    }

                    //get meshes using same material
                    std::vector<PaperRenderer::LODMesh const*> similarMeshes;
                    for(const LODMesh& mesh : object.getParentModelPtr()->getLODs().at(lodIndex).meshMaterialData.at(matIndex)) //iterate meshes with associated material
                    {
                        similarMeshes.push_back(&mesh);
                    }

                    //check if mesh group class is created
                    if(!renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances.count(materialInstance))
                    {
                        renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances[materialInstance].meshGroups = 
                            std::make_unique<CommonMeshGroup>(&device, &descriptors, materialInstance->getBaseMaterialPtr()->getRasterPipeline());
                    }

                    //add reference
                    renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances[materialInstance].meshGroups->addInstanceMeshes(&object, similarMeshes);
                }
            }
            rendering.addModelInstance(&object, selfIndex);
        }
    }

    void RenderEngine::removeObject(ModelInstance& object, std::unordered_map<LODMesh const*, CommonMeshGroup*>& meshReferences, uint64_t& selfIndex)
    {
        for(auto& [mesh, reference] : meshReferences)
        {
            if(reference) reference->removeInstanceMeshes(&object);
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
