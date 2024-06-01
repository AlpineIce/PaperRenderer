#pragma once
#include "Material.h"
#include "RHI/IndirectDraw.h"

#include <filesystem>
#include <unordered_map>
#include <list>

namespace PaperRenderer
{
    //----------MODEL CREATION INFO----------//

    struct MeshInfo
    {
        std::vector<PaperMemory::Vertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t materialIndex;
    };

    struct ModelCreateInfo
    {
        std::vector<std::unordered_map<uint32_t, std::vector<MeshInfo>>> LODs; //material index/slot, material associated meshes
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

    struct ModelTransform
    {
        glm::vec3 position = glm::vec3(0.0f); //world position
        glm::vec3 scale = glm::vec3(1.0f); //local scale
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); //local rotation
    };

    //----------MODEL DECLARATION----------//

    class Model //acts more like a collection of models (LODs)
    {
    private:
        std::vector<LOD> LODs;
        std::unique_ptr<PaperMemory::Buffer> vbo;
        std::unique_ptr<PaperMemory::Buffer> ibo;
        AABB aabb;

        class RenderEngine* rendererPtr;
        PaperMemory::DeviceAllocation* allocationPtr;

        std::unique_ptr<PaperMemory::Buffer> createDeviceLocalBuffer(VkDeviceSize size, void* data, VkBufferUsageFlags2KHR usageFlags);

    public:
        Model(RenderEngine* renderer, PaperMemory::DeviceAllocation* allocation, const ModelCreateInfo& creationInfo);
        ~Model();

        static VkDeviceSize getMemoryAlignment(Device* device);

        void bindBuffers(const VkCommandBuffer& cmdBuffer) const;

        VkDeviceAddress getVBOAddress() const { return vbo->getBufferDeviceAddress(); }
        VkDeviceAddress getIBOAddress() const { return ibo->getBufferDeviceAddress(); }
        const AABB& getAABB() const { return aabb; }
        const std::vector<LOD>& getLODs() const { return LODs; }
    };

    //----------MODEL INSTANCE DECLARATIONS----------//

    class ModelInstance
    {
    private:
        struct ShaderInputObject
        {
            //transformation
            glm::vec4 position;
            glm::vec4 scale; 
            glm::quat qRotation; //quat -> mat4... could possibly be a mat3
            AABB bounds;
            uint32_t lodCount = 0;
            uint32_t lodsOffset = 0;
        };

        struct ShaderLOD
        {
            uint32_t meshReferencesOffset = 0;
            uint32_t meshReferenceCount = 0;
        };

        struct ShaderMeshReference
        {
            uint32_t meshOffset = 0;
        };
        
        uint32_t lodsOffset;
        std::vector<char> preprocessData;
        std::vector<std::vector<MaterialInstance*>> materials;
        std::vector<std::vector<std::vector<uint32_t*>>> shaderMeshOffsetReferences;//LODs, material slots, meshes
        std::unordered_map<LODMesh const*, CommonMeshGroup*> meshReferences;

        ModelTransform transformation = ModelTransform();
        uint64_t selfIndex;
        bool isVisible = true;

        class RenderEngine* rendererPtr;
        Model const* modelPtr = NULL;

        void setRendererIndex(uint64_t newIndex) { this->selfIndex = newIndex; }
        std::vector<char> getRasterPreprocessData(uint32_t currentRequiredSize);
        ShaderInputObject getShaderInputObject() const;
        
    public:
        ModelInstance(RenderEngine* renderer, Model const* parentModel, const std::vector<std::unordered_map<uint32_t, MaterialInstance*>>& materials);
        ~ModelInstance();

        void transform(const ModelTransform& newTransform);
        void setVisibility(bool newVisibility) { this->isVisible = newVisibility; }
        
        Model const* getParentModelPtr() const { return modelPtr; }
        const ModelTransform& getTransformation() const { return transformation; }
        const std::vector<std::vector<MaterialInstance*>>& getMaterialInstances() const { return materials; } //LOD index, material slot
        const bool& getVisibility() const { return isVisible; }

        friend class CommonMeshGroup;
        friend class RenderEngine;
        friend class RenderPass;
    };
}