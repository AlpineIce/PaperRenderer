#pragma once
#include "Device.h"
#include "VulkanResources.h"

#include <filesystem>
#include <unordered_map>
#include <list>

namespace PaperRenderer
{
    //----------MODEL CREATION INFO----------//

    struct MeshInfo
    {
        ///where the vertexDescription stride represents the total size of a vertex, the position offset represents the offset in bytes of a vec3 (4 byte floats, 12 bytes total)
        ///of where the position data is. position is required since its used in creating an AABB for culling purposes
        std::vector<char> verticesData;
        std::vector<uint32_t> indices;
        uint32_t materialIndex;
    };

    struct MeshGroupInfo
    {
        std::vector<MeshInfo> meshInfo;
        bool opaque = true; //set to false if geometry will invoke any hit shaders in ray tracing
    };

    struct ModelLODInfo
    {
        std::unordered_map<uint32_t, MeshGroupInfo> lodData; //groups of meshes with a shared common material
    };

    struct ModelCreateInfo
    {
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        VkVertexInputBindingDescription vertexDescription;
        uint32_t vertexPositionOffset;
        std::vector<ModelLODInfo> LODs;
        bool createBLAS = true; //create a default BLAS
    };

    //----------MODEL INFORMATION----------//

    struct LODMesh
    {
        uint32_t vboOffset;
        uint32_t vertexCount;
        uint32_t iboOffset;
        uint32_t indexCount;
    };

    struct LODMeshGroup
    {
        std::vector<LODMesh> meshes;
        bool invokeAnyHit;
    };

    struct LOD //acts more like an individual model
    {
        std::vector<LODMeshGroup> meshMaterialData; //material index, meshes in material slot
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
        uint32_t vertexPositionOffset;

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
            uint32_t meshCount;
            uint32_t meshesOffset; //old variable, but still here for 16 byte alignment
            uint32_t iboOffset; //for ray tracing
            uint32_t vboOffset; //for ray tracing
        };

        uint64_t selfIndex;
        VkDeviceSize shaderDataLocation = UINT64_MAX;
        std::vector<char> shaderData;

        void setShaderData();

        class RenderEngine* rendererPtr;

        std::unique_ptr<Buffer> createDeviceLocalBuffer(VkDeviceSize size, void* data, VkBufferUsageFlags2KHR usageFlags) const;

        friend class RenderEngine;
        friend class ModelInstance;

    public:
        Model(RenderEngine* renderer, const ModelCreateInfo& creationInfo);
        ~Model();
        Model(const Model&) = delete;

        void bindBuffers(const VkCommandBuffer& cmdBuffer) const;

        const std::vector<VkVertexInputAttributeDescription>& getVertexAttributes() const { return vertexAttributes; }
        const VkVertexInputBindingDescription& getVertexDescription() const { return vertexDescription; }
        const uint32_t& getVertexPositionOffset() const { return vertexPositionOffset; }
        VkDeviceAddress getVBOAddress() const { return vbo->getBufferDeviceAddress(); }
        VkDeviceAddress getIBOAddress() const { return ibo->getBufferDeviceAddress(); }
        BLAS const* getBlasPtr() const { return defaultBLAS ? defaultBLAS.get() : NULL; }
        const AABB& getAABB() const { return aabb; }
        const std::vector<LOD>& getLODs() const { return LODs; }
        const std::vector<char>& getShaderData() const { return shaderData; }
        const VkDeviceSize& getShaderDataLocation() const { return shaderDataLocation; }
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
            uint32_t indirectDrawDatasOffset;
        };

        struct IndirectDrawData
        {
            uint32_t instanceCountIndex = UINT32_MAX;
            uint32_t matricesStartIndex = UINT32_MAX;
        };

        void setRenderPassInstanceData(class RenderPass const* renderPass);
        const std::vector<char>& getRenderPassInstanceData(class RenderPass const* renderPass) const { return renderPassSelfReferences.at(renderPass).renderPassInstanceData; };

        //renderer self index
        uint32_t rendererSelfIndex = UINT32_MAX;

        //render pass reference data
        struct RenderPassData
        {
            std::vector<char> renderPassInstanceData;
            VkDeviceSize LODsMaterialDataOffset = UINT64_MAX;
            std::unordered_map<std::vector<LODMesh> const*, class CommonMeshGroup*> meshGroupReferences;
            uint32_t selfIndex;
        };
        std::unordered_map<class RenderPass const*, RenderPassData> renderPassSelfReferences;

        //acceleration structure reference data
        std::unordered_map<class TLAS const*, uint32_t> accelerationStructureSelfReferences;

        //unique instance acceleration structure and VBO (only used if uniqueGeometry is set to true on instance creation)
        struct UniqueGeometryData
        {
            bool isUsed = false;
            std::unique_ptr<Buffer> uniqueVBO;
            std::unique_ptr<class BLAS> blas;
        } uniqueGeometryData;
        
        ModelTransformation transform = {};

        class RenderEngine* rendererPtr;
        Model const* modelPtr = NULL;

        friend class RenderEngine;
        friend class RenderPass;
        friend class RasterPreprocessPipeline;
        friend class TLAS;
        friend class TLASInstanceBuildPipeline;
        friend class AccelerationStructureBuilder;
        friend class CommonMeshGroup;
        
    public:
        //uniqueGeometry should only be set to true if the instance is animate
        ModelInstance(RenderEngine* renderer, Model const* parentModel, bool uniqueGeometry);
        ~ModelInstance();
        ModelInstance(const ModelInstance&) = delete;

        void setTransformation(const ModelTransformation& newTransformation);
        //void setVisibility(class RenderPass* renderPass, bool newVisibility); //renderPass can be NULL if setting the visibility for all is desired
        
        Model const* getParentModelPtr() const { return modelPtr; }
        Buffer const* getUniqueVBO() const { return uniqueGeometryData.isUsed ? uniqueGeometryData.uniqueVBO.get() : NULL; }
        class BLAS const* getBLAS() const; //Returns unique BLAS if created, else model BLAS. Returns null if either
        const ModelTransformation& getTransformation() const { return transform; };
        //bool getVisibility(class RenderPass* renderPass) const;
    };
}