#include "../src/PaperRenderer/PaperRenderer.h"
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
    PaperRenderer::RendererCreationStruct engineInfo = {
        .rasterPreprocessSpirv = readFile("resources/shaders/IndirectDrawBuild.spv"),
        .rtPreprocessSpirv = readFile("resources/shaders/TLASInstBuild.spv"),
        .windowState = {
            .windowName = "Example"
        }
    };
    PaperRenderer::RenderEngine renderer(engineInfo);

    //material info
    PaperRenderer::RasterPipelineBuildInfo materialInfo = {
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

            }},
            { 1, {
                
            }},
            { 2, {
                
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
        } //default properties are fine
    };
    //base material from custom class
    DefaultMaterial material(renderer, materialInfo);
    //material instance from custom base material
    DefaultMaterialInstance materialInstance(renderer, material);

    //load models
    
    return 0;
}