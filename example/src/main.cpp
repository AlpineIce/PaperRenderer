#include "Common.h"
#include "GuiRender.h"
#include "RenderPasses.h"

//tinygltf
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "lib/tinygltf/tiny_gltf.h"

#include <random>

//----------SCENE LOADING FROM GLTF----------//

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
                .modelName = modelName,
                .bounds = {}
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

            PaperRenderer::CameraTransformationParameters newTransform = {
                .rotation = glm::lookAt(newCameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
                .position = newCameraPosition
            };

            const PaperRenderer::CameraInfo cameraInfo = {
                .projection = PaperRenderer::PerspectiveCamera((float)camera.perspective.yfov * 100.0f),
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
        const float roughness = material.values.count("roughnessFactor") ? material.values.at("roughnessFactor").Factor() : 1.0f;
        const float metallic = material.values.count("metallicFactor") ? material.values.at("metallicFactor").Factor() : 1.0f;

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

//----------LIGHTING BUFFERS AND DESCRIPTORS----------//

//point light definition
struct PointLight
{
    glm::vec3 position;
    glm::vec3 color;
    float radius;
    float bounds;
    bool castShadow;
};

std::unique_ptr<PaperRenderer::Buffer> createPointLightsBuffer(PaperRenderer::RenderEngine& renderer)
{
    std::vector<PointLight> pointLightsData = {
        { glm::vec3(10.0f, 10.0, 5.0f),   glm::vec3(100.0f, 100.0f, 100.0f), 0.1f, 100.0f, true},
        { glm::vec3(10.0f, -10.0, 5.0f),  glm::vec3(100.0f, 100.0f, 100.0f), 0.1f, 100.0f, true},
        { glm::vec3(-10.0f, 10.0, 5.0f),  glm::vec3(100.0f, 100.0f, 100.0f), 0.1f, 100.0f, true},
        { glm::vec3(-10.0f, -10.0, 5.0f), glm::vec3(100.0f, 100.0f, 100.0f), 0.1f, 100.0f, true}
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
        .readData = pointLightsData.data()
    };
    pointLightBuffer->writeToBuffer({ pointLightsWrite });

    return pointLightBuffer;
}

//lighting uniform buffer
struct LightInfo
{
    glm::vec4 ambientLight;
    uint32_t pointLightCount;
    float padding[11];
};

std::unique_ptr<PaperRenderer::Buffer> createLightInfoUniformBuffer(PaperRenderer::RenderEngine& renderer)
{
    LightInfo uniformBufferData = {
        .ambientLight = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f),
        .pointLightCount = 4
    };

    PaperRenderer::BufferInfo uniformBufferInfo = {
        .size = sizeof(LightInfo) * 2,
        .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
        .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
    };
    std::unique_ptr<PaperRenderer::Buffer> uniformBuffer(std::make_unique<PaperRenderer::Buffer>(renderer, uniformBufferInfo));

    PaperRenderer::BufferWrite pointLightsWrite = {
        .offset = 0,
        .size = sizeof(LightInfo),
        .readData = &uniformBufferData
    };
    uniformBuffer->writeToBuffer({ pointLightsWrite });

    return uniformBuffer;
}

LightingData createLightingData(PaperRenderer::RenderEngine& renderer)
{
    //lighting buffers
    std::unique_ptr<PaperRenderer::Buffer> pointLightsBuffer = createPointLightsBuffer(renderer);
    std::unique_ptr<PaperRenderer::Buffer> lightingUniformBuffer = createLightInfoUniformBuffer(renderer);

    //descriptor layout
    VkDescriptorSetLayout lightingDescriptorLayout = renderer.getDescriptorAllocator().createDescriptorSetLayout({
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
            .pImmutableSamplers = NULL
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
            .pImmutableSamplers = NULL
        }
    });

    //descriptor
    std::unique_ptr<PaperRenderer::ResourceDescriptor> lightingDescriptor = std::make_unique<PaperRenderer::ResourceDescriptor>(renderer, lightingDescriptorLayout);

    //descriptor writes
    lightingDescriptor->updateDescriptorSet({
        .bufferWrites = {
            {
                .infos = { {
                    .buffer = lightingUniformBuffer->getBuffer(),
                    .offset = 0,
                    .range = VK_WHOLE_SIZE
                } },
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .binding = 0
            },
            {
                .infos = { {
                    .buffer = pointLightsBuffer->getBuffer(),
                    .offset = 0,
                    .range = VK_WHOLE_SIZE
                } },
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .binding = 1
            }
        }
    });

    //return
    return {
        std::move(lightingUniformBuffer),
        std::move(pointLightsBuffer),
        lightingDescriptorLayout,
        std::move(lightingDescriptor),
    };
}

//----------UBO UPDATE FUNCTION----------//

void updateUniformBuffers(PaperRenderer::RenderEngine& renderer, PaperRenderer::Camera& camera, DefaultMaterialInstance& material, ExampleRayTracing& exampleRayTrace, BufferCopyPass& bufferCopyPass)
{
    //update camera
    const glm::vec3 newCameraPosition = glm::vec3(15.0f * sin(glfwGetTime() * 0.1f), 15.0f * cos(glfwGetTime() * 0.1f), 5.0f);

    PaperRenderer::CameraTransformationParameters newTransform = {
        .rotation = glm::lookAt(newCameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
        .position = newCameraPosition
    };
    camera.updateView(newTransform);
    camera.updateUBO();

    //update material
    material.updateUBO();

    //update buffer copy pass
    bufferCopyPass.updateUBO();
}

//----------MAIN----------//

int main()
{
    //pre-declare rendering buffers and glTF scene for callback function lambda recordings
    HDRBuffer hdrBuffer = {};
    DepthBuffer depthBuffer = {};
    SceneData scene = {};
    ExampleRayTracing const* exampleRtPtr = NULL;
    BufferCopyPass const* bufferCopyPassPtr = NULL;

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
        case PaperRenderer::LogType::CRITICAL_ERROR:
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

        //update RT descriptor
        exampleRtPtr->updateHDRBuffer();

        //update buffer copy descriptor
        bufferCopyPassPtr->updateHDRBuffer();

        //destroy old depth buffer
        vkDestroyImageView(renderer.getDevice().getDevice(), depthBuffer.view, nullptr);
        depthBuffer.image.reset();

        //create new depth buffer
        depthBuffer = getDepthBuffer(renderer);

        //update camera
        const PaperRenderer::PerspectiveCamera newProjection = {
            //.xyScale = glm::vec2(30.0f, 30.0f)
            .yFov = std::get<PaperRenderer::PerspectiveCamera>(scene.camera->getCameraInfo().projection).yFov
        };
        scene.camera->updateProjection(newProjection);
    };

    //initialize renderer
    const PaperRenderer::PaperRendererInfo rendererInfo = {
        .logEventCallbackFunction = logCallbackFunction,
        .swapchainRebuildCallbackFunction = swapchainResizeFunction,
        .rasterPreprocessSpirv = readFromFile("../resources/shaders/IndirectDrawBuild.spv"),
        .rtPreprocessSpirv = readFromFile("../resources/shaders/TLASInstBuild.spv"),
        .deviceInstanceInfo = {
            .appName = "PaperRenderer Example",
            .engineName = "PaperRenderer"
        },
        .windowState = {
            .windowName = "PaperRenderer Example",
            .enableVsync = false
        }
    };
    PaperRenderer::RenderEngine renderer(rendererInfo);

    //----------GLTF SCENE LOADING----------//

    //load glTF scene
    scene = loadSceneData(renderer);

    //----------LIGHTING DATA----------//

    LightingData lightingData = createLightingData(renderer);

    //----------HDR & DEPTH RENDERING BUFFER----------//

    //get HDR buffer
    hdrBuffer = getHDRBuffer(renderer, VK_IMAGE_LAYOUT_GENERAL);

    //get depth buffer
    depthBuffer = getDepthBuffer(renderer);
    
    //----------RENDER PASSES----------//

    //ray tracing
    ExampleRayTracing exampleRayTrace(renderer, *scene.camera, hdrBuffer, lightingData);
    exampleRtPtr = &exampleRayTrace;
    
    //raster
    ExampleRaster exampleRaster(renderer, *scene.camera, hdrBuffer, depthBuffer, lightingData);
    
    //HDR buffer copy render pass
    BufferCopyPass bufferCopyPass(renderer, *scene.camera, hdrBuffer);
    bufferCopyPassPtr = &bufferCopyPass;

    //----------EXTRA MATERIALS----------//

    //leaf raster material
    DefaultMaterial leafMaterial(renderer, {
            .shaderInfo = {
                {
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .data = readFromFile("resources/shaders/Default_vert.spv")
                },
                {
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .data = readFromFile("resources/shaders/leaf_frag.spv")
                }
            },
            .descriptorSets = {
                { 0, renderer.getDefaultDescriptorSetLayout(PaperRenderer::DefaultDescriptors::CAMERA_MATRICES) },
                { 1, lightingData.lightingDescriptorLayout },
                { 2, exampleRaster.getParametersDescriptorSetLayout() }, //ownership of this one is kinda weird, I will admit that
                { 3, renderer.getDefaultDescriptorSetLayout(PaperRenderer::DefaultDescriptors::INDIRECT_DRAW_MATRICES) }
            },
            .pcRanges = {}, //no push constants
            .properties = {
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
                .vertexDescriptions = {
                    {
                        .binding = 0,
                        .stride = sizeof(Vertex),
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                    }
                },
                .colorAttachments = {
                    {
                        .blendEnable = VK_FALSE,
                        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                        .colorBlendOp = VK_BLEND_OP_ADD,
                        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                        .alphaBlendOp = VK_BLEND_OP_ADD,
                        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
                    }
                },
                .colorAttachmentFormats = {
                    { hdrBuffer.format }
                },
                .depthAttachmentFormat = depthBuffer.format,
                .rasterInfo = {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                    .pNext = NULL,
                    .flags = 0,
                    .depthClampEnable = VK_FALSE,
                    .rasterizerDiscardEnable = VK_FALSE,
                    .polygonMode = VK_POLYGON_MODE_FILL,
                    .cullMode = VK_CULL_MODE_NONE, //This is foliage so no backface culling will be used. Funny enough the grass doesnt use this material and culling is therefore enabled when it shouldnt be
                    .frontFace = VK_FRONT_FACE_CLOCKWISE,
                    .depthBiasEnable = VK_FALSE,
                    .depthBiasConstantFactor = 0.0f,
                    .depthBiasClamp = 0.0f,
                    .depthBiasSlopeFactor = 0.0f,
                    .lineWidth = 1.0f
                }
            }
        },
        lightingData
    );

    //base RT material
    PaperRenderer::ShaderHitGroup baseMaterialHitGroup = {
        .chitShaderData = readFromFile("resources/shaders/raytrace_chit.spv"),
        .ahitShaderData = {}, //TODO FOR TRANSPARENCY
        .intShaderData = {}
    };
    PaperRenderer::RTMaterial baseRTMaterial(renderer, baseMaterialHitGroup);

    //leaf RT material
    PaperRenderer::ShaderHitGroup leafMaterialHitGroup=  {
        .chitShaderData = readFromFile("resources/shaders/leaf_chit.spv"),
        .ahitShaderData = readFromFile("resources/shaders/leaf_ahit.spv"),
        .intShaderData = {}
    };
    PaperRenderer::RTMaterial leafRTMaterial(renderer, leafMaterialHitGroup);

    //RT material definitions
    std::vector<DefaultRTMaterialDefinition> instanceRTMaterialDefinitions;
    uint32_t adjustableMaterialIndex = 0; //for ImGUI
    uint32_t raindropMaterialIndex = 0; //for raindrops test

    //create material instances
    std::unordered_map<std::string, std::unique_ptr<DefaultMaterialInstance>> materialInstances;
    materialInstances.reserve(scene.materialInstancesData.size());
    for(const auto& [name, parameters] : scene.materialInstancesData)
    {
        if(name == "Leaves")
        {
            materialInstances[name] = std::make_unique<DefaultMaterialInstance>(renderer, leafMaterial, parameters, exampleRaster.getParametersDescriptorSetLayout());
        }
        else
        {
            materialInstances[name] = std::make_unique<DefaultMaterialInstance>(renderer, exampleRaster.getDefaultMaterial(), parameters, exampleRaster.getParametersDescriptorSetLayout());
        }
    }

    //----------MODEL INSTANCES----------//
    
    std::unordered_map<std::string, std::vector<std::unique_ptr<PaperRenderer::ModelInstance>>> modelInstances;

    auto addInstanceToRenderPass = [&](PaperRenderer::ModelInstance& instance, const PaperRenderer::RTMaterial& rtMaterial, bool sorted, uint32_t customIndexOverride=UINT32_MAX)
    {
        //raster
        std::unordered_map<uint32_t, PaperRenderer::MaterialInstance*> materials;
        uint32_t matIndex = 0;
        for(const std::string& matName : scene.instanceMaterials[instance.getParentModel().getModelName()])
        {
            materials[matIndex] = &materialInstances[matName]->getMaterialInstance();
            matIndex++;
        }
        exampleRaster.getRenderPass().addInstance(instance, { materials }, sorted);

        //RT
        const PaperRenderer::AccelerationStructureInstanceData asInstanceData = {
            .instancePtr = &instance,
            .customIndex = customIndexOverride == UINT32_MAX ? (uint32_t)instanceRTMaterialDefinitions.size() : customIndexOverride, //set custom index to the first material index in the buffer
            .mask = 0xFF,
            .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR
        };
        exampleRayTrace.getRTRender().addInstance(asInstanceData, rtMaterial);

        //RT instance materials
        if(customIndexOverride == UINT32_MAX)
        {
            for(const std::string& matName : scene.instanceMaterials[instance.getParentModel().getModelName()])
            {
                //for raindrop
                if(instance.getParentModel().getModelName() == "Drop") raindropMaterialIndex = (uint32_t)instanceRTMaterialDefinitions.size();

                //for ImGUI
                if(matName == "MetalBall") adjustableMaterialIndex = (uint32_t)instanceRTMaterialDefinitions.size();

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
    };

    //create a ring of suzanne model instances
    if(scene.models.count("Suzanne"))
    {
        const uint32_t instanceCount = 8;
        for(uint32_t i = 0; i < instanceCount; i++)
        {
            std::unique_ptr<PaperRenderer::ModelInstance> instance = std::make_unique<PaperRenderer::ModelInstance>(renderer, *scene.models["Suzanne"], false);

            //set transformation
            PaperRenderer::ModelTransformation newTransform = {
                .position = glm::vec3(sin(glm::radians(360.0f / instanceCount) * i) * 5.0f, cos(glm::radians(360.0f / instanceCount) * i) * 5.0f, 0.0f),
                .scale = glm::vec3(1.0f),
                .rotation = glm::quat(sin(-glm::radians(360.0f / instanceCount / 2.0f) * i), cos(glm::radians(360.0f / instanceCount / 2.0f) * i), 0.0f, 0.0f)
            };
            instance->setTransformation(newTransform);

            //add to render passes
            addInstanceToRenderPass(*instance, baseRTMaterial, false);

            //push to model instances
            modelInstances["Suzanne"].push_back(std::move(instance));
        }
    }

    //create a ring of trees
    if(scene.models.count("Tree"))
    {
        const uint32_t instanceCount = 4;
        for(uint32_t i = 0; i < instanceCount; i++)
        {
            std::unique_ptr<PaperRenderer::ModelInstance> instance = std::make_unique<PaperRenderer::ModelInstance>(renderer, *scene.models["Tree"], false);

            //set transformation
            PaperRenderer::ModelTransformation newTransform = {
                .position = glm::vec3(sin(glm::radians(360.0f / instanceCount) * i + (3.14f / 4.0f)) * 20.0f, cos(glm::radians(360.0f / instanceCount) * i + (3.14f / 4.0f)) * 20.0f , -3.0f),
                .scale = glm::vec3(1.0f),
                .rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
            };
            instance->setTransformation(newTransform);
            

            //add to render passes
            addInstanceToRenderPass(*instance, leafRTMaterial, false);

            //push to model instances
            modelInstances["Tree"].push_back(std::move(instance));
        }
    }

    //create ring of translucent objects
    if(scene.models.count("TranslucentObject"))
    {
        const uint32_t instanceCount = 4;
        for(uint32_t i = 0; i < instanceCount; i++)
        {
            std::unique_ptr<PaperRenderer::ModelInstance> instance = std::make_unique<PaperRenderer::ModelInstance>(renderer, *scene.models["TranslucentObject"], false);

            //set transformation
            PaperRenderer::ModelTransformation newTransform = {
                .position = glm::vec3(sin(glm::radians(360.0f / instanceCount) * i) * 0.6f, cos(glm::radians(360.0f / instanceCount) * i) * 0.6f, -2.0f),
                .scale = glm::vec3(1.0f),
                .rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
            };
            instance->setTransformation(newTransform);

            //add to render passes
            addInstanceToRenderPass(*instance, baseRTMaterial, true); //sort because translucency

            //push to model instances
            modelInstances["TranslucentObject"].push_back(std::move(instance));
        }
    }

    //sorted metal ball
    if(scene.models.count("MetalBall"))
    {
        std::unique_ptr<PaperRenderer::ModelInstance> instance = std::make_unique<PaperRenderer::ModelInstance>(renderer, *scene.models["MetalBall"], false);

        //set transformation
        instance->setTransformation(scene.instanceTransforms[scene.models["MetalBall"].get()]);

        //add to render passes
        addInstanceToRenderPass(*instance, baseRTMaterial, true);

        //push to model instances
        modelInstances["MetalBall"].push_back(std::move(instance));
    }

    //everything else (including one drop lol)
    for(const auto& [name, model] : scene.models)
    {
        if(!modelInstances.count(name))
        {
            std::unique_ptr<PaperRenderer::ModelInstance> instance = std::make_unique<PaperRenderer::ModelInstance>(renderer, *model, false);

            //set transformation
            instance->setTransformation(scene.instanceTransforms[model.get()]);

            //add to render passes
            addInstanceToRenderPass(*instance, baseRTMaterial, false);

            //push to model instances
            modelInstances[name].push_back(std::move(instance));
        }
    }

    //custom RT material buffer
    const PaperRenderer::BufferInfo rtMaterialDefinitionsBufferInfo = {
        .size = instanceRTMaterialDefinitions.size() * sizeof(DefaultRTMaterialDefinition),
        .usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
        .allocationFlags = 0
    };
    PaperRenderer::Buffer rtMaterialDefinitionsBuffer(renderer, rtMaterialDefinitionsBufferInfo);

    //convert material data vector to char vector (honestly there can be a better way around this but im lazy)
    std::vector<char> instanceRTMaterialDefinitionsData(instanceRTMaterialDefinitions.size() * sizeof(DefaultRTMaterialDefinition));
    memcpy(instanceRTMaterialDefinitionsData.data(), instanceRTMaterialDefinitions.data(), instanceRTMaterialDefinitions.size() * sizeof(DefaultRTMaterialDefinition));

    //queue data transfer for RT material data
    renderer.getStagingBuffer().queueDataTransfers(rtMaterialDefinitionsBuffer, 0, instanceRTMaterialDefinitionsData);

    //update descriptor
    exampleRayTrace.updateMaterialBuffer(rtMaterialDefinitionsBuffer);

    //----------MISC----------//

    //init GUI
    GuiContext guiContext = initImGui(renderer, *materialInstances.at("MetalBall"));

    //raindrops deque
    std::deque<std::unique_ptr<PaperRenderer::ModelInstance>> rainDrops;

    auto frameEvents = [&]()
    {
        const float heightThreshold = -5.0f;
        const float dropSpeed = 1.0f;
        const float deltaTime = renderer.getDeltaTime();

        //make drops fall
        for(std::unique_ptr<PaperRenderer::ModelInstance>& instance : rainDrops)
        {
            PaperRenderer::ModelTransformation transformation = instance->getTransformation();
            transformation.position.z -= dropSpeed * deltaTime;

            instance->setTransformation(transformation);
        }

        //remove any fallen past threshold (will all be in front because math)
        auto it = rainDrops.begin();
        while(it != rainDrops.end())
        {
            if(it->get()->getTransformation().position.z < heightThreshold)
            {
                it++;
                rainDrops.pop_front();
            }
            else
            {
                //stop when next drop isn't below threshold because it should all be in order
                break;
            }
        }

        //RNG
        std::random_device rd;
        std::mt19937 mt(rd());
        std::uniform_int_distribution<uint32_t> intDist(0.0, 0.01 / deltaTime);
        
        //populate new raindrop if RNG is happy
        if(intDist(mt) == 0)
        {
            //create new instance
            std::unique_ptr<PaperRenderer::ModelInstance> newInstance = std::make_unique<PaperRenderer::ModelInstance>(renderer, *scene.models["Drop"], false);

            //set transformation from RNG
            std::uniform_real_distribution<float> xFloatDist(-20.0f, 20.0f);
            std::uniform_real_distribution<float> yFloatDist(-20.0f, 20.0f);
            newInstance->setTransformation({ .position = glm::vec3(xFloatDist(mt), yFloatDist(mt), 10.0f) });

            //add to render passes
            addInstanceToRenderPass(*newInstance, baseRTMaterial, false, raindropMaterialIndex);

            //add to deque
            rainDrops.push_back(std::move(newInstance));
        }
    };

    //----------RENDER LOOP----------//

    //synchronization
    std::array<uint64_t, 2> finalSemaphoreValue = { 0, 0 };
    std::array<VkSemaphore, 2> renderingSemaphore = { 
        renderer.getDevice().getCommands().getTimelineSemaphore(finalSemaphoreValue[0]),
        renderer.getDevice().getCommands().getTimelineSemaphore(finalSemaphoreValue[1])
    };
    std::array<VkSemaphore, 2> presentationSemaphore = {
        renderer.getDevice().getCommands().getSemaphore(),
        renderer.getDevice().getCommands().getSemaphore()
    };

    while(!glfwWindowShouldClose(renderer.getSwapchain().getGLFWwindow()))
    {
        //pre-frame events
        frameEvents();

        //get last frame statistics (create copy since it WILL be cleared after renderer.beginFrame())
        PaperRenderer::Statistics lastFrameStatistics = renderer.getStatisticsTracker().getStatistics();

        //wait for last frame to finish rendering (last semaphore value)
        const std::vector<VkSemaphore> toWaitSemaphores = { renderingSemaphore[renderer.getBufferIndex()] };
        const std::vector<uint64_t> toWaitSemaphoreValues = { finalSemaphoreValue[renderer.getBufferIndex()] };
        VkSemaphoreWaitInfo beginWaitInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .pNext = NULL,
            .flags = 0,
            .semaphoreCount = (uint32_t)toWaitSemaphores.size(),
            .pSemaphores = toWaitSemaphores.data(),
            .pValues = toWaitSemaphoreValues.data()
        };
        vkWaitSemaphores(renderer.getDevice().getDevice(), &beginWaitInfo, UINT64_MAX);

        //get opposite buffer index (i didnt even know this was legal until i tried it)
        const uint32_t otherBufferIndex = !renderer.getBufferIndex();

        //begin frame
        VkSemaphore swapchainSemaphore = renderer.beginFrame();

        //remember to explicitly submit the staging buffer transfers (do entire submit in this case)
        const PaperRenderer::SynchronizationInfo transferSyncInfo = {
            .queueType = PaperRenderer::QueueType::TRANSFER,
            .timelineWaitPairs = { { renderingSemaphore[otherBufferIndex], VK_PIPELINE_STAGE_2_TRANSFER_BIT, finalSemaphoreValue[otherBufferIndex] } }, //make GPU wait on last frame at this point forward
            .timelineSignalPairs = { { renderingSemaphore[renderer.getBufferIndex()], VK_PIPELINE_STAGE_2_TRANSFER_BIT, finalSemaphoreValue[renderer.getBufferIndex()] + 1 } }
        };
        renderer.getStagingBuffer().submitQueuedTransfers(transferSyncInfo);

        //update uniform buffers
        updateUniformBuffers(renderer, *scene.camera, *guiContext.adjustableMaterial, exampleRayTrace, bufferCopyPass);

        //ray tracing
        if(!guiContext.raster)
        {
            //queue transfer of GUI adjustable material data
            DefaultRTMaterialDefinition newData = {
                .albedo = glm::vec3(guiContext.adjustableMaterial->getParameters().baseColor),
                .emissive = glm::vec3(guiContext.adjustableMaterial->getParameters().emission) * guiContext.adjustableMaterial->getParameters().emission.w,
                .metallic = guiContext.adjustableMaterial->getParameters().metallic,
                .roughness = guiContext.adjustableMaterial->getParameters().roughness,
                .transmission = glm::vec3(0.0f),
                .ior = 1.45f
            };

            std::vector<char> adjustableMaterialNewData(sizeof(DefaultRTMaterialDefinition));
            memcpy(adjustableMaterialNewData.data(), &newData, sizeof(DefaultRTMaterialDefinition));
            renderer.getStagingBuffer().queueDataTransfers(rtMaterialDefinitionsBuffer, adjustableMaterialIndex * sizeof(DefaultRTMaterialDefinition), adjustableMaterialNewData);

            //build queued BLAS's (wait on transfer, signal rendering semaphore
            const PaperRenderer::SynchronizationInfo blasSyncInfo = {
                .queueType = PaperRenderer::QueueType::COMPUTE,
                .timelineWaitPairs = { { renderingSemaphore[renderer.getBufferIndex()], VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, finalSemaphoreValue[renderer.getBufferIndex()] + 1 } },
                .timelineSignalPairs = { { renderingSemaphore[renderer.getBufferIndex()], VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR, finalSemaphoreValue[renderer.getBufferIndex()] + 2 } }
            };
            renderer.getAsBuilder().submitQueuedOps(blasSyncInfo);

            //update tlas (wait for BLAS build, signal rendering semaphore)
            const PaperRenderer::SynchronizationInfo tlasSyncInfo = {
                .queueType = PaperRenderer::QueueType::COMPUTE,
                .timelineWaitPairs = { { renderingSemaphore[renderer.getBufferIndex()], VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR, finalSemaphoreValue[renderer.getBufferIndex()] + 2 } },
                .timelineSignalPairs = { { renderingSemaphore[renderer.getBufferIndex()], VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, finalSemaphoreValue[renderer.getBufferIndex()] + 3 } }
            };
            exampleRayTrace.getRTRender().updateTLAS(VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, tlasSyncInfo);

            //update UBO after TLAS is built
            exampleRayTrace.updateUBO();

            //render pass (wait for TLAS build, signal rendering semaphore)
            PaperRenderer::SynchronizationInfo rtRenderSync = {
                .queueType = PaperRenderer::QueueType::COMPUTE,
                .timelineWaitPairs = { { renderingSemaphore[renderer.getBufferIndex()], VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, finalSemaphoreValue[renderer.getBufferIndex()] + 3 } },
                .timelineSignalPairs = { { renderingSemaphore[renderer.getBufferIndex()], VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, finalSemaphoreValue[renderer.getBufferIndex()] + 4 } }
            };
            exampleRayTrace.rayTraceRender(rtRenderSync, rtMaterialDefinitionsBuffer);
        }
        else //raster
        {
            //render pass (wait on transfer, signal rendering semaphore)
            const PaperRenderer::SynchronizationInfo rasterSyncInfo = {
                .queueType = PaperRenderer::QueueType::GRAPHICS,
                .timelineWaitPairs = { { renderingSemaphore[renderer.getBufferIndex()], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, finalSemaphoreValue[renderer.getBufferIndex()] + 1 } },
                .timelineSignalPairs = { { renderingSemaphore[renderer.getBufferIndex()], VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, finalSemaphoreValue[renderer.getBufferIndex()] + 4 } }
            };
            exampleRaster.rasterRender(rasterSyncInfo);
        }

        //copy HDR buffer to swapchain (wait for render pass and swapchain, signal rendering and presentation semaphores)
        const PaperRenderer::SynchronizationInfo bufferCopySyncInfo = {
            .queueType = PaperRenderer::QueueType::GRAPHICS,
            .binaryWaitPairs = { { swapchainSemaphore, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT } },
            .timelineWaitPairs = { { renderingSemaphore[renderer.getBufferIndex()], VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, finalSemaphoreValue[renderer.getBufferIndex()] + 4 } },
            .timelineSignalPairs = { { renderingSemaphore[renderer.getBufferIndex()], VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, finalSemaphoreValue[renderer.getBufferIndex()] + 5 } }
        };
        bufferCopyPass.render(bufferCopySyncInfo, guiContext.raster);

        //render GUI
        const PaperRenderer::SynchronizationInfo guiSyncInfo = {
            .queueType = PaperRenderer::QueueType::GRAPHICS,
            .binarySignalPairs = { { presentationSemaphore[renderer.getBufferIndex()], VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT } },
            .timelineWaitPairs = { { renderingSemaphore[renderer.getBufferIndex()], VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, finalSemaphoreValue[renderer.getBufferIndex()] + 5 } },
            .timelineSignalPairs = { { renderingSemaphore[renderer.getBufferIndex()], VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, finalSemaphoreValue[renderer.getBufferIndex()] + 6 } }
        };
        renderImGui(&renderer, &lastFrameStatistics, &guiContext, guiSyncInfo); //TODO THIS IS A MASSIVE HOST SYNC VIOLATION WITH QUEUES SINCE GUI DOESNT TAKE OWNERSHIP OF ITS QUEUE

        //increment final semaphore value to wait on
        finalSemaphoreValue[renderer.getBufferIndex()] += 6;

        //end frame (increments frame counter and therefore buffer index)
        renderer.endFrame({ presentationSemaphore[renderer.getBufferIndex()] });
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

    //destroy light buffers and descriptors
    lightingData.lightingUBO.reset();
    lightingData.pointLightsBuffer.reset();
    vkDestroyDescriptorSetLayout(renderer.getDevice().getDevice(), lightingData.lightingDescriptorLayout, nullptr);

    //destroy some stuff
    for(uint32_t i = 0; i < 2; i++)
    {
        vkDestroySemaphore(renderer.getDevice().getDevice(), presentationSemaphore[i], nullptr);
        vkDestroySemaphore(renderer.getDevice().getDevice(), renderingSemaphore[i], nullptr);
    }
    vkDestroyImageView(renderer.getDevice().getDevice(), hdrBuffer.view, nullptr);
    vkDestroyImageView(renderer.getDevice().getDevice(), depthBuffer.view, nullptr);
    vkDestroySampler(renderer.getDevice().getDevice(), hdrBuffer.sampler, nullptr);
    
    return 0;
}