#pragma once
#include "Common.h"

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
    float padding[6];
};

//helper struct
struct LightingData
{
    //note that you can use a constructor initializer list to initialize all this without "bad" memory layout from unique ptr
    std::unique_ptr<PaperRenderer::Buffer> lightingUBO;
    std::unique_ptr<PaperRenderer::Buffer> pointLightsBuffer;
    PaperRenderer::DescriptorSetLayout lightingDescriptorLayout;
    PaperRenderer::ResourceDescriptor lightingDescriptor;
};

//----------RASTER MATERIALS----------//

//default material class inherits PaperRenderer::Material
class DefaultMaterial
{
private:
    //lighting buffer references
    const LightingData& lightingData;

    //material
    PaperRenderer::Material material;

    //binding function
    void bind(VkCommandBuffer cmdBuffer, const PaperRenderer::Camera& camera) const;

    PaperRenderer::RenderEngine& renderer;

public:
    DefaultMaterial(PaperRenderer::RenderEngine& renderer, const PaperRenderer::RasterPipelineInfo& pipelineInfo, const LightingData& lightingData);
    ~DefaultMaterial();

    PaperRenderer::Material& getMaterial() { return material; }
};

//default material instance class inherits PaperRenderer::MaterialInstance
class DefaultMaterialInstance
{
private:
    //Parameters and corresponding UBO to be used for material instances. Setting uniforms in material instances saves on expensive pipeline binding
    MaterialParameters parameters;
    PaperRenderer::Buffer parametersUBO;
    PaperRenderer::ResourceDescriptor uboDescriptor;

    //binding function
    void bind(VkCommandBuffer cmdBuffer) const;

    //material instance
    PaperRenderer::MaterialInstance materialInstance;

    PaperRenderer::RenderEngine& renderer;

public:
    DefaultMaterialInstance(PaperRenderer::RenderEngine& renderer, DefaultMaterial& baseMaterial, MaterialParameters parameters, VkDescriptorSetLayout uboDescriptorLayout);
    ~DefaultMaterialInstance();
    
    void updateUBO();
    void setParameters(const MaterialParameters& newParameters) { this->parameters = newParameters; }

    const MaterialParameters& getParameters() const { return parameters; }
    PaperRenderer::MaterialInstance& getMaterialInstance() { return materialInstance; }
};

//----------RAY TRACE MATERIALS----------//

struct DefaultShaderHitGroupDefinition
{
    //surface
    glm::vec3 albedo; //normalized vec3
    glm::vec3 emissive; //non-normalized
    float metallic; //normalized
    float roughness; //normalized

    //transmission
    glm::vec3 transmission;
    float ior;
};