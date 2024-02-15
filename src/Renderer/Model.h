#pragma once
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "Material/material.h"
#include "RHI/IndirectDrawBuffer.h"
#include "glm/gtx/quaternion.hpp"

#include <filesystem>
#include <unordered_map>

namespace Renderer
{
    class Model
    {
    private:
        std::vector<ModelMesh> meshes;

        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;

        void processNode(aiNode* node, const aiScene* scene);
        void processMesh(aiMesh* mesh, const aiScene* scene);
    public:
        Model(const Model& model);
        Model(Device* device, CmdBufferAllocator* commands, std::string directory);
        ~Model();

        const std::vector<ModelMesh>& getModelMeshes() const { return meshes; }
    };

    struct ModelTransform
    {
        glm::vec3 position = glm::vec3(0.0f); //world position
        glm::vec3 scale = glm::vec3(1.0f); //local scale
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); //local rotation
    };

    typedef std::unordered_map<uint32_t, DrawBufferObject> RenderObjectReference;

    class ModelInstance
    {
    private:
        RenderObjectReference objRef;
        class RenderEngine* rendererPtr;
        ModelTransform transformation = ModelTransform();
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        Model const* modelPtr = NULL;
        std::unordered_map<uint32_t, MaterialInstance const*> materials;
    public:
        ModelInstance(RenderEngine* renderer, Model const* parentModel, std::unordered_map<uint32_t, MaterialInstance const*> materials);
        ~ModelInstance();

        void transform(const ModelTransform& newTransform);

        ModelTransform getTransformation() const { return transformation; }
        const std::unordered_map<uint32_t, MaterialInstance const*>& getMaterials() const { return materials; }
        Model const* getModelPtr() const { return modelPtr; }
    };
}