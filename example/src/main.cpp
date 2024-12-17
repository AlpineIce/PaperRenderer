#include "../src/PaperRenderer/PaperRenderer.h"
#include "GuiRender.h"

//tinygltf
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "lib/tinygltf/tiny_gltf.h"

#include <fstream>
#include <functional>
#include <future>
#include <iostream> //for logging callback

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

//material parameters for this example (no texturing)
struct MaterialParameters
{
    glm::vec4 baseColor;
    glm::vec4 emission;
    float roughness;
    float metallic;
};

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

            const PaperRenderer::CameraTranslation cameraTranslationInfo = {
                .pitch = 0.0f,
                .yaw = 0.0f,
                .roll = 0.0f,
                .position = glm::vec3(node.translation[0], node.translation[1], node.translation[2]),
                .qRotation = glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]),
                .useQuaternion = true //IMPORTANT TO SET IF USING qRotation FOR ROTATION AND NOT PITCH, YAW, ROLL
            };

            const PaperRenderer::CameraCreateInfo cameraInfo = {
                .fov = (float)camera.perspective.yfov * 100.0f,
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
    glm::vec4 position;
    glm::vec4 color;
};

std::unique_ptr<PaperRenderer::Buffer> createPointLightsBuffer(PaperRenderer::RenderEngine& renderer)
{
    std::vector<PointLight> pointLightsData = {
        { glm::vec4(10.0f, 10.0, 5.0f, 1.0f),   glm::vec4(0.6f, 1.0f, 0.8f, 1.0f) },
        { glm::vec4(10.0f, -10.0, 5.0f, 1.0f),  glm::vec4(1.0f, 0.8f, 0.8f, 1.0f) },
        { glm::vec4(-10.0f, 10.0, 5.0f, 1.0f),  glm::vec4(0.6f, 0.8f, 1.0f, 1.0f) },
        { glm::vec4(-10.0f, -10.0, 5.0f, 1.0f), glm::vec4(0.8f, 1.0f, 0.6f, 1.0f) }
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

//----------HDR BUFFER----------//

//HDR buffer creation
struct HDRBuffer
{
    std::unique_ptr<PaperRenderer::Image> image = NULL;
    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
};

HDRBuffer getHDRBuffer(PaperRenderer::RenderEngine& renderer, VkImageLayout startingLayout)
{
    //HDR buffer format
    const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;

    //get new extent
    const VkExtent2D extent = renderer.getSwapchain().getExtent();

    //HDR buffer for rendering
    const PaperRenderer::ImageInfo hdrBufferInfo = {
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { extent.width, extent.height, 1 },
        .maxMipLevels = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, //no MSAA used
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .imageAspect = VK_IMAGE_ASPECT_COLOR_BIT,
        .desiredLayout = startingLayout
    };

    std::unique_ptr<PaperRenderer::Image> hdrBuffer = std::make_unique<PaperRenderer::Image>(renderer, hdrBufferInfo);

    //HDR buffer view
    VkImageView view = hdrBuffer->getNewImageView(VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, format);

    //HDR buffer sampler (I've profiled this before and its much more efficient to use a render pass than a computer shader and blit)
    VkSampler sampler = hdrBuffer->getNewSampler();

    return { std::move(hdrBuffer), format, view, sampler };
}

//----------DEPTH BUFFER----------//

struct DepthBuffer
{
    std::unique_ptr<PaperRenderer::Image> image = NULL;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageView view = VK_NULL_HANDLE;
};

DepthBuffer getDepthBuffer(PaperRenderer::RenderEngine& renderer)
{
    //depth buffer format
    VkFormat depthBufferFormat = VK_FORMAT_UNDEFINED;
    VkFormatProperties properties;

    //find format (prefer higher bits)
    vkGetPhysicalDeviceFormatProperties(renderer.getDevice().getGPU(), VK_FORMAT_D32_SFLOAT, &properties);
    if(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
        depthBufferFormat = VK_FORMAT_D32_SFLOAT;
    }
    else
    {
        vkGetPhysicalDeviceFormatProperties(renderer.getDevice().getGPU(), VK_FORMAT_D24_UNORM_S8_UINT, &properties);
        if(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            depthBufferFormat = VK_FORMAT_D24_UNORM_S8_UINT;
        }
        else 
        {
            vkGetPhysicalDeviceFormatProperties(renderer.getDevice().getGPU(), VK_FORMAT_D16_UNORM_S8_UINT, &properties);
            if(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            {
                depthBufferFormat = VK_FORMAT_D16_UNORM_S8_UINT;
            }
        }
    }

    //get new extent
    const VkExtent2D extent = renderer.getSwapchain().getExtent();

    //depth buffer for rendering
    const PaperRenderer::ImageInfo depthBufferInfo = {
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depthBufferFormat,
        .extent = { extent.width, extent.height, 1 },
        .maxMipLevels = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, //no MSAA used
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .imageAspect = VK_IMAGE_ASPECT_DEPTH_BIT,
        .desiredLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
    };

    std::unique_ptr<PaperRenderer::Image> depthBuffer = std::make_unique<PaperRenderer::Image>(renderer, depthBufferInfo);

    //depth buffer view
    VkImageView view = depthBuffer->getNewImageView(VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D, depthBufferFormat);

    return { std::move(depthBuffer), depthBufferFormat, view };
}

//----------HDR BUFFER -> SWAPCHAIN COPY PASS----------//

//Buffer copy render pass
class BufferCopyPass
{
private:
    //buffer copy material
    class BufferCopyMaterial : public PaperRenderer::Material
    {
    private:
        //UBO
        struct UBOInputData
        {
            glm::vec4 colorFilter;
            float exposure;
            float WBtemp;
            float WBtint;
            float contrast;
            float brightness;
            float saturation;
            float gammaCorrection;
        };
        std::unique_ptr<PaperRenderer::Buffer> uniformBuffer;
        
        //HDR buffer
        const HDRBuffer& hdrBuffer;

    public:
        BufferCopyMaterial(PaperRenderer::RenderEngine& renderer, const HDRBuffer& hdrBuffer)
            :PaperRenderer::Material(
                renderer, 
                {
                    .shaderInfo = {
                        {
                            .stage = VK_SHADER_STAGE_VERTEX_BIT,
                            .data = readFile("resources/shaders/Quad.spv")
                        },
                        {
                            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                            .data = readFile("resources/shaders/BufferCopy.spv")
                        }
                    },
                    .descriptorSets = {
                        { 0, {
                            { //UBO
                                .binding = 0,
                                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .descriptorCount = 1,
                                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
                            },
                            { //HDR buffer
                                .binding = 1,
                                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .descriptorCount = 1,
                                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
                            }
                        }}
                    },
                    .pcRanges = {},
                    .properties = {
                        .vertexAttributes = {}, //no vertex data
                        .vertexDescriptions = {}, //no vertex data
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
                            { renderer.getSwapchain().getFormat() }
                        },
                        .rasterInfo = {
                            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                            .pNext = NULL,
                            .flags = 0,
                            .depthClampEnable = VK_FALSE,
                            .rasterizerDiscardEnable = VK_FALSE,
                            .polygonMode = VK_POLYGON_MODE_FILL,
                            .cullMode = VK_CULL_MODE_NONE,
                            .frontFace = VK_FRONT_FACE_CLOCKWISE,
                            .depthBiasEnable = VK_FALSE,
                            .depthBiasConstantFactor = 0.0f,
                            .depthBiasClamp = 0.0f,
                            .depthBiasSlopeFactor = 0.0f,
                            .lineWidth = 1.0f
                        }
                    }
                },
                false
            ),
            hdrBuffer(hdrBuffer)
        {
            //uniform buffer
            PaperRenderer::BufferInfo preprocessBufferInfo = {
                .size = sizeof(UBOInputData),
                .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
                .allocationFlags=  VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
            };

            uniformBuffer = std::make_unique<PaperRenderer::Buffer>(renderer, preprocessBufferInfo);
        }
        
        ~BufferCopyMaterial() override
        {
        }

        void bind(VkCommandBuffer cmdBuffer, const PaperRenderer::Camera& camera, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites>& descriptorWrites) override
        {
            //----------UBO----------//

            UBOInputData uboInputData = {};
            uboInputData.exposure = 2.0f;
            uboInputData.WBtemp = 0.0f;
            uboInputData.WBtint = 0.0f;
            uboInputData.contrast = 1.0f;
            uboInputData.colorFilter = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            uboInputData.brightness = 0.0f;
            uboInputData.saturation = 1.0f;
            uboInputData.gammaCorrection = renderer.getSwapchain().getIsUsingHDR() ? 1.0f : 2.2f;

            PaperRenderer::BufferWrite uboWrite;
            uboWrite.data = &uboInputData;
            uboWrite.offset = 0;
            uboWrite.size = sizeof(UBOInputData);
            uniformBuffer->writeToBuffer({ uboWrite });

            //uniform buffer
            VkDescriptorBufferInfo uniformInfo = {};
            uniformInfo.buffer = uniformBuffer->getBuffer();
            uniformInfo.offset = 0;
            uniformInfo.range = sizeof(UBOInputData);

            PaperRenderer::BuffersDescriptorWrites uniformWrite;
            uniformWrite.binding = 0;
            uniformWrite.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uniformWrite.infos = { uniformInfo };

            //hdr buffer
            VkDescriptorImageInfo hdrBufferInfo = {};
            hdrBufferInfo.imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            hdrBufferInfo.imageView = hdrBuffer.view;
            hdrBufferInfo.sampler = hdrBuffer.sampler;

            PaperRenderer::ImagesDescriptorWrites hdrBufferWrite;
            hdrBufferWrite.binding = 1;
            hdrBufferWrite.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            hdrBufferWrite.infos = { hdrBufferInfo };

            //descriptor writes
            descriptorWrites[0].bufferWrites.push_back(uniformWrite);
            descriptorWrites[0].imageWrites.push_back(hdrBufferWrite);

            //next level bind
            PaperRenderer::Material::bind(cmdBuffer, camera, descriptorWrites);
        }
    } material;

    //renderer reference
    PaperRenderer::RenderEngine& renderer;

public:
    BufferCopyPass(PaperRenderer::RenderEngine& renderer, const HDRBuffer& hdrBuffer)
        :material(renderer, hdrBuffer),
        renderer(renderer)
    {
    }

    ~BufferCopyPass()
    {
    }

    //to render function
    void render(const PaperRenderer::SynchronizationInfo &syncInfo, const PaperRenderer::Camera& camera, const HDRBuffer &hdrBuffer, bool fromRaster)
    {
        //----------PRE-RENDER BARRIER----------//

        //swapchain transition from undefined to color attachment
        std::vector<VkImageMemoryBarrier2> preRenderImageBarriers;
        preRenderImageBarriers.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = renderer.getSwapchain().getCurrentImage(),
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        });
        preRenderImageBarriers.push_back({
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                .oldLayout = fromRaster ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL,
                .newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = hdrBuffer.image->getImage(),
                .subresourceRange =  {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                }
            });

        VkDependencyInfo preRenderBarriers = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = NULL,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = NULL,
            .imageMemoryBarrierCount = (uint32_t)preRenderImageBarriers.size(),
            .pImageMemoryBarriers = preRenderImageBarriers.data()
        };

        //----------POST-RENDER BARRIER----------//

        //swapchain transition from color attachment to presentation optimal
        std::vector<VkImageMemoryBarrier2> postRenderImageBarriers;
        postRenderImageBarriers.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = renderer.getSwapchain().getCurrentImage(),
            .subresourceRange =  {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        });

        VkDependencyInfo postRenderBarriers = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = NULL,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = NULL,
            .imageMemoryBarrierCount = (uint32_t)postRenderImageBarriers.size(),
            .pImageMemoryBarriers = postRenderImageBarriers.data()
        };

        //----------ATTACHMENTS----------//
        
        //color attachment
        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        colorAttachments.push_back({    //output
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext = NULL,
            .imageView = renderer.getSwapchain().getCurrentImageView(),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { 0.0f, 0.0f, 0.0f, 0.0f } 
        });
    

        //----------VIEWPORT SCISSORS AND RENDER AREA----------//

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)(renderer.getSwapchain().getExtent().width);
        viewport.height = (float)(renderer.getSwapchain().getExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissors = {};
        scissors.extent = renderer.getSwapchain().getExtent();
        scissors.offset = { 0, 0 };

        VkRect2D renderArea = {};
        renderArea.offset = {0, 0};
        renderArea.extent = renderer.getSwapchain().getExtent();

        //----------RENDER----------//

        VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(PaperRenderer::QueueType::GRAPHICS);

        VkCommandBufferBeginInfo cmdBufferBeginInfo = {};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufferBeginInfo.pNext = NULL;
        cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        cmdBufferBeginInfo.pInheritanceInfo = NULL;
        
        vkBeginCommandBuffer(cmdBuffer, &cmdBufferBeginInfo);

        vkCmdPipelineBarrier2(cmdBuffer, &preRenderBarriers);

        //rendering
        VkRenderingInfo renderInfo = {};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
        renderInfo.pNext = NULL;
        renderInfo.flags = 0;
        renderInfo.renderArea = renderArea;
        renderInfo.layerCount = 1;
        renderInfo.viewMask = 0;
        renderInfo.colorAttachmentCount = colorAttachments.size();
        renderInfo.pColorAttachments = colorAttachments.data();
        renderInfo.pDepthAttachment = NULL;
        renderInfo.pStencilAttachment = NULL;

        vkCmdBeginRendering(cmdBuffer, &renderInfo);

        //scissors (plural) and viewports
        vkCmdSetViewportWithCount(cmdBuffer, 1, &viewport);
        vkCmdSetScissorWithCount(cmdBuffer, 1, &scissors);

        //MSAA samples
        vkCmdSetRasterizationSamplesEXT(cmdBuffer, VK_SAMPLE_COUNT_1_BIT);

        //compare op
        vkCmdSetDepthCompareOp(cmdBuffer, VK_COMPARE_OP_NEVER);

        //bind (camera isn't actually used in this implementation, but thats fine)
        std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> descriptorWrites;
        material.bind(cmdBuffer, camera, descriptorWrites);

        //draw command
        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

        //end rendering
        vkCmdEndRendering(cmdBuffer);

        vkCmdPipelineBarrier2(cmdBuffer, &postRenderBarriers);

        vkEndCommandBuffer(cmdBuffer);

        renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

        renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer });
    }
};

//----------RENDER PASSES----------//

//rt UBO
struct RayTraceInfo
{
    glm::mat4 projection;
    glm::mat4 view;
    uint64_t modelDataReference;
    uint64_t frameNumber;
};

std::unique_ptr<PaperRenderer::Buffer> createRTInfoUBO(PaperRenderer::RenderEngine& renderer, const PaperRenderer::Camera& camera)
{
    RayTraceInfo uniformBufferData = {
        .projection = camera.getProjection(),
        .view = camera.getViewMatrix(),
        .modelDataReference = renderer.getModelDataBuffer().getBufferDeviceAddress(),
        .frameNumber = 0
    };

    PaperRenderer::BufferInfo uniformBufferInfo = {
        .size = sizeof(RayTraceInfo),
        .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
        .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
    };
    std::unique_ptr<PaperRenderer::Buffer> uniformBuffer(std::make_unique<PaperRenderer::Buffer>(renderer, uniformBufferInfo));

    PaperRenderer::BufferWrite uboDataWrite = {
        .offset = 0,
        .size = sizeof(RayTraceInfo),
        .data = &uniformBufferData
    };
    uniformBuffer->writeToBuffer({ uboDataWrite });

    return uniformBuffer;
}

void rayTraceRender(
    PaperRenderer::RenderEngine& renderer, 
    PaperRenderer::RayTraceRender& rtRenderPass,
    const PaperRenderer::Buffer& pointLightsBuffer,
    const PaperRenderer::Buffer& lightInfoBuffer,
    const PaperRenderer::Buffer& rtInfoUBO,
    const PaperRenderer::Camera& camera,
    const HDRBuffer& hdrBuffer,
    const PaperRenderer::SynchronizationInfo& syncInfo
)
{
    //pre-render barriers
    std::vector<VkImageMemoryBarrier2> preRenderImageBarriers;
    preRenderImageBarriers.push_back({ //HDR buffer undefined -> general layout; required for correct shader access
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = NULL,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
        .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = hdrBuffer.image->getImage(),
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    });
    
    const VkDependencyInfo preRenderDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = NULL,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = NULL,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = NULL,
        .imageMemoryBarrierCount = (uint32_t)preRenderImageBarriers.size(),
        .pImageMemoryBarriers = preRenderImageBarriers.data()
    };

    //descriptor writes
    const PaperRenderer::DescriptorWrites descriptorWrites = {
        .bufferWrites = {
            {
                .infos = {{
                    .buffer = pointLightsBuffer.getBuffer(),
                    .offset = 0,
                    .range = VK_WHOLE_SIZE
                }},
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .binding = 1
            },
            {
                .infos = {{
                    .buffer = lightInfoBuffer.getBuffer(),
                    .offset = 0,
                    .range = VK_WHOLE_SIZE
                }},
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .binding = 2
            },
            {
                .infos = {{
                    .buffer = rtInfoUBO.getBuffer(),
                    .offset = 0,
                    .range = VK_WHOLE_SIZE
                }},
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .binding = 4
            },
            { //TODO THIS CAN CHANGE AND SHOULD BE FIXED
                .infos = {{
                    .buffer = rtRenderPass.getTLAS().getInstanceDescriptionsRange() ? rtRenderPass.getTLAS().getInstancesBuffer().getBuffer() : VK_NULL_HANDLE,
                    .offset = rtRenderPass.getTLAS().getInstanceDescriptionsOffset(),
                    .range = rtRenderPass.getTLAS().getInstanceDescriptionsRange()
                }},
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .binding = 5
            },

        },
        .imageWrites = {
            {
                .infos = {{
                    .sampler = hdrBuffer.sampler,
                    .imageView = hdrBuffer.view,
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL
                }},
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .binding = 3
            }
        },
        .bufferViewWrites = {},
        .accelerationStructureWrites = {
            {
                .accelerationStructures = { &rtRenderPass.getTLAS() },
                .binding = 0
            },
        }
    };

    //rt render info
    const PaperRenderer::RayTraceRenderInfo rtRenderInfo = {
        .image = *hdrBuffer.image,
        .camera = camera,
        .preRenderBarriers = &preRenderDependency,
        .postRenderBarriers = NULL, //no post render barrier
        .rtDescriptorWrites = descriptorWrites
    };
    
    rtRenderPass.render(rtRenderInfo, syncInfo);
}

void rasterRender(
    PaperRenderer::RenderEngine& renderer, 
    PaperRenderer::RenderPass& renderPass,
    const PaperRenderer::Buffer& pointLightsBuffer,
    const PaperRenderer::Buffer& lightInfoBuffer,
    const PaperRenderer::Camera& camera,
    const HDRBuffer& hdrBuffer,
    const DepthBuffer& depthBuffer,
    const PaperRenderer::SynchronizationInfo& syncInfo
)
{
    //pre-render barriers
    std::vector<VkImageMemoryBarrier2> preRenderImageBarriers;
    preRenderImageBarriers.push_back({ //HDR buffer undefined -> general layout; required for correct shader access
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = NULL,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = hdrBuffer.image->getImage(),
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    });
    
    const VkDependencyInfo preRenderDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = NULL,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = NULL,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = NULL,
        .imageMemoryBarrierCount = (uint32_t)preRenderImageBarriers.size(),
        .pImageMemoryBarriers = preRenderImageBarriers.data()
    };

    //color attachment
    std::vector<VkRenderingAttachmentInfo> colorAttachments;
    colorAttachments.push_back({
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = NULL,
        .imageView = hdrBuffer.view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { 0.1f, 0.1f, 0.1f, 1.0f }
    });

    //depth attachment
    const VkRenderingAttachmentInfo depthAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = NULL,
        .imageView = depthBuffer.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { 1.0f, 0 }
    };

    //viewport, scissors, render area
    const VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)(renderer.getSwapchain().getExtent().width),
        .height = (float)(renderer.getSwapchain().getExtent().height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };

    const VkRect2D scissors = {
        .offset = { 0, 0 },
        .extent = renderer.getSwapchain().getExtent()
    };

    const VkRect2D renderArea = {
        .offset = { 0, 0 },
        .extent = renderer.getSwapchain().getExtent()
    };

    //render pass info
    const PaperRenderer::RenderPassInfo renderPassInfo = {
        .camera = camera,
        .colorAttachments = colorAttachments,
        .depthAttachment = &depthAttachment,
        .stencilAttachment = NULL,
        .viewports = { viewport },
        .scissors = { scissors },
        .renderArea = renderArea,
        .sampleCount = VK_SAMPLE_COUNT_1_BIT,
        .preRenderBarriers = &preRenderDependency,
        .postRenderBarriers = NULL,
        .depthCompareOp = VK_COMPARE_OP_LESS
    };

    //render
    renderer.getDevice().getCommands().submitToQueue(syncInfo, renderPass.render(renderPassInfo));
}

//----------MATERIALS----------//

//default material class inherits PaperRenderer::Material
class DefaultMaterial : public PaperRenderer::Material
{
private:
    const PaperRenderer::Buffer& lightBuffer;
    const PaperRenderer::Buffer& lightInfoUBO;

public:
    DefaultMaterial(PaperRenderer::RenderEngine& renderer, const PaperRenderer::RasterPipelineBuildInfo& pipelineInfo, const PaperRenderer::Buffer& lightBuffer, const PaperRenderer::Buffer& lightInfoUBO)
        :PaperRenderer::Material(renderer, pipelineInfo),
        lightBuffer(lightBuffer),
        lightInfoUBO(lightInfoUBO)
    {
    }

    ~DefaultMaterial() override
    {
    }

    //bind class can override base class
    void bind(VkCommandBuffer cmdBuffer, const PaperRenderer::Camera& camera, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites>& descriptorWrites) override
    {
        //additional non-default descriptor writes can be inserted into descriptorWrites here
        descriptorWrites[0].bufferWrites.push_back({
            .infos = {{
                .buffer = lightBuffer.getBuffer(),
                .offset = 0,
                .range = VK_WHOLE_SIZE
            }},
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .binding = 1
        });
        descriptorWrites[0].bufferWrites.push_back({
            .infos = {{
                .buffer = lightInfoUBO.getBuffer(),
                .offset = 0,
                .range = VK_WHOLE_SIZE
            }},
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .binding = 2
        });

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


//----------UBOs----------//

void updateUniformBuffers(PaperRenderer::RenderEngine& renderer, PaperRenderer::Camera& camera, PaperRenderer::Buffer& rtUBO)
{
    //update RT UBO
    RayTraceInfo rtInfo = {
        .projection = camera.getProjection(),
        .view = camera.getViewMatrix(),
        .modelDataReference = renderer.getModelDataBuffer().getBufferDeviceAddress(),
        .frameNumber = renderer.getFramesRenderedCount()
    };

    //std::vector<char> rtInfoData(sizeof(RayTraceInfo));
    //memcpy(rtInfoData.data(), &rtInfo, sizeof(RayTraceInfo));
    
    PaperRenderer::BufferWrite write = {
        .offset = 0,
        .size = sizeof(RayTraceInfo),
        .data = &rtInfo
    };
    rtUBO.writeToBuffer({ write });

    //renderer.getStagingBuffer().queueDataTransfers(rtUBO, 0, rtInfoData);

    //update camera
    PaperRenderer::CameraTranslation newTranslation = camera.getTranslation();
    newTranslation.position = glm::vec3(15.0f * sin(glfwGetTime()), 15.0f * cos(glfwGetTime()), 5.0f);
    newTranslation.qRotation = glm::lookAt(newTranslation.position, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    camera.updateCameraView(newTranslation);
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
            //only print info if in debug
#ifndef NDEBUG
        case PaperRenderer::LogType::INFO:
            std::cout << beginString << "\033[1;37m--INFO--: \033[0m";
            break;
#endif
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
        scene.camera->updateCameraProjection();
    };

    //initialize renderer
    const PaperRenderer::RendererCreationStruct rendererInfo = {
        .logEventCallbackFunction = logCallbackFunction,
        .swapchainRebuildCallbackFunction = swapchainResizeFunction,
        .rasterPreprocessSpirv = readFile("../resources/shaders/IndirectDrawBuild.spv"),
        .rtPreprocessSpirv = readFile("../resources/shaders/TLASInstBuild.spv"),
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

    //----------HDR & DEPTH RENDERING BUFFER----------//

    //get HDR buffer
    hdrBuffer = getHDRBuffer(renderer, VK_IMAGE_LAYOUT_GENERAL);

    //HDR buffer copy render pass
    BufferCopyPass bufferCopyPass(renderer, hdrBuffer);

    //get depth buffer
    depthBuffer = getDepthBuffer(renderer);

    //----------UNIFORM AND STORAGE BUFFERS----------//

    //lgihting buffers
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
            }},
            { 2, {
                {
                    .binding = 0,
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
                .cullMode = VK_CULL_MODE_BACK_BIT,
                .frontFace = VK_FRONT_FACE_CLOCKWISE,
                .depthBiasEnable = VK_FALSE,
                .depthBiasConstantFactor = 0.0f,
                .depthBiasClamp = 0.0f,
                .depthBiasSlopeFactor = 0.0f,
                .lineWidth = 1.0f
            }
        },
        .drawDescriptorIndex = 1
    };

    DefaultMaterial baseMaterial(renderer, materialInfo, *pointLightsBuffer, *lightingUniformBuffer);

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
            { //AS pointer
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
            },
            { //point lights (pbr.glsl)
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
            },
            { //light info (pbr.glsl)
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
            },
            { //hdr buffer
                .binding = 3,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
            },
            { //RT input data
                .binding = 4,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR
            },
            { //instance descriptions
                .binding = 5,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR
            }
        }}
    };

    //ubo
    std::unique_ptr<PaperRenderer::Buffer> rtInfoUBO = createRTInfoUBO(renderer, *scene.camera);
    
    //render pass
    PaperRenderer::RayTraceRender rtRenderPass(renderer, tlas, generalShaders, rtDescriptors, {});

    //----------ADD MODEL INSTANCES TO RT AND RASTER PASSES----------//

    for(std::unique_ptr<PaperRenderer::ModelInstance>& instance: modelInstances)
    {
        //raster render pass
        std::unordered_map<uint32_t, PaperRenderer::MaterialInstance*> materials;
        uint32_t matIndex = 0;
        for(const std::string& matName : scene.instanceMaterials[instance->getParentModel().getModelName()])
        {
            materials[matIndex] = materialInstances[matName].get();
            matIndex++;
        }
        renderPass.addInstance(*instance, { materials });

        //rt render pass (just use the base RT material for simplicity)
        rtRenderPass.addInstance(*instance, baseRTMaterial);

        //add to TLAS
        tlas.addInstance(*instance);
    }

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

    bool raster = true;
    while(!glfwWindowShouldClose(renderer.getSwapchain().getGLFWwindow()))
    {
        //async wait for last frame and last staging buffer transfer (on this frame, which was incremented on presentation)
        //std::future waitSemaphoreFuture(std::async(std::launch::async, waitSemaphoreFunction));

        //block this thread and while waiting for the begin function, no more work to do BIG OL TODO WE ASYNC-ING
        VkSemaphore swapchainSemaphore = waitSemaphoreFunction();//waitSemaphoreFuture.get();
        
        //update uniform buffers
        updateUniformBuffers(renderer, *scene.camera, *rtInfoUBO);

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
            rtRenderPass.updateTLAS(VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, tlasSyncInfo);

            //render pass (wait for TLAS build, signal rendering semaphore)
            PaperRenderer::SynchronizationInfo rtRenderSync = {
                .queueType = PaperRenderer::QueueType::COMPUTE,
                .timelineWaitPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, finalSemaphoreValue + 2 } },
                .timelineSignalPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, finalSemaphoreValue + 3 } }
            };
            rayTraceRender(renderer, rtRenderPass, *pointLightsBuffer, *lightingUniformBuffer, *rtInfoUBO, *scene.camera, hdrBuffer, rtRenderSync);
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
            rasterRender(renderer, renderPass, *pointLightsBuffer, *lightingUniformBuffer, *scene.camera, hdrBuffer, depthBuffer, rasterSyncInfo);
        }

        //copy HDR buffer to swapchain (wait for render pass and swapchain, signal rendering and presentation semaphores)
        const PaperRenderer::SynchronizationInfo bufferCopySyncInfo = {
            .queueType = PaperRenderer::QueueType::GRAPHICS,
            .binaryWaitPairs = { { swapchainSemaphore, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT } },
            .timelineWaitPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, finalSemaphoreValue + 3 } },
            .timelineSignalPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, finalSemaphoreValue + 4 } }
        };
        bufferCopyPass.render(bufferCopySyncInfo, *scene.camera, hdrBuffer, raster);

        //render GUI
        const PaperRenderer::SynchronizationInfo guiSyncInfo = {
            .queueType = PaperRenderer::QueueType::GRAPHICS,
            .binarySignalPairs = { { presentationSemaphore, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT } },
            .timelineWaitPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, finalSemaphoreValue + 4 } },
            .timelineSignalPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, finalSemaphoreValue + 5 } }
        };
        renderImGui(&renderer, guiContext, guiSyncInfo); //TODO THIS IS A MASSIVE HOST SYNC VIOLATION WITH QUEUES SINCE GUI DOESNT TAKE OWNERSHIP OF ITS QUEUE

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

    //destroy rt uniform buffer
    rtInfoUBO.reset();

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