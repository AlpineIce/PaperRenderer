#include "Model.h"

namespace Renderer
{
    Model::Model(const Model &model)
    {

    }

    Model::Model(Device* device, Commands* commands, std::string directory)
        :devicePtr(device),
        commandsPtr(commands)
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(directory, aiProcess_Triangulate | aiProcess_FlipUVs);
        if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            throw std::runtime_error("Model importing failed at model " + directory);
        }

        processNode(scene->mRootNode, scene);
    }

    Model::~Model()
    {

    }

    void Model::processNode(aiNode *node, const aiScene *scene)
    {
        // process all the node's meshes (if any)
		for (unsigned int i = 0; i < node->mNumMeshes; i++)
		{
			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
			processMesh(mesh, scene);

			//meshes.push_back(Mesh(meshData.vertices, meshData.indices));
		}
		// then do the same for each of its children
		for (unsigned int i = 0; i < node->mNumChildren; i++)
		{
			processNode(node->mChildren[i], scene);
		}
    }

    void Model::processMesh(aiMesh* mesh, const aiScene* scene)
	{
		//vertices
        std::vector<Vertex> vertices(mesh->mNumVertices);
		for (unsigned int i = 0; i < mesh->mNumVertices; i++)
		{
			aiVector3D position = mesh->mVertices[i];
			aiVector3D normal = mesh->mNormals[i];
			aiVector3D texCoords = mesh->mTextureCoords[0][i];
			//aiVector3D tangents = mesh->mBitangents[i];
			//aiVector3D bitangents = mesh->mTangents[i];
			vertices.at(i).position = { position.x, position.y, position.z };
			vertices.at(i).normal = { normal.x, normal.y, normal.z };
			

			//texture coordinates
			if (mesh->mTextureCoords[0])
			{
				vertices.at(i).texCoord = { texCoords.x, texCoords.y };
			}
			else
			{
				vertices.at(i).texCoord = { 0.0f, 0.0f };
			}

			//tangents
			/*if (mesh->mTextureCoords[0])
			{
				vertices.at(i).tangents = { tangents.x, tangents.y, tangents.z };
			}
			else
			{
				vertices.at(i).tangents = { 0.0f, 0.0f, 0.0f };
			}

			//bitangents
			if (mesh->mTextureCoords[0])
			{
				vertices.at(i).bitangents = { bitangents.x, bitangents.y, bitangents.z };
			}
			else
			{
				ververtices.at(i)tex.bitangents = { 0.0f, 0.0f, 0.0f };
			}*/
		}

		//indices
        std::vector<uint32_t> indices;
		for (unsigned int i = 0; i < mesh->mNumFaces; i++)
		{
			aiFace face = mesh->mFaces[i];
			for (unsigned int j = 0; j < face.mNumIndices; j++)
			{
				indices.push_back(face.mIndices[j]);
			}
		}
		
		//material slot
        uint32_t materialIndex = mesh->mMaterialIndex;

        //insert into vector
        ModelMesh returnMesh;
        returnMesh.mesh = std::make_shared<Mesh>(devicePtr, commandsPtr, &vertices, &indices);
        returnMesh.materialIndex = materialIndex;
        meshes.push_back(returnMesh);
	}
}