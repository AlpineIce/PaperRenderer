#include "Common.h"
#include "GuiRender.h"
#include "RenderPasses.h"

//tinygltf
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "lib/tinygltf/tiny_gltf.h"

//loaded scene data from glTF
struct SceneData
{
    std::unordered_map<std::string, std::unique_ptr<PaperRenderer::Model>> models = {};
    std::unordered_map<PaperRenderer::Model const*, PaperRenderer::ModelTransformation> instanceTransforms = {}; //this example does 1 instance per model, so thats why its 1:1
    std::unordered_map<std::string, std::vector<std::string>> instanceMaterials = {};
    std::unordered_map<std::string, MaterialParameters> materialInstancesData = {};
    std::unique_ptr<PaperRenderer::Camera> camera = NULL;
};

//example function for loading a glTF and integrating with Model, Material, and Camera creation
SceneData loadSceneData(PaperRenderer::RenderEngine& renderer)
{
    //glTF path
    std::string gltfPath = "./resources/models/PaperRendererExample.glb";
    
    //load glTF
    tinygltf::Model gltfModel;
    std::string error;
    std::string warning;
    tinygltf::TinyGLTF gltfContext;

    gltfContext.LoadBinaryFromFile(&gltfModel, &error, &warning, gltfPath);

    //initialize scene data variable
    SceneData returnData;
    returnData.models.reserve(gltfModel.meshes.size());
    returnData.materialInstancesData.reserve(gltfModel.materials.size());

    //iterate nodes
    for(const tinygltf::Node& node : gltfModel.nodes)
    {
        //get node type
        if(node.mesh != -1) //models
        {
            //model name
            const std::string modelName = node.name;

            //only one LOD will be used in this example
            PaperRenderer::ModelLODInfo modelLOD;

            //iterate mesh primitives
            for(const tinygltf::Primitive& primitive : gltfModel.meshes[node.mesh].primitives)
            {
                const uint32_t matIndex = primitive.material;
                
                //fill in a vector with vertex data
                const tinygltf::BufferView& vertexPositions = gltfModel.bufferViews[primitive.attributes.at("POSITION")];
                const tinygltf::BufferView& vertexNormals = gltfModel.bufferViews[primitive.attributes.at("NORMAL")];
                const tinygltf::BufferView& vertexUVs = gltfModel.bufferViews[primitive.attributes.at("TEXCOORD_0")];
                std::vector<char> vertexData(sizeof(Vertex) * gltfModel.accessors[primitive.attributes.at("POSITION")].count);

                for(uint32_t i = 0; i < gltfModel.accessors[primitive.attributes.at("POSITION")].count; i++)
                {
                    Vertex vertex = {
                        .position = *(glm::vec3*)(gltfModel.buffers[0].data.data() + vertexPositions.byteOffset + (12 * i)),
                        .normal =   *(glm::vec3*)(gltfModel.buffers[0].data.data() + vertexNormals.byteOffset   + (12 * i)),
                        .uv =       *(glm::vec2*)(gltfModel.buffers[0].data.data() + vertexUVs.byteOffset       + (8  * i))
                    };
                    memcpy(vertexData.data() + (sizeof(Vertex) * i), &vertex, sizeof(Vertex));
                }

                //fill in a vector with index data
                const tinygltf::BufferView& indices = gltfModel.bufferViews[primitive.indices];
                const uint32_t indexStride = tinygltf::GetComponentSizeInBytes(gltfModel.accessors[primitive.indices].componentType);
                std::vector<uint32_t> indexData(gltfModel.accessors[primitive.indices].count);

                for(uint32_t i = 0; i < gltfModel.accessors[primitive.indices].count; i++)
                {
                    memcpy(indexData.data() + i, gltfModel.buffers[0].data.data() + indices.byteOffset + (i * indexStride), indexStride);
                }

                //push data to LOD
                modelLOD.lodData[matIndex] = {
                    .verticesData = std::move(vertexData),
                    .indices = std::move(indexData),
                    .opaque = gltfModel.materials[matIndex].alphaMode == "OPAQUE"
                };

                //set instance material
                returnData.instanceMaterials[modelName].push_back(gltfModel.materials[matIndex].name);
            }

            const PaperRenderer::ModelCreateInfo modelInfo = {
                .vertexAttributes = {
                    {
                        .location = 0,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(Vertex, position)
                    },
                    {
                        .location = 1,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(Vertex, normal)
                    },
                    {
                        .location = 2,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32_SFLOAT,
                        .offset = offsetof(Vertex, uv)
                    }
                },
                .vertexDescription = {
                    .binding = 0,
                    .stride = sizeof(Vertex),
                    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                },
                .LODs = { modelLOD },
                .createBLAS = true,
                .modelName = modelName
            };

            returnData.models[modelName] = std::make_unique<PaperRenderer::Model>(renderer, modelInfo);

            //model transform
            PaperRenderer::ModelTransformation transform = {
                .position = node.translation.size() ? glm::vec3(node.translation[0], node.translation[1], node.translation[2]) : glm::vec3(0.0f),
                .scale = node.scale.size() ? glm::vec3(node.scale[0], node.scale[1], node.scale[2]) : glm::vec3(1.0f),
                .rotation = node.rotation.size() ? glm::quat(node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
            };
            returnData.instanceTransforms[returnData.models[modelName].get()] = transform;
        }
        else if(node.camera != -1 && !returnData.camera) //camera
        {
            const tinygltf::Camera& camera = gltfModel.cameras[node.camera];

            const glm::vec3 newCameraPosition = glm::vec3(15.0f * sin(glfwGetTime()), 15.0f * cos(glfwGetTime()), 5.0f);

            PaperRenderer::CameraTransformation newTransform = {
                .translationParameters = {
                    .rotationType = PaperRenderer::CameraRotationType::QUATERNION,
                    .rotation = { .qRotation = glm::lookAt(newCameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f)) },
                    .position = newCameraPosition
                }
            };

            const PaperRenderer::CameraInfo cameraInfo = {
                .projectionType = PaperRenderer::CameraProjectionType::PERSPECTIVE,
                .projection = { .perspective = { (float)camera.perspective.yfov * 100.0f } },
                .transformationType = PaperRenderer::CameraTransformationType::PARAMETERS,
                .transformation = newTransform,
                .clipNear = (float)camera.perspective.znear,
                .clipFar = (float)camera.perspective.zfar
            };

            returnData.camera = std::make_unique<PaperRenderer::Camera>(renderer, cameraInfo);
        }
    }

    //load material instances
    for(const tinygltf::Material& material : gltfModel.materials)
    {
        const tinygltf::ColorValue baseColor = material.values.count("baseColorFactor") ? material.values.at("baseColorFactor").ColorFactor() : tinygltf::ColorValue({1.0, 1.0, 1.0, 1.0});
        const float roughness = material.values.at("roughnessFactor").Factor();
        const float metallic = material.values.at("metallicFactor").Factor();

        //emissive
        float emissionStrength = 0.0f;
        glm::vec3 emission = glm::vec3(0.0f, 0.0f, 0.0f);

        if(material.extensions.contains("KHR_materials_emissive_strength"))
        {
            const std::vector<double>& numberArray = material.additionalValues.at("emissiveFactor").number_array;
            emissionStrength = material.extensions.at("KHR_materials_emissive_strength").Get("emissiveStrength").GetNumberAsDouble();
            emission = glm::vec3(numberArray[0], numberArray[1], numberArray[2]);
        }
        
        returnData.materialInstancesData[material.name] = {
            .baseColor = glm::vec4(baseColor[0], baseColor[1], baseColor[2], baseColor[3]),
            .emission = glm::vec4(emission.r, emission.g, emission.b, emissionStrength),
            .roughness = roughness,
            .metallic = metallic
        };
    }

    //verify loading worked
    if(!(returnData.models.size() && returnData.materialInstancesData.size() && returnData.camera))
    {
        throw std::runtime_error("glTF loading falied because either no models or materials were loaded, or no camera existed in the glTF");
    }
    
    return returnData;
}

//point light definition
struct PointLight
{
    glm::vec3 position;
    glm::vec3 color;
    float radius;
    bool castShadow;
};

std::unique_ptr<PaperRenderer::Buffer> createPointLightsBuffer(PaperRenderer::RenderEngine& renderer)
{
    std::vector<PointLight> pointLightsData = {
        { glm::vec3(10.0f, 10.0, 5.0f),   glm::vec3(0.6f, 1.0f, 0.8f), 0.1f, true},
        { glm::vec3(10.0f, -10.0, 5.0f),  glm::vec3(1.0f, 0.8f, 0.8f), 0.1f, true},
        { glm::vec3(-10.0f, 10.0, 5.0f),  glm::vec3(0.6f, 0.8f, 1.0f), 0.1f, true},
        { glm::vec3(-10.0f, -10.0, 5.0f), glm::vec3(0.8f, 1.0f, 0.6f), 0.1f, true}
    };

    PaperRenderer::BufferInfo pointLightBufferInfo = {
        .size = sizeof(PointLight) * pointLightsData.size(),
        .usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR,
        .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
    };
    std::unique_ptr<PaperRenderer::Buffer> pointLightBuffer(std::make_unique<PaperRenderer::Buffer>(renderer, pointLightBufferInfo));

    PaperRenderer::BufferWrite pointLightsWrite = {
        .offset = 0,
        .size = sizeof(PointLight) * pointLightsData.size(),
        .data = pointLightsData.data()
    };
    pointLightBuffer->writeToBuffer({ pointLightsWrite });

    return pointLightBuffer;
}

//lighting uniform buffer
struct LightInfo
{
    glm::vec4 ambientLight;
    uint32_t pointLightCount;
};

std::unique_ptr<PaperRenderer::Buffer> createLightInfoUniformBuffer(PaperRenderer::RenderEngine& renderer)
{
    LightInfo uniformBufferData = {
        .ambientLight = glm::vec4(0.05f, 0.05f, 0.05f, 1.0f),
        .pointLightCount = 4
    };

    PaperRenderer::BufferInfo uniformBufferInfo = {
        .size = sizeof(LightInfo),
        .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
        .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
    };
    std::unique_ptr<PaperRenderer::Buffer> uniformBuffer(std::make_unique<PaperRenderer::Buffer>(renderer, uniformBufferInfo));

    PaperRenderer::BufferWrite pointLightsWrite = {
        .offset = 0,
        .size = sizeof(LightInfo),
        .data = &uniformBufferData
    };
    uniformBuffer->writeToBuffer({ pointLightsWrite });

    return uniformBuffer;
}

//----------UBOs----------//

void updateUniformBuffers(PaperRenderer::RenderEngine& renderer, PaperRenderer::Camera& camera, ExampleRayTracing& exampleRayTrace)
{
    //update camera
    const glm::vec3 newCameraPosition = glm::vec3(15.0f * sin(glfwGetTime()), 15.0f * cos(glfwGetTime()), 5.0f);

    PaperRenderer::CameraTransformation newTransform = {
        .translationParameters = {
            .rotationType = PaperRenderer::CameraRotationType::QUATERNION,
            .rotation = { .qRotation = glm::lookAt(newCameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f)) },
            .position = newCameraPosition
        }
    };
    camera.updateView(newTransform, PaperRenderer::CameraTransformationType::PARAMETERS);
    camera.updateUBO();

    //update RT
    exampleRayTrace.updateUBO();
}





int main()
{
    //pre-declare rendering buffers and glTF scene for callback function lambda recordings
    HDRBuffer hdrBuffer = {};
    DepthBuffer depthBuffer = {};
    SceneData scene = {};

    //----------RENDERER INITIALIZATION----------//

    //log event callback
    const auto logCallbackFunction = [&](PaperRenderer::RenderEngine& renderer, const PaperRenderer::LogEvent& event) {
        const char* beginString = "PAPER RENDERER LOG ";

        switch(event.type)
        {
        case PaperRenderer::LogType::INFO:
            std::cout << beginString << "\033[1;37m--INFO--: \033[0m";
            break;
        case PaperRenderer::LogType::WARNING:
            std::cout << beginString << "\033[1;33m--WARNING--: \033[0m";
            break;
        case PaperRenderer::LogType::ERROR:
            std::cout << beginString << "\033[1;31m--ERROR--: \033[0m";
            break;
        }

        std::cout << event.text << std::endl;
    };

    //swapchain resize callback
    const auto swapchainResizeFunction = [&](PaperRenderer::RenderEngine& renderer, VkExtent2D newExtent) {
        //destroy old HDR buffer
        vkDestroySampler(renderer.getDevice().getDevice(), hdrBuffer.sampler, nullptr);
        vkDestroyImageView(renderer.getDevice().getDevice(), hdrBuffer.view, nullptr);
        hdrBuffer.image.reset();

        //create new HDR buffer
        hdrBuffer = getHDRBuffer(renderer, VK_IMAGE_LAYOUT_GENERAL);

        //destroy old depth buffer
        vkDestroyImageView(renderer.getDevice().getDevice(), depthBuffer.view, nullptr);
        depthBuffer.image.reset();

        //create new depth buffer
        depthBuffer = getDepthBuffer(renderer);

        //update camera
        const PaperRenderer::CameraProjection newProjection = {
            //.orthographic = { .xyScale = glm::vec2(30.0f, 30.0f) }
            .perspective = { .yFov = scene.camera->getCameraInfo().projection.perspective.yFov }
        };
        scene.camera->updateProjection(newProjection, PaperRenderer::CameraProjectionType::PERSPECTIVE);
    };

    //initialize renderer
    const PaperRenderer::RendererCreationStruct rendererInfo = {
        .logEventCallbackFunction = logCallbackFunction,
        .swapchainRebuildCallbackFunction = swapchainResizeFunction,
        .rasterPreprocessSpirv = readFromFile("../resources/shaders/IndirectDrawBuild.spv"),
        .rtPreprocessSpirv = readFromFile("../resources/shaders/TLASInstBuild.spv"),
        .deviceInstanceInfo = {
            .appName = "PaperRenderer Example",
            .engineName = "PaperRenderer"
        },
        .windowState = {
            .windowName = "PaperRenderer Example"
        }
    };
    PaperRenderer::RenderEngine renderer(rendererInfo);

    //----------GLTF SCENE LOADING----------//

    //load glTF scene
    scene = loadSceneData(renderer);

    //----------HDR & DEPTH RENDERING BUFFER----------//

    //get HDR buffer
    hdrBuffer = getHDRBuffer(renderer, VK_IMAGE_LAYOUT_GENERAL);

    //get depth buffer
    depthBuffer = getDepthBuffer(renderer);

    //----------UNIFORM AND STORAGE BUFFERS----------//

    //lgihting buffers
    std::unique_ptr<PaperRenderer::Buffer> pointLightsBuffer = createPointLightsBuffer(renderer);
    std::unique_ptr<PaperRenderer::Buffer> lightingUniformBuffer = createLightInfoUniformBuffer(renderer);
    
    //----------RENDER PASSES----------//

    //ray tracing
    ExampleRayTracing exampleRayTrace(renderer, *scene.camera, hdrBuffer, *pointLightsBuffer, *lightingUniformBuffer);
    
    //raster
    ExampleRaster exampleRaster(renderer, *scene.camera, hdrBuffer, depthBuffer, *pointLightsBuffer, *lightingUniformBuffer);
    
    //HDR buffer copy render pass
    BufferCopyPass bufferCopyPass(renderer, *scene.camera, hdrBuffer);

    //----------MODEL INSTANCES----------//

    std::vector<std::unique_ptr<PaperRenderer::ModelInstance>> modelInstances;
    modelInstances.reserve(scene.models.size());

    //create 1 instance per model for this example
    for(const auto& [name, model] : scene.models)
    {
        //unique geometry is false because animation is currently unavailable
        std::unique_ptr<PaperRenderer::ModelInstance> instance = std::make_unique<PaperRenderer::ModelInstance>(renderer, *model, false);

        //set transformation
        instance->setTransformation(scene.instanceTransforms[model.get()]);
        modelInstances.push_back(std::move(instance));
    }

    //----------MATERIALS----------//

    //base RT material
    PaperRenderer::ShaderHitGroup baseMaterialHitGroup = {
        .chitShaderData = readFromFile("resources/shaders/raytrace_chit.spv"),
        .ahitShaderData = {}, //TODO FOR TRANSPARENCY
        .intShaderData = {}
    };
    PaperRenderer::RTMaterial baseRTMaterial(renderer, baseMaterialHitGroup);

    //create material instances that "derive" from base material and the loaded scene data parameters
    std::unordered_map<std::string, std::unique_ptr<PaperRenderer::MaterialInstance>> materialInstances;
    materialInstances.reserve(scene.materialInstancesData.size());
    for(const auto& [name, parameters] : scene.materialInstancesData)
    {
        materialInstances[name] = std::make_unique<DefaultMaterialInstance>(renderer, exampleRaster.getDefaultMaterial(), parameters);

        //TODO RT MATERIAL "INSTANCE" IMPLEMENTATION

    }

    //----------ADD MODEL INSTANCES TO RT AND RASTER PASSES----------//

    std::vector<DefaultRTMaterialDefinition> instanceRTMaterialDefinitions;
    for(uint32_t i = 0; i < modelInstances.size(); i++)
    {
        //raster render pass
        std::unordered_map<uint32_t, PaperRenderer::MaterialInstance*> materials;
        uint32_t matIndex = 0;
        for(const std::string& matName : scene.instanceMaterials[modelInstances[i]->getParentModel().getModelName()])
        {
            materials[matIndex] = materialInstances[matName].get();
            matIndex++;
        }
        exampleRaster.getRenderPass().addInstance(*modelInstances[i], { materials });

        //rt render pass (just use the base RT material for simplicity)
        const PaperRenderer::AccelerationStructureInstanceData asInstanceData = {
            .instancePtr = modelInstances[i].get(),
            .customIndex = (uint32_t)instanceRTMaterialDefinitions.size(), //set custom index to the first material index in the buffer
            .mask = 0xFF,
            .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR
        };
        exampleRayTrace.getRTRender().addInstance(asInstanceData, baseRTMaterial);

        //RT instance materials
        for(const std::string& matName : scene.instanceMaterials[modelInstances[i]->getParentModel().getModelName()])
        {
            instanceRTMaterialDefinitions.push_back({
                .albedo = glm::vec3(scene.materialInstancesData[matName].baseColor),
                .emissive = glm::vec3(scene.materialInstancesData[matName].emission) * scene.materialInstancesData[matName].emission.w,
                .metallic = scene.materialInstancesData[matName].metallic,
                .roughness = scene.materialInstancesData[matName].roughness,
                .transmission = glm::vec3(0.0f),
                .ior = 1.45f
            });
        }
    }

    //custom RT material buffer
    const PaperRenderer::BufferInfo rtMaterialDefinitionsBufferInfo = {
        .size = instanceRTMaterialDefinitions.size() * sizeof(DefaultRTMaterialDefinition),
        .usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT,
        .allocationFlags = 0
    };
    PaperRenderer::Buffer rtMaterialDefinitionsBuffer(renderer, rtMaterialDefinitionsBufferInfo);

    //convert material data vector to char vector (honestly there can be a better way around this but im lazy)
    std::vector<char> instanceRTMaterialDefinitionsData(instanceRTMaterialDefinitions.size() * sizeof(DefaultRTMaterialDefinition));
    memcpy(instanceRTMaterialDefinitionsData.data(), instanceRTMaterialDefinitions.data(), instanceRTMaterialDefinitions.size() * sizeof(DefaultRTMaterialDefinition));

    //queue data transfer for RT material data
    renderer.getStagingBuffer().queueDataTransfers(rtMaterialDefinitionsBuffer, 0, instanceRTMaterialDefinitionsData);

    //----------MISC----------//

    //init GUI
    GuiContext guiContext = initImGui(renderer);

    //----------RENDER LOOP----------//

    //synchronization
    uint64_t finalSemaphoreValue = 0;
    VkSemaphore renderingSemaphore = renderer.getDevice().getCommands().getTimelineSemaphore(finalSemaphoreValue);
    VkSemaphore presentationSemaphore = renderer.getDevice().getCommands().getSemaphore();

    //rendering loop functions
    auto waitSemaphoreFunction = [&]()
    {
        const std::vector<VkSemaphore> toWaitSemaphores = { renderingSemaphore, renderer.getStagingBuffer().getTransferSemaphore().semaphore };
        const std::vector<uint64_t> toWaitSemaphoreValues = { finalSemaphoreValue, renderer.getStagingBuffer().getTransferSemaphore().value };
        VkSemaphoreWaitInfo beginWaitInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .pNext = NULL,
            .flags = 0,
            .semaphoreCount = (uint32_t)toWaitSemaphores.size(),
            .pSemaphores = toWaitSemaphores.data(),
            .pValues = toWaitSemaphoreValues.data()
        };
        vkWaitSemaphores(renderer.getDevice().getDevice(), &beginWaitInfo, UINT64_MAX);

        //begin frame
        return renderer.beginFrame();
    };

    bool raster = false;
    while(!glfwWindowShouldClose(renderer.getSwapchain().getGLFWwindow()))
    {
        //get last frame statistics (create copy since it WILL be cleared after renderer.beginFrame())
        PaperRenderer::Statistics lastFrameStatistics = renderer.getStatisticsTracker().getStatistics();

        //block this thread and while waiting for the begin function, no more work to do BIG OL TODO WE ASYNC-ING
        VkSemaphore swapchainSemaphore = waitSemaphoreFunction();//waitSemaphoreFuture.get();
        
        //update uniform buffers
        updateUniformBuffers(renderer, *scene.camera, exampleRayTrace);

        //remember to explicitly submit the staging buffer transfers (do entire submit in this case)
        const PaperRenderer::SynchronizationInfo transferSyncInfo = {
            .queueType = PaperRenderer::QueueType::TRANSFER,
        };
        renderer.getStagingBuffer().submitQueuedTransfers(transferSyncInfo);

        //ray tracing
        if(!raster)
        {
            //build queued BLAS's (wait on transfer, signal rendering semaphore
            const PaperRenderer::SynchronizationInfo blasSyncInfo = {
                .queueType = PaperRenderer::QueueType::COMPUTE,
                .timelineWaitPairs = { { renderer.getStagingBuffer().getTransferSemaphore() } },
                .timelineSignalPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR, finalSemaphoreValue + 1 } }
            };
            renderer.getAsBuilder().submitQueuedOps(blasSyncInfo, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);
            
            //update tlas (wait for BLAS build, signal rendering semaphore)
            const PaperRenderer::SynchronizationInfo tlasSyncInfo = {
                .queueType = PaperRenderer::QueueType::COMPUTE,
                .timelineWaitPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR, finalSemaphoreValue + 1 } },
                .timelineSignalPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR, finalSemaphoreValue + 2 } }
            };
            exampleRayTrace.getRTRender().updateTLAS(VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, tlasSyncInfo);

            //render pass (wait for TLAS build, signal rendering semaphore)
            PaperRenderer::SynchronizationInfo rtRenderSync = {
                .queueType = PaperRenderer::QueueType::COMPUTE,
                .timelineWaitPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, finalSemaphoreValue + 2 } },
                .timelineSignalPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, finalSemaphoreValue + 3 } }
            };
            exampleRayTrace.rayTraceRender(rtRenderSync, rtMaterialDefinitionsBuffer);
        }
        //raster
        else
        {
            //render pass (wait on transfer, signal rendering semaphore)
            const PaperRenderer::SynchronizationInfo rasterSyncInfo = {
                .queueType = PaperRenderer::QueueType::GRAPHICS,
                .timelineWaitPairs = { { renderer.getStagingBuffer().getTransferSemaphore() } },
                .timelineSignalPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, finalSemaphoreValue + 3 } }
            };
            exampleRaster.rasterRender(rasterSyncInfo);
        }

        //copy HDR buffer to swapchain (wait for render pass and swapchain, signal rendering and presentation semaphores)
        const PaperRenderer::SynchronizationInfo bufferCopySyncInfo = {
            .queueType = PaperRenderer::QueueType::GRAPHICS,
            .binaryWaitPairs = { { swapchainSemaphore, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT } },
            .timelineWaitPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, finalSemaphoreValue + 3 } },
            .timelineSignalPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, finalSemaphoreValue + 4 } }
        };
        bufferCopyPass.render(bufferCopySyncInfo, raster);

        //render GUI
        const PaperRenderer::SynchronizationInfo guiSyncInfo = {
            .queueType = PaperRenderer::QueueType::GRAPHICS,
            .binarySignalPairs = { { presentationSemaphore, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT } },
            .timelineWaitPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, finalSemaphoreValue + 4 } },
            .timelineSignalPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, finalSemaphoreValue + 5 } }
        };
        renderImGui(&renderer, &lastFrameStatistics, &guiContext, guiSyncInfo); //TODO THIS IS A MASSIVE HOST SYNC VIOLATION WITH QUEUES SINCE GUI DOESNT TAKE OWNERSHIP OF ITS QUEUE

        //end frame
        renderer.endFrame({ presentationSemaphore });

        //increment final semaphore value to wait on
        finalSemaphoreValue += 5;
    }

    //wait for rendering
    vkDeviceWaitIdle(renderer.getDevice().getDevice());

    //destroy ImGui
    destroyImGui();

    //destroy hdr and depth buffers
    hdrBuffer.image.reset();
    depthBuffer.image.reset();

    //destroy scene info
    scene = {};

    //destroy light buffers
    lightingUniformBuffer.reset();
    pointLightsBuffer.reset();

    //destroy some stuff
    vkDestroySemaphore(renderer.getDevice().getDevice(), presentationSemaphore, nullptr);
    vkDestroySemaphore(renderer.getDevice().getDevice(), renderingSemaphore, nullptr);
    vkDestroyImageView(renderer.getDevice().getDevice(), hdrBuffer.view, nullptr);
    vkDestroyImageView(renderer.getDevice().getDevice(), depthBuffer.view, nullptr);
    vkDestroySampler(renderer.getDevice().getDevice(), hdrBuffer.sampler, nullptr);
    
    return 0;
}