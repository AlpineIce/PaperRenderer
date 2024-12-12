#include "../src/PaperRenderer/PaperRenderer.h"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "lib/tinygltf/tiny_gltf.h"
#include <fstream>

std::vector<uint32_t> readFile(const std::string& location)
{
    std::ifstream file(location, std::ios::binary);
    std::vector<uint32_t> buffer;

    if(file.is_open())
    {
        file.seekg (0, file.end);
        uint32_t length = file.tellg();
        file.seekg (0, file.beg);

        buffer.resize(length);
        file.read((char*)buffer.data(), length);

        file.close();

        return buffer;
    }
    else
    {
        throw std::runtime_error("Couldn't open file " + location);
    }
}

//vertex definition
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct MaterialParameters
{
    glm::vec4 baseColor;
    glm::vec4 emission;
    float roughness;
    float metallic;
};

struct SceneData
{
    std::unordered_map<std::string, std::unique_ptr<PaperRenderer::Model>> models;
    std::unordered_map<std::string, MaterialParameters> materialInstancesData;
    std::unique_ptr<PaperRenderer::Camera> camera;
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
        }
        else if(node.camera != -1 && !returnData.camera) //camera
        {
            const tinygltf::Camera& camera = gltfModel.cameras[node.camera];

            const PaperRenderer::CameraTranslation cameraTranslationInfo = {
                .pitch = 0.0f,
                .yaw = 0.0f,
                .roll = 0.0f,
                .position = glm::vec3(node.translation[0], node.translation[1], node.translation[2]),
                .qRotation = glm::quat(node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]),
                .useQuaternion = true //IMPORTANT TO SET IF USING qRotation FOR ROTATION AND NOT PITCH, YAW, ROLL
            };

            const PaperRenderer::CameraCreateInfo cameraInfo = {
                .fov = (float)camera.perspective.yfov,
                .clipNear = (float)camera.perspective.znear,
                .clipFar = (float)camera.perspective.zfar,
                .initTranslation = cameraTranslationInfo
            };

            returnData.camera = std::make_unique<PaperRenderer::Camera>(renderer, cameraInfo);
        }
    }

    //load material instances
    for(const tinygltf::Material& material : gltfModel.materials)
    {
        const tinygltf::ColorValue baseColor = material.values.at("baseColorFactor").ColorFactor();
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
    glm::vec4 position;
    glm::vec4 color;
};

std::unique_ptr<PaperRenderer::Buffer> createPointLightsBuffer(PaperRenderer::RenderEngine& renderer)
{
    std::vector<PointLight> pointLightsData = {
        { glm::vec4(10.0f, 10.0, 0.0f, 5.0f), glm::vec4(1.0f, 0.0f, 0.0f, 5.0f) },
        { glm::vec4(10.0f, -10.0, 0.0f, 5.0f), glm::vec4(1.0f, 1.0f, 0.0f, 5.0f) },
        { glm::vec4(-10.0f, 10.0, 0.0f, 5.0f), glm::vec4(0.0f, 1.0f, 0.0f, 5.0f) },
        { glm::vec4(-10.0f, -10.0, 0.0f, 5.0f), glm::vec4(0.0f, 1.0f, 1.0f, 5.0f) }
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
        .usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR,
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

//default material class inherits PaperRenderer::Material
class DefaultMaterial : public PaperRenderer::Material
{
private:

public:
    DefaultMaterial(PaperRenderer::RenderEngine& renderer, const PaperRenderer::RasterPipelineBuildInfo& pipelineInfo)
        :PaperRenderer::Material(renderer, pipelineInfo)
    {

    }
    ~DefaultMaterial() override
    {

    }

    //bind class can override base class
    void bind(VkCommandBuffer cmdBuffer, const PaperRenderer::Camera& camera, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites>& descriptorWrites) override
    {
        //additional non-default descriptor writes can be inserted into descriptorWrites here

        Material::bind(cmdBuffer, camera, descriptorWrites); //parent class function must be called
    }
};

//default material instance class inherits PaperRenderer::MaterialInstance
class DefaultMaterialInstance : public PaperRenderer::MaterialInstance
{
private:
    //Parameters and corresponding UBO to be used for material instances. Setting uniforms in material instances saves on expensive pipeline binding
    const MaterialParameters parameters;
    std::unique_ptr<PaperRenderer::Buffer> parametersUBO;

public:
    DefaultMaterialInstance(PaperRenderer::RenderEngine& renderer, const PaperRenderer::Material& baseMaterial, MaterialParameters parameters)
        :PaperRenderer::MaterialInstance(renderer, baseMaterial),
        parameters(parameters)
    {
        //create UBO
        PaperRenderer::BufferInfo uboInfo = {
            .size = sizeof(MaterialParameters),
            .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
            .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
        };

        parametersUBO = std::make_unique<PaperRenderer::Buffer>(renderer, uboInfo);

        //fill UBO data (this can change every frame too, which should be done in the bind() function, but for this example, that is unnecessary)
        PaperRenderer::BufferWrite uboWrite = {
            .offset = 0,
            .size=  sizeof(MaterialParameters),
            .data = &parameters
        };

        parametersUBO->writeToBuffer({ uboWrite });
    }

    ~DefaultMaterialInstance() override
    {
    }

    void bind(VkCommandBuffer cmdBuffer, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites>& descriptorWrites) override
    {
        //additional non-default descriptor writes can be inserted into descriptorWrites here

        //set 2, binding 0 (example material parameters)
        descriptorWrites[2].bufferWrites.push_back({
            .infos = {{
                .buffer = parametersUBO->getBuffer(),
                .offset = 0,
                .range = VK_WHOLE_SIZE
            }},
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .binding = 0
        });
        //remember to call the parent class function!
        MaterialInstance::bind(cmdBuffer, descriptorWrites);
    }
};

int main()
{
    //initialize renderer
    const PaperRenderer::RendererCreationStruct rendererInfo = {
        .rasterPreprocessSpirv = readFile("resources/shaders/IndirectDrawBuild.spv"),
        .rtPreprocessSpirv = readFile("resources/shaders/TLASInstBuild.spv"),
        .windowState = {
            .windowName = "Paper Renderer Example"
        }
    };
    PaperRenderer::RenderEngine renderer(rendererInfo);

    //----------GLTF SCENE LOADING----------//

    //load glTF scene
    SceneData scene = loadSceneData(renderer);

    //----------UNIFORM AND STORAGE BUFFERS----------//

    //point lights buffer
    std::unique_ptr<PaperRenderer::Buffer> pointLightsBuffer = createPointLightsBuffer(renderer);

    std::unique_ptr<PaperRenderer::Buffer> lightingUniformBuffer = createLightInfoUniformBuffer(renderer);

    //----------MATERIALS----------//

    //base raster material
    const PaperRenderer::RasterPipelineBuildInfo materialInfo = {
        .shaderInfo = {
            {
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .data = readFile("resources/shaders/Default_vert.spv")
            },
            {
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .data = readFile("resources/shaders/Default_frag.spv")
            }
        },
        .descriptorSets = {
            { 0, {
                {
                    .binding = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
                },
                {
                    .binding = 2,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
                }
            }}
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
            }
        },
        .drawDescriptorIndex = 1
    };

    DefaultMaterial baseMaterial(renderer, materialInfo);

    //base RT material
    PaperRenderer::ShaderHitGroup baseMaterialHitGroup = {
        .chitShaderData = readFile("resources/shaders/raytrace_chit.spv"),
        .ahitShaderData = {}, //TODO FOR TRANSPARENCY
        .intShaderData = {}
    };

    PaperRenderer::RTMaterial baseRTMaterial(renderer, baseMaterialHitGroup);

    //create material instances that "derive" from base material and the loaded scene data parameters
    std::unordered_map<std::string, std::unique_ptr<PaperRenderer::MaterialInstance>> materialInstances;
    materialInstances.reserve(scene.materialInstancesData.size());
    for(const auto& [name, parameters] : scene.materialInstancesData)
    {
        materialInstances[name] = std::make_unique<DefaultMaterialInstance>(renderer, baseMaterial, parameters);

        //TODO RT MATERIAL "INSTANCE" IMPLEMENTATION

    }
    
    //----------RASTER RENDER PASS----------//

    //default material instance
    DefaultMaterialInstance defaultMaterialInstance(renderer, baseMaterial, {
        .baseColor = glm::vec4(1.0f, 0.5f, 1.0f, 1.0f),
        .emission = glm::vec4(0.0f),
        .roughness = 0.5f,
        .metallic = 0.0f
    });

    //raster render pass
    PaperRenderer::RenderPass renderPass(renderer, defaultMaterialInstance);

    //----------RAY TRACING RENDER PASS----------//

    //tlas
    PaperRenderer::TLAS tlas(renderer);

    //general shaders
    const PaperRenderer::Shader rgenShader(renderer, readFile("resources/shaders/raytrace_rgen.spv"));
    const PaperRenderer::Shader rmissShader(renderer, readFile("resources/shaders/raytrace_rmiss.spv"));
    const PaperRenderer::Shader rshadowShader(renderer, readFile("resources/shaders/raytraceShadow_rmiss.spv"));
    const std::vector<PaperRenderer::ShaderDescription> generalShaders = {
        { VK_SHADER_STAGE_RAYGEN_BIT_KHR, &rgenShader },
        { VK_SHADER_STAGE_MISS_BIT_KHR, &rmissShader },
        { VK_SHADER_STAGE_MISS_BIT_KHR, &rshadowShader }
    };

    //descriptors
    const std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>> rtDescriptors = {
        { 0, {
            {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
            },
            {
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
            },
            { //hdr buffer
                .binding = 3,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
            },
            { //light info
                .binding = 4,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
            },
            { //point lights
                .binding = 5,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
            }
        }}
    };
    
    PaperRenderer::RayTraceRender rtRenderPass(renderer, tlas, generalShaders, rtDescriptors, {});

    //synchronization
    uint64_t renderingSemaphoreValue = 0;
    VkSemaphore renderingSemaphore = renderer.getDevice().getCommands().getTimelineSemaphore(renderingSemaphoreValue);

    //rendering loop
    while(!glfwWindowShouldClose(renderer.getSwapchain().getGLFWwindow()))
    {
        //wait for last frame
        VkSemaphoreWaitInfo beginWaitInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .pNext = NULL,
            .flags = 0,
            .semaphoreCount = 1,
            .pSemaphores = &renderingSemaphore,
            .pValues = &renderingSemaphoreValue
        };
        vkWaitSemaphores(renderer.getDevice().getDevice(), &beginWaitInfo, UINT64_MAX);

        //begin frame sync info
        PaperRenderer::SynchronizationInfo transferSyncInfo = {};
        transferSyncInfo.queueType = PaperRenderer::QueueType::TRANSFER;
        transferSyncInfo.timelineSignalPairs = {};

        PaperRenderer::SynchronizationInfo asSyncInfo = {};
        asSyncInfo.queueType = PaperRenderer::QueueType::TRANSFER;
        asSyncInfo.timelineSignalPairs = {};

        //begin frame
        const VkSemaphore& swapchainSemaphore = renderer.beginFrame(transferSyncInfo, asSyncInfo);

        //copy HDR buffer to swapchain

        //end frame
        renderer.endFrame({ swapchainSemaphore });

        //MORE TODO
    }
    
    return 0;
}