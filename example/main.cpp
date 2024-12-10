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

std::vector<std::unique_ptr<PaperRenderer::Model>> createModels(PaperRenderer::RenderEngine& renderer)
{
    //glTF path
    std::string gltfPath = "./resources/models/PaperRendererExample.glb";
    
    //load glTF
    tinygltf::Model gltfModel;
    std::string error;
    std::string warning;
    tinygltf::TinyGLTF gltfContext;

    gltfContext.LoadBinaryFromFile(&gltfModel, &error, &warning, gltfPath);

    //loaded models vector
    std::vector<std::unique_ptr<PaperRenderer::Model>> loadedModels;
    loadedModels.reserve(gltfModel.meshes.size());

    //iterate meshes
    for(const tinygltf::Mesh& mesh : gltfModel.meshes)
    {
        const std::string modelName = mesh.name;

        //iterate sub-meshes (no LODs in this example, but an LOD implementation shouldn't be too hard to understand, its just different "sub-models" in one model)
        for(const tinygltf::Primitive& primitive : mesh.primitives)
        {
            const uint32_t matIndex = primitive.material;
            const tinygltf::BufferView& indices = gltfModel.bufferViews[primitive.indices];

            //fill in a vector with index data
            
            

            //fill in a vector with vertex data

            int a = 0;
        }

        PaperRenderer::ModelCreateInfo modelInfo;
        modelInfo.vertexAttributes = 
        modelInfo.vertexDescription = 
        modelInfo.vertexPositionOffset = 
        modelInfo.LODs = 
        modelInfo.createBLAS = true;
        loadedModels.push_back(std::make_unique<PaperRenderer::Model>(modelInfo));
    }
    

    return loadedModels;
}

//point light definition
struct PointLight
{
    glm::vec3 position;
    glm::vec3 color;
};

std::unique_ptr<PaperRenderer::Buffer> createPointLightsBuffer(PaperRenderer::RenderEngine& renderer)
{
    std::vector<PointLight> pointLightsData = {
        { glm::vec3(10.0f, 10.0, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f) },
        { glm::vec3(10.0f, -10.0, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f) },
        { glm::vec3(-10.0f, 10.0, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f) },
        { glm::vec3(-10.0f, -10.0, 0.0f), glm::vec3(0.0f, 1.0f, 1.0f) }
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
    glm::vec4 camPos;
    uint32_t pointLightCount;
};

std::unique_ptr<PaperRenderer::Buffer> createLightInfoUniformBuffer(PaperRenderer::RenderEngine& renderer)
{
    LightInfo uniformBufferData = {
        .ambientLight = glm::vec4(0.05f, 0.05f, 0.05f, 1.0f),
        .camPos = glm::vec4(0.0f, 0.0f, 1.5f, 0.0f),
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

//vertex definition
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

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

public:
    DefaultMaterialInstance(PaperRenderer::RenderEngine& renderer, const PaperRenderer::Material& baseMaterial)
        :PaperRenderer::MaterialInstance(renderer, baseMaterial)
    {

    }
    ~DefaultMaterialInstance() override
    {

    }

    void bind(VkCommandBuffer cmdBuffer, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites>& descriptorWrites) override
    {
        //additional non-default descriptor writes can be inserted into descriptorWrites here

        MaterialInstance::bind(cmdBuffer, descriptorWrites); //parent class function must be called
    }
};

int main()
{
    //initialize renderer
    const PaperRenderer::RendererCreationStruct rendererInfo = {
        .rasterPreprocessSpirv = readFile("resources/shaders/IndirectDrawBuild.spv"),
        .rtPreprocessSpirv = readFile("resources/shaders/TLASInstBuild.spv"),
        .windowState = {
            .windowName = "Example"
        }
    };
    PaperRenderer::RenderEngine renderer(rendererInfo);

    //----------UNIFORM AND STORAGE BUFFERS----------//

    //point lights buffer
    std::unique_ptr<PaperRenderer::Buffer> pointLightsBuffer = createPointLightsBuffer(renderer);

    std::unique_ptr<PaperRenderer::Buffer> lightingUniformBuffer = createLightInfoUniformBuffer(renderer);

    //----------RASTER MATERIALS----------//

    //material info
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
        }
    };
    //base material from custom class
    //DefaultMaterial material(renderer, materialInfo);
    //default material instance from custom base material
    //DefaultMaterialInstance defaultMaterialInstance(renderer, material);

    //----------RASTER RENDER PASS----------//

    //raster render pass
    //PaperRenderer::RenderPass renderPass(renderer, defaultMaterialInstance);

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

    //load models TODO
    std::vector<std::unique_ptr<PaperRenderer::Model>> models = createModels(renderer);

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