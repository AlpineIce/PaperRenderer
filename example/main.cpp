#include "../src/PaperRenderer/PaperRenderer.h"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "lib/tinygltf/tiny_gltf.h"
#include <fstream>
#include <functional>

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
                            .cullMode = VK_CULL_MODE_NONE, //no depth testing
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
        VkClearValue colorClearValue = {};
        colorClearValue.color = {0.0f, 0.0f, 0.0f, 0.0f};

        VkClearValue depthClearValue = {};
        depthClearValue.color = {0.0f, 0.0f, 0.0, 0.0f};

        //attachments
        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        colorAttachments.push_back({    //output
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext = NULL,
            .imageView = renderer.getSwapchain().getCurrentImageView(),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = colorClearValue 
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
        VkRenderingInfoKHR renderInfo = {};
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
        .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
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

void rasterRender()
{
    //PaperRenderer::RenderPassInfo
}

void updateUniformBuffers(PaperRenderer::RenderEngine& renderer, PaperRenderer::Camera& camera, PaperRenderer::Buffer& rtUBO)
{
    //update RT UBO
    RayTraceInfo rtInfo = {
        .projection = camera.getProjection(),
        .view = camera.getViewMatrix(),
        .modelDataReference = renderer.getModelDataBuffer().getBufferDeviceAddress(),
        .frameNumber = renderer.getFramesRenderedCount()
    };

    PaperRenderer::BufferWrite rtInfoWrite = {
        .offset = 0,
        .size = sizeof(RayTraceInfo),
        .data = &rtInfo
    };

    rtUBO.writeToBuffer({ rtInfoWrite });
}

//----------MATERIALS----------//

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

    //----------HDR RENDERING BUFFER----------//

    //get HDR buffer
    HDRBuffer hdrBuffer = getHDRBuffer(renderer, VK_IMAGE_LAYOUT_GENERAL);

    //HDR buffer copy render pass
    BufferCopyPass bufferCopyPass(renderer, hdrBuffer);

    //----------MISC----------//

    //swapchain resize callback
    auto swapchainResizeFunction = [&](VkExtent2D newExtent) {
        //destroy old HDR buffer
        hdrBuffer.image.reset();
        vkDestroyImageView(renderer.getDevice().getDevice(), hdrBuffer.view, nullptr);
        vkDestroySampler(renderer.getDevice().getDevice(), hdrBuffer.sampler, nullptr);

        //create new HDR buffer
        hdrBuffer = getHDRBuffer(renderer, VK_IMAGE_LAYOUT_GENERAL);

        //update camera
        scene.camera->updateCameraProjection();
    };
    renderer.getSwapchain().setSwapchainRebuildCallback(swapchainResizeFunction);

    //----------RENDER LOOP----------//

    //synchronization
    uint64_t finalSemaphoreValue = 0;
    VkSemaphore renderingSemaphore = renderer.getDevice().getCommands().getTimelineSemaphore(finalSemaphoreValue);
    VkSemaphore presentationSemaphore = renderer.getDevice().getCommands().getSemaphore();

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
            .pValues = &finalSemaphoreValue
        };
        vkWaitSemaphores(renderer.getDevice().getDevice(), &beginWaitInfo, UINT64_MAX);

        //----------END OF SYNCHRONIZATOIN DEPENDENCIES FROM LAST FRAME----------//

        //update uniform  buffers
        updateUniformBuffers(renderer, *scene.camera, *rtInfoUBO);

        //begin frame
        const VkSemaphore& swapchainSemaphore = renderer.beginFrame();

        //remember to explicitly submit the staging buffer transfers (do entire submit in this case)
        const PaperRenderer::SynchronizationInfo transferSyncInfo = {
            .queueType = PaperRenderer::QueueType::TRANSFER,
        };
        renderer.getStagingBuffer().submitQueuedTransfers(transferSyncInfo);

        //build queued BLAS's (wait on transfer, signal rendering semaphore
        const PaperRenderer::SynchronizationInfo blasSyncInfo = {
            .queueType = PaperRenderer::QueueType::COMPUTE,
            .timelineWaitPairs = { { renderer.getStagingBuffer().getTransferSemaphore() } },
            .timelineSignalPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR, finalSemaphoreValue + 1 } }
        };
        renderer.getAsBuilder().submitQueuedOps(blasSyncInfo, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);
        
        //update tlas
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

        //copy HDR buffer to swapchain (wait for render pass and swapchain, signal rendering and presentation semaphores)
        PaperRenderer::SynchronizationInfo bufferCopySyncInfo = {
            .queueType = PaperRenderer::QueueType::GRAPHICS,
            .binaryWaitPairs = { { swapchainSemaphore, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT } },
            .binarySignalPairs = { { presentationSemaphore, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT } },
            .timelineWaitPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, finalSemaphoreValue + 3 } },
            .timelineSignalPairs = { { renderingSemaphore, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, finalSemaphoreValue + 4 } }
        };
        bufferCopyPass.render(bufferCopySyncInfo, *scene.camera, hdrBuffer, false);

        //end frame
        renderer.endFrame({ presentationSemaphore });

        //increment final semaphore value to wait on
        finalSemaphoreValue += 4;
    }
    
    return 0;
}