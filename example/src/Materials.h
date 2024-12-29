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
};

//----------RASTER MATERIALS----------//

//default material class inherits PaperRenderer::Material
class DefaultMaterial : public PaperRenderer::Material
{
private:
    const PaperRenderer::Buffer& lightBuffer;
    const PaperRenderer::Buffer& lightInfoUBO;

public:
    DefaultMaterial(PaperRenderer::RenderEngine& renderer, const PaperRenderer::RasterPipelineBuildInfo& pipelineInfo, const PaperRenderer::Buffer& lightBuffer, const PaperRenderer::Buffer& lightInfoUBO);
    virtual ~DefaultMaterial() override;

    //bind class can override base class
    virtual void bind(VkCommandBuffer cmdBuffer, const PaperRenderer::Camera& camera, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites>& descriptorWrites) override;
};

//default material instance class inherits PaperRenderer::MaterialInstance
class DefaultMaterialInstance : public PaperRenderer::MaterialInstance
{
private:
    //Parameters and corresponding UBO to be used for material instances. Setting uniforms in material instances saves on expensive pipeline binding
    MaterialParameters parameters;
    PaperRenderer::Buffer parametersUBO;

public:
    DefaultMaterialInstance(PaperRenderer::RenderEngine& renderer, const PaperRenderer::Material& baseMaterial, MaterialParameters parameters);
    virtual ~DefaultMaterialInstance() override;

    virtual void bind(VkCommandBuffer cmdBuffer, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites>& descriptorWrites) override;

    const MaterialParameters& getParameters() const { return parameters; }
    void setParameters(const MaterialParameters& newParameters) { this->parameters = newParameters; }
};

//example leaf material
class LeafMaterial : public DefaultMaterial
{
public:
    LeafMaterial(PaperRenderer::RenderEngine& renderer, const PaperRenderer::RasterPipelineBuildInfo& pipelineInfo, const PaperRenderer::Buffer& lightBuffer, const PaperRenderer::Buffer& lightInfoUBO);
    virtual ~LeafMaterial() override;

    virtual void bind(VkCommandBuffer cmdBuffer, const PaperRenderer::Camera& camera, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites>& descriptorWrites) override;
};

//example leaf material instance
class LeafMaterialInstance : public DefaultMaterialInstance
{
public:
    LeafMaterialInstance(PaperRenderer::RenderEngine& renderer, const LeafMaterial& baseMaterial, MaterialParameters parameters);
    virtual ~LeafMaterialInstance() override;

    virtual void bind(VkCommandBuffer cmdBuffer, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites>& descriptorWrites) override;
};

//----------RAY TRACE MATERIALS----------//

struct DefaultRTMaterialDefinition
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