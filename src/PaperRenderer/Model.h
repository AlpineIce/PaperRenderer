#pragma once
#include "Device.h"
#include "VulkanResources.h"

#include <unordered_map>
#include <list>

namespace PaperRenderer
{
    //----------MODEL CREATION INFO----------//

    struct MaterialMeshInfo
    {
        std::vector<char> verticesData; //DEEP COPY WARNING! USE STD::MOVE why is it like this? because theres a high risk of accidential dangling pointers
        std::vector<uint32_t> indices; //DEEP COPY WARNING! USE STD::MOVE why is it like this? because theres a high risk of accidential dangling pointers
        bool opaque = true; //set to false if geometry will invoke any hit shaders in ray tracing
    };

    struct ModelLODInfo
    {
        std::map<uint32_t, MaterialMeshInfo> lodData; //groups of meshes with a shared common material... ordered because I learned this the hard way
    };

    struct ModelCreateInfo
    {
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        VkVertexInputBindingDescription vertexDescription;
        const std::vector<ModelLODInfo>& LODs;
        bool createBLAS = true; //create a default BLAS
        const std::string& modelName;
    };

    //----------MODEL INFORMATION----------//

    struct LODMesh
    {
        uint32_t vboOffset;
        uint32_t vertexCount;
        uint32_t iboOffset;
        uint32_t indexCount;
    };

    struct MaterialMesh
    {
        LODMesh mesh;
        bool invokeAnyHit;
    };

    struct LOD //acts more like an individual model
    {
        std::vector<MaterialMesh> materialMeshes; //material index, mesh in material slot
    };

    struct AABB
    {
        float posX = 0.0f;
        float negX = 0.0f;
        float posY = 0.0f;
        float negY = 0.0f;
        float posZ = 0.0f;
        float negZ = 0.0f;
    };

    struct ModelTransformation
    {
        glm::vec3 position = glm::vec3(0.0f); //world position
        glm::vec3 scale = glm::vec3(1.0f); //local scale
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); //local rotation
    };

    //----------MODEL DECLARATION----------//

    class Model //acts more like a collection of models (LODs)
    {
    private:
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        VkVertexInputBindingDescription vertexDescription;
        const std::string modelName;

        std::vector<LOD> LODs;
        std::unique_ptr<Buffer> vbo;
        std::unique_ptr<Buffer> ibo;
        AABB aabb;

        //BLAS info
        std::unique_ptr<class BLAS> defaultBLAS;

        //shader data
        struct ShaderModel
        {
            AABB bounds;
            uint64_t vertexAddress;
            uint64_t indexAddress;
            uint32_t lodCount;
            uint32_t lodsOffset;
            uint32_t vertexStride;
        };

        struct ShaderModelLOD
        {
            uint32_t materialCount;
            uint32_t meshGroupsOffset;
        };

        struct ShaderModelLODMeshGroup
        {
            uint32_t iboOffset; //for ray tracing
            uint32_t vboOffset; //for ray tracing
        };

        uint64_t selfIndex;
        VkDeviceSize shaderDataLocation = UINT64_MAX;
        std::vector<char> shaderData;

        void setShaderData();

        class RenderEngine& renderer;

        std::unique_ptr<Buffer> createDeviceLocalBuffer(VkDeviceSize size, void* data, VkBufferUsageFlags2KHR usageFlags) const;

        friend class RenderEngine;
        friend class ModelInstance;

    public:
        Model(RenderEngine& renderer, const ModelCreateInfo& creationInfo);
        ~Model();
        Model(const Model&) = delete;

        void bindBuffers(const VkCommandBuffer& cmdBuffer) const;

        const std::vector<VkVertexInputAttributeDescription>& getVertexAttributes() const { return vertexAttributes; }
        const VkVertexInputBindingDescription& getVertexDescription() const { return vertexDescription; }
        VkDeviceAddress getVBOAddress() const { return vbo->getBufferDeviceAddress(); }
        VkDeviceAddress getIBOAddress() const { return ibo->getBufferDeviceAddress(); }
        BLAS const* getBlasPtr() const { return defaultBLAS ? defaultBLAS.get() : NULL; }
        const AABB& getAABB() const { return aabb; }
        const std::vector<LOD>& getLODs() const { return LODs; }
        const std::vector<char>& getShaderData() const { return shaderData; }
        const VkDeviceSize& getShaderDataLocation() const { return shaderDataLocation; }
        const std::string& getModelName() const { return modelName; }
    };

    //----------MODEL INSTANCE DECLARATIONS----------//

    class ModelInstance
    {
    private:
        //per instance data
        struct ShaderModelInstance
        {
            glm::vec3 position;
            glm::vec3 scale; 
            glm::quat qRotation;
            uint32_t modelDataOffset;
        };

        struct AccelerationStructureInstance
        {
            uint64_t blasReference;
            uint32_t selfIndex;
            uint32_t customIndex;
            uint32_t modelInstanceIndex;
            uint32_t mask = 0xAA000000; //8 byte limit
            uint32_t recordOffset = 0; //24 bit limit
            VkGeometryInstanceFlagsKHR flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        };

        ShaderModelInstance getShaderInstance() const;
        
        //per render pass data
        struct RenderPassInstance
        {
            uint32_t modelInstanceIndex;
            uint32_t LODsMaterialDataOffset;
            bool isVisible;
        };

        struct LODMaterialData
        {
            uint32_t meshGroupsOffset;
        };

        struct MaterialMeshGroup
        {
            uint64_t drawCommandAddress = 0;
            uint64_t matricesBufferAddress = 0;
        };

        void setRenderPassInstanceData(class RenderPass const* renderPass);
        const std::vector<char>& getRenderPassInstanceData(class RenderPass const* renderPass) const { return renderPassSelfReferences.at(renderPass).renderPassInstanceData; };

        //renderer self index
        uint32_t rendererSelfIndex = UINT32_MAX;

        //RenderPass reference data
        struct RenderPassData
        {
            std::vector<char> renderPassInstanceData;
            VkDeviceSize LODsMaterialDataOffset = UINT64_MAX;
            std::unordered_map<LODMesh const*, class CommonMeshGroup*> meshGroupReferences;
            uint32_t selfIndex;
            bool sorted = false;
        };
        std::unordered_map<class RenderPass const*, RenderPassData> renderPassSelfReferences;

        //RayTraceRender reference data
        struct RayTraceRenderPassData
        {
            class RTMaterial const* material = NULL;
            uint32_t selfIndex;
        };
        std::unordered_map<class RayTraceRender const*, RayTraceRenderPassData> rtRenderSelfReferences;

        //unique instance acceleration structure and VBO (only used if uniqueGeometry is set to true on instance creation)
        struct UniqueGeometryData
        {
            bool isUsed = false;
            std::unique_ptr<Buffer> uniqueVBO;
            std::unique_ptr<class BLAS> blas;
        } uniqueGeometryData;
        
        ModelTransformation transform = {};

        class RenderEngine& renderer;
        const Model& parentModel;

        friend class RenderEngine;
        friend class RenderPass;
        friend class RayTraceRender;
        friend class RasterPreprocessPipeline;
        friend class TLAS;
        friend class TLASInstanceBuildPipeline;
        friend class AccelerationStructureBuilder;
        friend class CommonMeshGroup;
        
    public:
        //uniqueGeometry should only be set to true if the instance is animate
        ModelInstance(RenderEngine& renderer, const Model& parentModel, bool uniqueGeometry);
        ~ModelInstance();
        ModelInstance(const ModelInstance&) = delete;

        void setTransformation(const ModelTransformation& newTransformation);
        
        const Model& getParentModel() const { return parentModel; }
        Buffer const* getUniqueVBO() const { return uniqueGeometryData.isUsed ? uniqueGeometryData.uniqueVBO.get() : NULL; }
        class BLAS* getUniqueBLAS() { return uniqueGeometryData.blas ? uniqueGeometryData.blas.get() : NULL; } //Returns unique BLAS if created, else NULL
        const ModelTransformation& getTransformation() const { return transform; };
    };
}