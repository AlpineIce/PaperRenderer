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

    struct ModelLODInfo
    {
        std::unordered_map<uint32_t, std::vector<MeshInfo>> lodData;
    };

    struct ModelCreateInfo
    {
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        VkVertexInputBindingDescription vertexDescription;
        uint32_t vertexPositionOffset;
        
        std::vector<ModelLODInfo> LODs;
    };

    //----------MODEL INFORMATION----------//

    struct LODMesh
    {
        uint32_t vboOffset;
        uint32_t vertexCount;
        uint32_t iboOffset;
        uint32_t indexCount;        
    };

    struct LOD //acts more like an individual model
    {
        std::vector<std::vector<LODMesh>> meshMaterialData; //material index, meshes in material slot
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

        //shader data
        struct ShaderModel
        {
            AABB bounds;
            uint32_t lodCount;
            uint32_t lodsOffset;
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

        std::unique_ptr<Buffer> createDeviceLocalBuffer(VkDeviceSize size, void* data, VkBufferUsageFlags2KHR usageFlags);

        friend class RenderEngine;
        friend class ModelInstance;

    public:
        Model(RenderEngine* renderer, const ModelCreateInfo& creationInfo);
        ~Model();

        void bindBuffers(const VkCommandBuffer& cmdBuffer) const;

        VkDeviceAddress getVBOAddress() const { return vbo->getBufferDeviceAddress(); }
        VkDeviceAddress getIBOAddress() const { return ibo->getBufferDeviceAddress(); }
        const std::vector<VkVertexInputAttributeDescription>& getVertexAttributes() const { return vertexAttributes; }
        const VkVertexInputBindingDescription& getVertexDescription() const { return vertexDescription; }
        const uint32_t& getVertexPositionOffset() const { return vertexPositionOffset; }
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
            glm::vec4 position;
            glm::vec4 scale; 
            glm::quat qRotation;
            uint32_t modelDataOffset;
        };

        ShaderModelInstance getShaderInstance() const;

        //per acceleration structure data
        struct AccelerationStructureInstance
        {
            uint64_t blasReference;
            uint32_t modelInstanceIndex;
        };
        
        //per render pass data
        struct RenderPassInstance
        {
            uint32_t modelInstanceIndex;
            uint32_t LODsMaterialDataOffset;
            bool isVisible;
            float padding;
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

        uint32_t rendererSelfIndex = UINT32_MAX;

        struct RenderPassData
        {
            std::vector<char> renderPassInstanceData;
            VkDeviceSize LODsMaterialDataOffset = UINT64_MAX;
            std::unordered_map<std::vector<LODMesh> const*, class CommonMeshGroup*> meshGroupReferences;
            uint32_t selfIndex;
        };
        std::unordered_map<class RenderPass const*, RenderPassData> renderPassSelfReferences;

        struct AccelerationStructureData
        {
            uint64_t blasAddress;
            uint32_t selfIndex;
        };
        std::unordered_map<class AccelerationStructure const*, AccelerationStructureData> accelerationStructureSelfReferences;

        ModelTransformation transform = {};

        class RenderEngine* rendererPtr;
        Model const* modelPtr = NULL;

        friend class RenderEngine;
        friend class RenderPass;
        friend class RasterPreprocessPipeline;
        friend class AccelerationStructure;
        friend class TLASInstanceBuildPipeline;
        friend class CommonMeshGroup;
        
    public:
        ModelInstance(RenderEngine* renderer, Model const* parentModel);
        ~ModelInstance();

        void setTransformation(const ModelTransformation& newTransformation);
        //void setVisibility(class RenderPass* renderPass, bool newVisibility); //renderPass can be NULL if setting the visibility for all is desired
        
        Model const* getParentModelPtr() const { return modelPtr; }
        const ModelTransformation& getTransformation() const { return transform; };
        //bool getVisibility(class RenderPass* renderPass) const;
    };
}