#pragma once
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "RHI/Buffer.h"

#include <filesystem>
#include <unordered_map>

namespace Renderer
{
    struct ModelMesh
    {
        std::shared_ptr<Mesh> mesh;
        uint32_t materialIndex;
    };
    
    class Model
    {
    private:
        std::vector<ModelMesh> meshes;

        Device* devicePtr;
        Commands* commandsPtr;

        void processNode(aiNode* node, const aiScene* scene);
        void processMesh(aiMesh* mesh, const aiScene* scene);
    public:
        Model(const Model& model);
        Model(Device* device, Commands* commands, std::string directory);
        ~Model();

        const std::vector<ModelMesh>& getModelMeshes() const { return meshes; }
    };
}