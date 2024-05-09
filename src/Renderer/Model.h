#pragma once
#include "material.h"
#include "RHI/IndirectDrawBuffer.h"

#include <filesystem>
#include <unordered_map>

namespace PaperRenderer
{
    //----------MODEL CREATION INFO----------//

    struct MeshInfo
    {
        std::vector<PaperMemory::Vertex> vertices;
        std::vector<uint32_t> indices;
        ModelOBB meshOBB;
        uint32_t materialIndex;
    };

    struct ModelCreateInfo
    {
        ModelOBB OBB; //should be calculated by the model loading solution; "furthest" vertex in each axis
        std::vector<std::unordered_map<uint32_t, std::vector<MeshInfo>>> LODs; //material index/slot, material associated meshes
    };

    //----------MODEL INFORMATION----------//

    struct LOD //acts more like an individual model
    {
        ShaderLOD shaderLOD;
        std::unordered_map<uint32_t, MaterialInstance const*> materials; //material index/slot, associated material
        std::unordered_map<uint32_t, std::vector<LODMesh>> meshes; //material index/slot, material associated meshes
    };

    //----------MODEL DECLARATION----------//

    class Model //acts more like a collection of models (LODs)
    {
    private:
        std::vector<LOD> LODs;
        std::unique_ptr<PaperMemory::Buffer> vbo;
        std::unique_ptr<PaperMemory::Buffer> ibo;
        ModelOBB OBB;
        uint32_t LODDataOffset;
        uint32_t bufferMeshLODsOffset;

        class RenderEngine* rendererPtr;
        PaperMemory::DeviceAllocation* allocationPtr;

        std::unique_ptr<PaperMemory::Buffer> createDeviceLocalBuffer(VkDeviceSize size, void* data, VkBufferUsageFlags2KHR usageFlags);

    public:
        Model(RenderEngine* renderer, PaperMemory::DeviceAllocation* allocation, const ModelCreateInfo& creationInfo);
        ~Model();

        std::vector<LOD>& getLODs() { return LODs; }
        void bindBuffers(const VkCommandBuffer& cmdBuffer) const;
        const ModelOBB& getOBB() const { return OBB; }

        //mutable functions used in render loop
        std::vector<ShaderLOD> getLODData(uint32_t currentBufferSize);
        uint32_t getLODDataOffset() const { return LODDataOffset;}
        std::vector<LODMesh> getMeshLODData(uint32_t currentBufferSize);
        uint32_t getMeshLODsOffset() const { return bufferMeshLODsOffset; }

        VkDeviceAddress getVBOAddress() const { return vbo->getBufferDeviceAddress(); }
        VkDeviceAddress getIBOAddress() const { return ibo->getBufferDeviceAddress(); }
    };

    //----------MODEL INSTANCE DECLARATIONS----------//

    class ModelInstance
    {
    private:
        std::vector<std::unordered_map<uint32_t, DrawBufferObject>> meshReferences;
        uint64_t selfIndex;
        ModelTransform transformation = ModelTransform();
        std::vector<std::unordered_map<uint32_t, MaterialInstance const*>> materials;
        bool isVisible = true;
        class RenderEngine* rendererPtr;
        Model* modelPtr = NULL;
        
    public:
        ModelInstance(RenderEngine* renderer, Model* parentModel, std::vector<std::unordered_map<uint32_t, MaterialInstance const*>> materials);
        ~ModelInstance();

        void transform(const ModelTransform& newTransform);
        void setVisibility(bool newVisibility) { this->isVisible = newVisibility; }
        void setRendererIndex(uint64_t newIndex) { this->selfIndex = newIndex; } //FOR RENDERER USE ONLY

        Model* getModelPtr() { return modelPtr; }
        const ModelTransform& getTransformation() const { return transformation; }
        const std::vector<std::unordered_map<uint32_t, MaterialInstance const*>>& getMaterialInstances() const { return materials; }
        const bool& getVisibility() const { return isVisible; }
    };
}