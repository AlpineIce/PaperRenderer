#pragma once
#include "Material/material.h"
#include "RHI/IndirectDrawBuffer.h"

#include <filesystem>
#include <unordered_map>
#include <list>

namespace Renderer
{
    //----------MODEL CREATION INFO----------//

    struct MeshInfo
    {
        std::shared_ptr<std::vector<Vertex>> vertices;
        std::shared_ptr<std::vector<uint32_t>> indices;
        uint32_t materialIndex;
    };

    struct LODInfo
    {
        std::unordered_map<uint32_t, std::vector<MeshInfo>> meshes; //material index/slot, material associated meshes
    };

    struct ModelCreateInfo
    {
        std::vector<LODInfo> LODs;
    };

    //----------MODEL INFORMATION----------//

    struct LOD //acts more like an individual model
    {
        ShaderLOD shaderLOD;
        std::unordered_map<uint32_t, Material const*> materials; //material index/slot, associated material
        std::unordered_map<uint32_t, std::vector<LODMesh>> meshes; //material index/slot, material associated meshes
    };

    //----------MODEL DECLARATION----------//

    class Model //acts more like a collection of models (LODs)
    {
    private:
        std::vector<LOD> LODs;
        std::shared_ptr<VertexBuffer> vbo;
        std::shared_ptr<IndexBuffer> ibo;
        float sphericalBounds = 0.0f; //calculated as largest possible LOD size in constructor
        uint32_t LODDataOffset;
        uint32_t bufferMeshLODsOffset;

        class RenderEngine* rendererPtr;

    public:
        Model(RenderEngine* renderer, const ModelCreateInfo& creationInfo);
        ~Model();

        std::vector<LOD>& getLODs() { return LODs; }
        void bindBuffers(const VkCommandBuffer& cmdBuffer) const;
        const float& getSphericalBounds() const { return sphericalBounds; }

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
        std::list<ModelInstance*>::iterator objectReference;
        ModelTransform transformation = ModelTransform();
        std::vector<std::unordered_map<uint32_t, Material const*>> materials;
        bool isVisible = true;

        class RenderEngine* rendererPtr;
        Model* modelPtr = NULL;
        
    public:
        ModelInstance(RenderEngine* renderer, Model* parentModel, std::vector<std::unordered_map<uint32_t, Material const*>> materials);
        ~ModelInstance();

        void transform(const ModelTransform& newTransform);
        void setVisibility(bool newVisibility) { this->isVisible = newVisibility; }

        Model* getModelPtr() { return modelPtr; }
        const ModelTransform& getTransformation() const { return transformation; }
        const std::vector<std::unordered_map<uint32_t, Material const*>>& getMaterials() const { return materials; }
        const bool& getVisibility() const { return isVisible; }
    };
}