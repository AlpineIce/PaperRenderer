#pragma once
#include "Device.h"
#include "VulkanResources.h"

#include <unordered_map>
#include <list>

namespace PaperRenderer
{
    //----------MODEL CREATION INFO----------//

    struct AABB
    {
        float posX = 0.0f;
        float negX = 0.0f;
        float posY = 0.0f;
        float negY = 0.0f;
        float posZ = 0.0f;
        float negZ = 0.0f;

        bool operator==(const AABB& other) const
        {
            bool equal = true;
            equal = equal && posX == other.posX;
            equal = equal && negX == other.negX;
            equal = equal && posY == other.posY;
            equal = equal && negY == other.negY;
            equal = equal && posZ == other.posZ;
            equal = equal && negZ == other.negZ;

            return equal;
        }
    };

    struct MaterialMeshInfo
    {
        uint32_t vertexStride = 0;
        std::vector<char> verticesData = {};
        VkIndexType indexType = VK_INDEX_TYPE_UINT32;
        std::vector<char> indicesData = {};
        bool opaque = true; //set to false if geometry will invoke any hit shaders in ray tracing
    };

    struct ModelLODInfo
    {
        std::map<uint32_t, MaterialMeshInfo> lodData; //groups of meshes with a shared common material... ordered because I learned this the hard way
    };

    struct ModelCreateInfo
    {
        std::vector<ModelLODInfo> LODs = {};
        bool createBLAS = true; //create a default BLAS
        VkBuildAccelerationStructureFlagsKHR blasFlags = 0;
        std::string modelName = "Untitled";
        AABB bounds = {};
    };

    //----------MODEL INFORMATION----------//

    struct LODMesh
    {
        uint32_t vertexStride = 0;
        uint32_t indexStride = 0;
        uint32_t vboOffset = 0;
        uint32_t verticesSize = 0;
        uint32_t iboOffset = 0;
        uint32_t indicesSize = 0;
        uint32_t invokeAnyHit = false;
        VkIndexType indexType = VK_INDEX_TYPE_NONE_KHR;
    };

    struct LOD //acts more like an individual model
    {
        std::vector<LODMesh> materialMeshes; //material index, mesh in material slot
    };

    struct ModelTransformation
    {
        glm::vec3 position = glm::vec3(0.0f); //world position
        glm::vec3 scale = glm::vec3(1.0f); //local scale
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); //local rotation
    };

    //----------MODEL DECLARATION----------//

    class ModelGeometryData
    {
    private:
        // Generic geometry data
        AABB aabb = {};
        Buffer vbo;
        VkBuildAccelerationStructureFlagsKHR blasFlags;
        std::unique_ptr<class BLAS> blas = NULL;

        // Buffer and reference data
        std::vector<uint8_t> shaderData = {};
        struct ShaderDataReference
        {
            uint32_t selfIndex = UINT32_MAX;
            VkDeviceSize shaderDataLocation = UINT64_MAX;
        } shaderDataReference = {};

        std::vector<uint8_t> createShaderData(const VkDeviceAddress iboAddress, const VkDeviceAddress vboAddress, const AABB& bounds, const std::vector<LOD>& LODs) const;

        class Model* parentModel;
        RenderEngine* renderer;

        friend class RenderEngine;

    public:
        ModelGeometryData(RenderEngine& renderer, const AABB& aabb, const std::vector<uint8_t>& vertices, Model& parentModel, const bool createBLAS, const VkBuildAccelerationStructureFlagsKHR blasFlags);
        ModelGeometryData(RenderEngine& renderer, const ModelGeometryData& geometryData, const bool createBLAS);
        ~ModelGeometryData();
        ModelGeometryData(const ModelGeometryData&) = delete;
        ModelGeometryData(ModelGeometryData&& other) noexcept;
        ModelGeometryData& operator=(ModelGeometryData&& other) noexcept;
        
        void updateShaderData(const VkDeviceAddress iboAddress, const VkDeviceAddress vboAddress, const AABB& bounds, const std::vector<LOD>& LODs);

        const Buffer& getVBO() const { return vbo; }
        BLAS* getBlasPtr() { return blas ? blas.get() : NULL; }
        BLAS const* getBlasPtr() const { return blas ? blas.get() : NULL; }
        const AABB& getAABB() const { return aabb; }
        const std::vector<uint8_t>& getShaderData() const { return shaderData; }
        const ShaderDataReference& getShaderDataReference() const { return shaderDataReference; }
        const Model& getParentModel() const { return *parentModel;}
    };

    class Model //Immutable collection of LODs with unique Material-Mesh groups
    {
    private:
        const std::string modelName;

        const std::vector<LOD> LODs;
        const Buffer ibo;
        ModelGeometryData geometry;

        class RenderEngine& renderer;

        friend ModelGeometryData;

    public:
        Model(RenderEngine& renderer, const ModelCreateInfo& creationInfo);
        ~Model();
        Model(const Model&) = delete;
        Model(const Model&& other) noexcept;
        Model& operator=(Model&& other) noexcept;

        const Buffer& getIBO() const { return ibo; }
        const ModelGeometryData& getGeometryData() const { return geometry; }
        const std::vector<LOD>& getLODs() const { return LODs; }
        const std::string& getModelName() const { return modelName; }
    };

    //----------MODEL INSTANCE DECLARATIONS----------//

    class ModelInstance //Mutable instance of a parent model which always shares its parent's index buffer, but may contain a unique vertex buffer if specified
    {
    private:
        //per instance data
        struct ShaderModelInstance
        {
            glm::vec3 position;
            glm::vec3 scale; 
            glm::quat qRotation;
            uint32_t selfModelDataOffset = 0xFFFFFFFF;
            uint32_t parentModelDataOffset = 0xFFFFFFFF;
        };

        struct AccelerationStructureInstance
        {
            uint64_t blasReference;
            uint32_t modelInstanceIndex;
            uint32_t customIndex:24;
            uint32_t mask:8 = (uint8_t)0xFF;
            uint32_t recordOffset:24 = 0;
            VkGeometryInstanceFlagsKHR flags:8 = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            uint32_t padding = 0;
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
        const std::vector<uint8_t>& getRenderPassInstanceData(class RenderPass const* renderPass) const { return renderPassSelfReferences.at(renderPass).renderPassInstanceData; };

        //renderer self index
        uint32_t rendererSelfIndex = UINT32_MAX;

        //RenderPass reference data
        struct RenderPassData
        {
            std::vector<uint8_t> renderPassInstanceData;
            VkDeviceSize LODsMaterialDataOffset = UINT64_MAX;
            std::unordered_map<LODMesh const*, class CommonMeshGroup*> meshGroupReferences;
            uint32_t selfIndex;
            bool sorted = false;
        };
        std::unordered_map<class RenderPass const*, RenderPassData> renderPassSelfReferences;

        // RT reference data
        struct RTRenderData
        {
            class ShaderHitGroup const* material = NULL;
            uint32_t selfIndex = 0;
        };
        std::unordered_map<class RayTraceRender const*, std::unordered_map<class TLAS*, RTRenderData>> rtRenderSelfReferences;

        //unique instance acceleration structure and VBO (only used if uniqueGeometry is set to true on instance creation)
        std::unique_ptr<ModelGeometryData> uniqueGeometryData = NULL;
        
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
        ModelInstance(RenderEngine& renderer, const Model& parentModel, bool uniqueGeometry, const VkBuildAccelerationStructureFlagsKHR flags=0);
        ~ModelInstance();
        ModelInstance(const ModelInstance&) = delete;
        ModelInstance(const ModelInstance&& other) noexcept;
        ModelInstance& operator=(ModelInstance&& other) noexcept;

        void setTransformation(const ModelTransformation& newTransformation);
        void queueBLAS(const VkBuildAccelerationStructureFlagsKHR flags) const; //call this to queue an update of it's acceleration structure for the next AS builder call
        
        const Model& getParentModel() const { return parentModel; }
        const ModelGeometryData& getGeometryData() const { return uniqueGeometryData ? *uniqueGeometryData : parentModel.getGeometryData(); }
        const ModelTransformation& getTransformation() const { return transform; };
    };
}